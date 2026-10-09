#include "DesktopPetEditorSubsystem.h"
#include "Editor.h"
#include "Interfaces/IPluginManager.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DesktopPetEditor"

/** 模块只负责按钮和图标注册，配置业务由编辑器子系统统一实现。 */
class FDesktopPetEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (IsRunningCommandlet()) return;
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DesktopPet"));
        if (!Plugin) return;
        Style = MakeShared<FSlateStyleSet>(TEXT("DesktopPetEditorStyle"));
        const FString Icon = Plugin->GetBaseDir() / TEXT("Resources/Icon128.png");
        Style->Set(TEXT("DesktopPet.Configure"), new FSlateImageBrush(Icon, FVector2D(40,40)));
        Style->Set(TEXT("DesktopPet.Configure.Small"), new FSlateImageBrush(Icon, FVector2D(20,20)));
        FSlateStyleRegistry::RegisterSlateStyle(*Style);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FDesktopPetEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        // 热重载、关闭编辑器时一并移除回调，避免重复工具栏或悬空图标。
        UToolMenus::UnRegisterStartupCallback(this);
        UToolMenus::UnregisterOwner(this);
        if (Style) { FSlateStyleRegistry::UnRegisterSlateStyle(*Style); Style.Reset(); }
    }

private:
    TSharedPtr<FSlateStyleSet> Style;

    static UDesktopPetEditorSubsystem* GetPetTools()
    {
        return GEditor ? GEditor->GetEditorSubsystem<UDesktopPetEditorSubsystem>() : nullptr;
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        const FUIAction Configure(
            FExecuteAction::CreateLambda([] { if (auto* Tools = GetPetTools()) Tools->PetConfigureProject(); }),
            FCanExecuteAction::CreateLambda([] { const auto* Tools = GetPetTools(); return Tools && Tools->PetCanConfigureProject(); }));
        const FText Label = LOCTEXT("Configure", "配置桌宠");
        const FText Tooltip = LOCTEXT("ConfigureTip", "补齐桌宠默认配置、DX11/SM5 与 DX12/SM6；当前已保存地图设为游戏入口并加入打包。选中模型可自动接入宿主。保留已有光照、阴影、抗锯齿和 UI；生成备份及报告。场景改动需保存关卡，新 RHI/启动配置需重启编辑器。");
        const FSlateIcon Icon(Style->GetStyleSetName(), TEXT("DesktopPet.Configure"));
        UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.User"))
            ->FindOrAddSection(TEXT("DesktopPet"))
            .AddEntry(FToolMenuEntry::InitToolBarButton(TEXT("DesktopPet.ConfigureProject"), Configure, Label, Tooltip, Icon));
        // 菜单作为小窗口下工具栏折叠时的备用入口。
        auto& Section = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"))->FindOrAddSection(TEXT("DesktopPet"), LOCTEXT("Group", "DesktopPet"));
        Section.AddMenuEntry(TEXT("DesktopPet.ConfigureProject"), Label, Tooltip, Icon, Configure);
        Section.AddMenuEntry(TEXT("DesktopPet.Settings"), LOCTEXT("Settings", "桌宠项目设置"), LOCTEXT("SettingsTip", "编辑 DesktopPet 默认参数。"), Icon,
            FUIAction(FExecuteAction::CreateLambda([] {
                if (auto* Settings = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
                    Settings->ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("DesktopPetSettings"));
            })));
    }
};

IMPLEMENT_MODULE(FDesktopPetEditorModule, DesktopPetEditor)
#undef LOCTEXT_NAMESPACE
