#include "DesktopPetEditorSubsystem.h"
#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/MeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "GameMapsSettings.h"
#include "Settings/ProjectPackagingSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY_STATIC(LogDesktopPetSetup, Log, All);

namespace DesktopPetSetup
{
    /** 只追加确实需要的配置段，原有注释、数组规则和无关字段保持原样。 */
    struct FIniPatch
    {
        FString Path, Before, After, LastSection;
        FConfigFile Values;
        bool bReadable = true;
        explicit FIniPatch(const TCHAR* Name)
        {
            Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / Name);
            if (IFileManager::Get().FileExists(*Path))
            {
                bReadable = FFileHelper::LoadFileToString(Before, *Path);
                // Combine 解释 + / - / ! 数组语义，以便重复点击保持幂等。
                Values.Combine(Path);
            }
            After = Before;
        }
        void Append(const FString& Section, const FString& Line)
        {
            if (LastSection != Section)
            {
                After += FString::Printf(TEXT("\n[%s]\n"), *Section);
                LastSection = Section;
            }
            After += Line + TEXT("\n");
        }
        bool HasValue(const TCHAR* Section, const FString& Key) const
        {
            FString Value;
            return Values.GetString(Section, *Key, Value);
        }
        void Set(const TCHAR* Section, const FString& Key, const FString& Value)
        {
            FString Existing;
            if (!Values.GetString(Section, *Key, Existing) || Existing != Value)
            {
                Append(Section, Key + TEXT("=") + Value);
                Values.SetString(Section, *Key, *Value);
            }
        }
        void Add(const TCHAR* Section, const TCHAR* Key, const FString& Value)
        {
            TArray<FString> Existing;
            Values.GetArray(Section, Key, Existing);
            if (!Existing.Contains(Value))
            {
                Append(Section, FString(TEXT("+")) + Key + TEXT("=") + Value);
                Existing.Add(Value);
                Values.SetArray(Section, Key, Existing);
            }
        }
        bool Changed() const { return Before != After; }
    };

    /** 过滤编辑器图标、碰撞体、灯光和体积；Niagara 等有真实场景代理的 Primitive 同样可登记。 */
    bool IsDisplayActor(AActor* Actor)
    {
        if (!IsValid(Actor) || Actor->IsA<ADesktopPetActor>() || Actor->IsEditorOnly() || Actor->IsHidden()) return false;
        TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
        for (UPrimitiveComponent* Component : Components)
            if (Component && !Component->IsEditorOnly() && Component->IsVisible() && !Component->bHiddenInGame &&
                (Component->IsA<UMeshComponent>() || Component->GetClass()->GetName().Contains(TEXT("Niagara")))) return true;
        return false;
    }
}

bool UDesktopPetEditorSubsystem::PetCanConfigureProject() const
{
    return GEditor && !GEditor->PlayWorld && !GEditor->bIsSimulatingInEditor &&
        GEditor->GetEditorWorldContext().World() && !IsRunningCommandlet();
}

bool UDesktopPetEditorSubsystem::FinishSetup(bool bSuccess, const FString& Report)
{
    LastSetupReport = Report;
    if (!LastSetupReportPath.IsEmpty() && !FFileHelper::SaveStringToFile(Report, *LastSetupReportPath, FFileHelper::EEncodingOptions::ForceUTF8))
        LastSetupReport += TEXT("\n报告文件写入失败，请从输出日志复制结果。");
    UE_LOG(LogDesktopPetSetup, Display, TEXT("%s"), *LastSetupReport);
    FNotificationInfo Info(FText::FromString(bSuccess
        ? TEXT("DesktopPet 配置已完成。场景有改动时请保存关卡；配置明细见报告。")
        : TEXT("DesktopPet 配置未完成，请查看报告或输出日志。")));
    Info.ExpireDuration = 12;
    if (!LastSetupReportPath.IsEmpty())
    {
        const FString ReportPath = LastSetupReportPath;
        Info.HyperlinkText = FText::FromString(TEXT("打开配置报告"));
        Info.Hyperlink = FSimpleDelegate::CreateLambda([ReportPath] { FPlatformProcess::LaunchFileInDefaultExternalApplication(*ReportPath); });
    }
    if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
        Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
    return bSuccess;
}

bool UDesktopPetEditorSubsystem::PetConfigureProject()
{
    using namespace DesktopPetSetup;
    LastSetupReportPath.Reset();
    if (!PetCanConfigureProject()) return FinishSetup(false, TEXT("请停止 PIE/模拟，在关卡编辑器中配置桌宠。未修改工程。"));
    UWorld* World = GEditor->GetEditorWorldContext().World();
    const FString Map = World->GetOutermost()->GetName();
    // 未保存地图没有稳定的包名；先保存后再点按钮，不把 /Temp 写入启动配置。
    if (!Map.StartsWith(TEXT("/Game/")) || !FPackageName::DoesPackageExist(Map))
        return FinishSetup(false, TEXT("请先把当前关卡保存到项目 Content，再点击配置桌宠。未修改工程。"));

    TArray<AActor*> SelectedModels;
    TArray<ADesktopPetActor*> Hosts, SelectedHosts;
    for (TActorIterator<ADesktopPetActor> It(World); It; ++It) Hosts.Add(*It);
    for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
    {
        AActor* Actor = Cast<AActor>(*It);
        if (!Actor || Actor->GetWorld() != World) continue;
        if (auto* Host = Cast<ADesktopPetActor>(Actor)) SelectedHosts.Add(Host);
        else if (IsDisplayActor(Actor)) SelectedModels.Add(Actor);
    }
    if (SelectedHosts.Num() > 1 || (Hosts.Num() > 1 && SelectedHosts.IsEmpty()))
        return FinishSetup(false, TEXT("当前关卡有多个桌宠宿主，请只选中需要配置的一个宿主，可同时选中模型。未修改工程。"));
    ADesktopPetActor* Host = SelectedHosts.Num() == 1 ? SelectedHosts[0] : (Hosts.Num() == 1 ? Hosts[0] : nullptr);

    FIniPatch Game(TEXT("DefaultGame.ini")), Engine(TEXT("DefaultEngine.ini"));
    if (!Game.bReadable || !Engine.bReadable)
        return FinishSetup(false, TEXT("读取 DefaultGame.ini/DefaultEngine.ini 失败。未修改工程。"));
    int32 AddedDefaults = 0;
    const TCHAR* PetSection = TEXT("/Script/DesktopPet.DesktopPetSettings");
    const UDesktopPetSettings* Defaults = GetDefault<UDesktopPetSettings>();
    // 从当前有效 CDO 补齐缺失键，已写入项目的用户值不覆盖，也不另维护一份容易过期的默认值表。
    for (TFieldIterator<FProperty> It(UDesktopPetSettings::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
    {
        if (!It->HasAnyPropertyFlags(CPF_Config) || Game.HasValue(PetSection, It->GetName())) continue;
        FString Value;
        // 直接导出单个值，避免增量导出把 False、0 或空软引用省略成空字符串。
        It->ExportTextItem_Direct(Value, It->ContainerPtrToValuePtr<void>(Defaults), nullptr, nullptr, PPF_None);
        Game.Set(PetSection, It->GetName(), Value);
        ++AddedDefaults;
    }
    // 双 RHI 着色器只增加缺少的格式，不改 DefaultGraphicsRHI 或任何 RendererSettings。
    const TCHAR* WindowsSection = TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings");
    Engine.Add(WindowsSection, TEXT("D3D11TargetedShaderFormats"), TEXT("PCD3D_SM5"));
    Engine.Add(WindowsSection, TEXT("D3D12TargetedShaderFormats"), TEXT("PCD3D_SM6"));
    Engine.Set(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), Map);
    Game.Add(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("MapsToCook"), FString::Printf(TEXT("(FilePath=\"%s\")"), *Map));

    // 全部预检通过才开始写磁盘，不自动修改只读属性或源代码管理状态。
    for (FIniPatch* Patch : { &Game, &Engine })
        if (Patch->Changed() && IFileManager::Get().IsReadOnly(*Patch->Path))
            return FinishSetup(false, FString::Printf(TEXT("配置文件只读，请先签出或解除只读：%s。未修改工程。"), *Patch->Path));
    if (!IFileManager::Get().MakeDirectory(*FPaths::ProjectConfigDir(), true))
        return FinishSetup(false, TEXT("创建项目 Config 目录失败。未修改工程。"));
    const FString BackupDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DesktopPetSetup") /
        (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    if (!IFileManager::Get().MakeDirectory(*BackupDir, true))
        return FinishSetup(false, TEXT("创建配置备份目录失败。未修改工程。"));
    LastSetupReportPath = BackupDir / TEXT("Report.txt");
    for (FIniPatch* Patch : { &Game, &Engine })
        if (Patch->Changed() && IFileManager::Get().FileExists(*Patch->Path) &&
            IFileManager::Get().Copy(*(BackupDir / FPaths::GetCleanFilename(Patch->Path)), *Patch->Path) != COPY_OK)
            return FinishSetup(false, TEXT("备份 ini 失败，配置未写入。"));

    TArray<FIniPatch*> Written;
    for (FIniPatch* Patch : { &Game, &Engine })
    {
        if (!Patch->Changed()) continue;
        if (!FFileHelper::SaveStringToFile(Patch->After, *Patch->Path, FFileHelper::EEncodingOptions::ForceUTF8))
        {
            // 包括本次可能部分写入的文件也恢复；任何恢复失败都会明确列出备份位置。
            Written.Add(Patch);
            FString Error = TEXT("写入 ini 失败，已尝试恢复原文本。备份目录：") + BackupDir;
            for (FIniPatch* Previous : Written)
                if (!FFileHelper::SaveStringToFile(Previous->Before, *Previous->Path, FFileHelper::EEncodingOptions::ForceUTF8))
                    Error += TEXT("\n恢复失败：") + Previous->Path;
            return FinishSetup(false, Error);
        }
        Written.Add(Patch);
    }
    // 重建配置缓存并刷新编辑器中的设置对象；RHI 和启动分配参数仍需重启进程。
    FConfigCacheIni::LoadGlobalIniFile(GGameIni, TEXT("Game"), nullptr, true);
    FConfigCacheIni::LoadGlobalIniFile(GEngineIni, TEXT("Engine"), nullptr, true);
    GetMutableDefault<UDesktopPetSettings>()->ReloadConfig();
    GetMutableDefault<UGameMapsSettings>()->ReloadConfig();
    GetMutableDefault<UProjectPackagingSettings>()->ReloadConfig();

    FString Report = FString::Printf(TEXT("DesktopPet 4.4 工程配置\n当前地图：%s\n补齐桌宠默认参数：%d 项\n写入配置文件：%d 个\n"), *Map, AddedDefaults, Written.Num());
    Report += TEXT("已保证 DX11/SM5 与 DX12/SM6；当前地图设为 GameDefaultMap 并加入 MapsToCook。\n");
    Report += TEXT("保留已有默认 RHI、GI、反射、VSM/传统阴影、全局 AA、光追、静态光照及 Alpha Output。\n");
    if (!SelectedModels.IsEmpty())
    {
        const FScopedTransaction Transaction(FText::FromString(TEXT("DesktopPet 接入选中模型")));
        const bool bCreate = Host == nullptr;
        if (!Host)
        {
            // 使用编辑器生成入口，让宿主创建与名单修改都进入同一个撤销事务。
            Host = Cast<ADesktopPetActor>(GEditor->AddActor(World->PersistentLevel,
                ADesktopPetActor::StaticClass(), FTransform::Identity, true, RF_Transactional, false));
            if (Host) { Host->SetActorLabel(TEXT("DesktopPetHost")); Host->VisibleActorTag = NAME_None; }
        }
        if (Host)
        {
            Host->Modify();
            for (AActor* Model : SelectedModels) Host->VisibleActors.AddUnique(Model);
            if (bCreate)
            {
                // 新宿主按模型联合包围球留出 15% 边距。已有宿主相机、UI、实例覆盖保持原样。
                FBox Bounds(ForceInit);
                for (AActor* Model : SelectedModels) Bounds += Model->GetComponentsBoundingBox(true);
                if (Bounds.IsValid)
                {
                    const float Aspect = float(FMath::Max(1, Defaults->WindowSize.X)) / FMath::Max(1, Defaults->WindowSize.Y);
                    const float HalfFov = FMath::DegreesToRadians(Host->Capture->FOVAngle * 0.5f);
                    const float LimitingHalfFov = FMath::Min(HalfFov, FMath::Atan(FMath::Tan(HalfFov) / Aspect));
                    Host->OrbitFocusActor = SelectedModels[0];
                    Host->OrbitCenterOffset = Bounds.GetCenter() - SelectedModels[0]->GetActorLocation();
                    Host->OrbitDistance = FMath::Max(50.f, float(Bounds.GetExtent().Size()) * 1.15f / FMath::Sin(LimitingHalfFov));
                    Host->Capture->SetWorldLocationAndRotation(Bounds.GetCenter() - Host->OrbitRotation.Vector() * Host->OrbitDistance, Host->OrbitRotation);
                }
            }
            Host->PetRefreshVisibleActors();
            Host->MarkPackageDirty();
            Report += FString::Printf(TEXT("%s宿主：%s；本次登记 %d 个选中模型/特效（自动去重）。请保存关卡。\n"), bCreate ? TEXT("新建") : TEXT("复用"), *Host->GetActorLabel(), SelectedModels.Num());
        }
        else Report += TEXT("创建宿主失败；ini 已配置，请手动放置 DesktopPetActor。\n");
    }
    else if (Host) Report += TEXT("复用现有桌宠宿主；未选择新模型，白名单、相机、UI 和实例覆盖保持原样。\n");
    else Report += TEXT("待办：选中要显示的模型再点一次，可创建通用宿主；或自行放置 DesktopPetActor 并填写白名单。\n");
    if (Host && !Host->bAutoStart) Report += TEXT("提示：已有宿主关闭了 AutoStart，请在项目逻辑中调用 PetStartDesktopWindow。\n");
    if (Host && Host->bOverrideDefaultConfig) Report += TEXT("提示：已有宿主使用 InitialConfig 覆盖，项目默认值不会覆盖该实例。\n");
    Report += TEXT("UI、灯光、角色蓝图仍由项目选择。WidgetOverride 为常驻 UI，ConfigWidgetClass 为双击菜单。纯字符串动态加载的资产需自行配置 Cook。\n");
    Report += TEXT("首次增加 RHI 着色器格式或修改启动内存参数后请重启编辑器，双 RHI 首次打包可能重新编译着色器。\n");
    Report += TEXT("场景改动可 Ctrl+Z 撤销；ini 不属于场景撤销，请关闭编辑器后用同目录备份恢复。\n备份及报告：") + BackupDir;
    return FinishSetup(true, Report);
}
