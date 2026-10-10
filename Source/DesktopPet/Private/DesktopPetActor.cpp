#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
#include "DesktopPetMemory.h"
#include "DesktopPetRuntime.h"
#include "Components/PrimitiveComponent.h"

// 通用宿主不预设人物镜头；项目通过 Capture 组件自行布置。
ADesktopPetActor::ADesktopPetActor()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Capture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
    Capture->SetupAttachment(RootComponent);
    Capture->bCaptureEveryFrame=false;
    Capture->bCaptureOnMovement=false;
}
ADesktopPetActor::~ADesktopPetActor()=default;

// 项目也可以关闭自动启动，先创建自己的 Widget / 角色，再手动 Start。
void ADesktopPetActor::BeginPlay(){Super::BeginPlay();if(bAutoStart)PetStartDesktopWindow();}
// 切图、停止 PIE、销毁 Actor 都走同一套资源回收。
void ADesktopPetActor::EndPlay(const EEndPlayReason::Type Reason){PetStopDesktopWindow();Super::EndPlay(Reason);}

// 场景捕获的白名单对 StaticMesh、SkeletalMesh、Niagara 等 PrimitiveComponent 一视同仁。
void ADesktopPetActor::PetRefreshVisibleActors()
{
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->ShowOnlyActors.Reset();Capture->ShowOnlyComponents.Reset();
    if(bIncludeOwnComponents)Capture->ShowOnlyActorComponents(this,true);
    for(AActor* A:VisibleActors)if(IsValid(A))Capture->ShowOnlyActorComponents(A,true);
    for(UPrimitiveComponent* C:VisibleComponents)if(IsValid(C))Capture->ShowOnlyComponent(C);
    if(!VisibleActorTag.IsNone())
        for(TActorIterator<AActor> It(GetWorld());It;++It)
            if(It->ActorHasTag(VisibleActorTag))Capture->ShowOnlyActorComponents(*It,true);
}
// 运行时生成的粒子 Actor 可直接登记，不需要改透明窗口核心。
void ADesktopPetActor::PetAddVisibleActor(AActor* A){if(IsValid(A))VisibleActors.AddUnique(A);PetRefreshVisibleActors();}
// 组件级登记适用于一个 Actor 中只有部分组件应显示的情况。
void ADesktopPetActor::PetAddVisibleComponent(UPrimitiveComponent* C){if(IsValid(C))VisibleComponents.AddUnique(C);PetRefreshVisibleActors();}
// 通过 Tag 仍被包含的对象，移除时还应由项目删除相应 Tag。
void ADesktopPetActor::PetRemoveVisibleActor(AActor* A){VisibleActors.Remove(A);PetRefreshVisibleActors();}

// 启动只建立捕获、UI 绘制器和透明窗口，不创建项目角色或菜单。
bool ADesktopPetActor::PetStartDesktopWindow()
{
    if(Runtime||IsRunningCommandlet()||!FSlateApplication::IsInitialized())return false;
    if(!bHasConfig)
    {
        DesiredConfig=bOverrideDefaultConfig?InitialConfig:PetGetDefaultRuntimeConfig();
        FParse::Value(FCommandLine::Get(),TEXT("PetAA="),DesiredConfig.SupersampleScale);
        FParse::Value(FCommandLine::Get(),TEXT("PetFPS="),DesiredConfig.TargetFrameRate);
        DesiredConfig.bWriteDiagnostics|=FParse::Param(FCommandLine::Get(),TEXT("PetDiagnostics"));
        if(FParse::Param(FCommandLine::Get(),TEXT("PetOpaque")))DesiredConfig.bTransparentWindowEnabled=false;
        if(FParse::Param(FCommandLine::Get(),TEXT("PetFullMemory")))DesiredConfig.bCompactMemory=false;
        DesiredConfig.Normalize();bHasConfig=true;
    }
    InitialCaptureTransform=Capture->GetComponentTransform();InitialOrbitRotation=OrbitRotation;InitialOrbitDistance=OrbitDistance;
    UpdateOrbitCamera();
    if(!OverlayWidget)
    {
        UClass* Class=WidgetOverride.Get()?WidgetOverride.Get():GetDefault<UDesktopPetSettings>()->WidgetClass.LoadSynchronous();
        if(Class)OverlayWidget=CreateWidget<UUserWidget>(GetWorld(),Class);
    }
    Runtime=MakeShared<FDesktopPetRuntime>();Runtime->Owner=this;
    if(!Runtime->Create(DesiredConfig)){Runtime.Reset();return false;}
    FDesktopPetMemory::Update(this,DesiredConfig);
    FDesktopPetFramePacing::Update(this,DesiredConfig,Runtime->Shell.IsInTray());
    SceneTarget=NewObject<UTextureRenderTarget2D>(this);
    SceneTarget->ClearColor=FLinearColor(0,0,0,1);
    SceneTarget->InitCustomFormat(Runtime->Width*Runtime->Scale,Runtime->Height*Runtime->Scale,PF_FloatRGBA,true);
    SceneTarget->UpdateResourceImmediate(true);
    FinalColorTarget=NewObject<UTextureRenderTarget2D>(this);
    FinalColorTarget->ClearColor=FLinearColor::Transparent;
    FinalColorTarget->InitCustomFormat(Runtime->Width*Runtime->Scale,Runtime->Height*Runtime->Scale,PF_FloatRGBA,true);
    FinalColorTarget->UpdateResourceImmediate(true);
    GeometryTarget=NewObject<UTextureRenderTarget2D>(this);
    GeometryTarget->ClearColor=FLinearColor::White;GeometryTarget->bCanCreateUAV=true;
    GeometryTarget->InitCustomFormat(Runtime->Width*Runtime->Scale,Runtime->Height*Runtime->Scale,PF_R16F,true);
    GeometryTarget->UpdateResourceImmediate(true);
    ViewExtension=FSceneViewExtensions::NewExtension<FDesktopPetViewExtension>(FinalColorTarget->GameThread_GetRenderTargetResource(),GeometryTarget->GameThread_GetRenderTargetResource(),SceneTarget->GameThread_GetRenderTargetResource(),this);
    OpacityCapture=NewObject<USceneCaptureComponent2D>(this,NAME_None,RF_Transient);
    OpacityCapture->bCaptureEveryFrame=false;
    OpacityCapture->bCaptureOnMovement=false;
    OpacityCapture->RegisterComponent();
    UITarget=NewObject<UTextureRenderTarget2D>(this);
    UITarget->ClearColor=FLinearColor::Transparent;
    UITarget->InitCustomFormat(Runtime->Width,Runtime->Height,PF_B8G8R8A8,false);
    UITarget->UpdateResourceImmediate(true);
    ConfigureCapturePipeline();
    PetRefreshVisibleActors();
    bConfigPending=false;bWidgetPending=false;
    PetRuntimeConfigApplied.Broadcast(DesiredConfig);
    return true;
}

// 项目的 Widget 实例保留，便于停止/启动显示时保留菜单状态。
void ADesktopPetActor::PetStopDesktopWindow()
{
    if(bStopping)return;
    bStopping=true;
    PetCloseConfigWidget();RestoreDebugCamera();
    UpdateHoveredActor(nullptr);
    if(Runtime)Runtime->ReleaseHeldInput();
    Runtime.Reset();ViewExtension.Reset();
    if(Capture)Capture->TextureTarget=nullptr;
    if(OpacityCapture){OpacityCapture->TextureTarget=nullptr;OpacityCapture->DestroyComponent();OpacityCapture=nullptr;}
    SceneTarget=nullptr;FinalColorTarget=nullptr;GeometryTarget=nullptr;UITarget=nullptr;
    FDesktopPetMemory::Release(this);
    FDesktopPetFramePacing::Release(this);
    bStopping=false;
}
// 仅显式重启才替换 HWND；尺寸和 SSAA 的运行时更改不走这里。
void ADesktopPetActor::PetRestartDesktopWindow(){bRestartRequested=true;}
// 窗口关闭策略延后到 Tick 执行，允许从 UMG 按钮回调调用。
void ADesktopPetActor::PetRequestClose(){if(Runtime)Runtime->CloseRequested=true;}
bool ADesktopPetActor::PetIsDesktopWindowRunning() const{return Runtime.IsValid();}

// UI 更换在下一 Tick 执行，避免正在分发 OnClicked 时释放其 Slate 路径。
void ADesktopPetActor::PetSetOverlayWidget(UUserWidget* Widget){OverlayWidget=Widget;bWidgetPending=true;}
// 未启动的实例也可以先读取并修改默认配置。
FDesktopPetConfig ADesktopPetActor::PetGetDefaultRuntimeConfig(){return GetDefault<UDesktopPetSettings>()->MakeRuntimeConfig();}
FDesktopPetConfig ADesktopPetActor::PetGetRuntimeConfig() const
{
    FDesktopPetConfig Result=bHasConfig?DesiredConfig:(bOverrideDefaultConfig?InitialConfig:PetGetDefaultRuntimeConfig());
    if(Runtime&&!bConfigPending)
    {Result.WindowPosition=PetGetWindowPosition();Result.DisplayScale=Runtime->PresentedScale;}
    return Result;
}
// 任何字段都可通过整组配置运行时修改；普通参数属于实例，Memory 字段由全局协调器协商。
void ADesktopPetActor::PetApplyRuntimeConfig(const FDesktopPetConfig& Config)
{
    if(Runtime)Runtime->ZoomCommitPending=false;
    const FDesktopPetConfig Before=PetGetRuntimeConfig();
    DesiredConfig=Config;DesiredConfig.Normalize();
    if(DesiredConfig.bCenterAnchoredScaling&&DesiredConfig.WindowPosition==Before.WindowPosition)
    {
        // 拖拽或外部移动后重新校准；未应用的多次滚轮请求共享同一个精确中心。
        if(!bCenterAnchorValid||Before.WindowPosition!=CenterAnchorPosition)
            ExactWindowCenter=FVector2D(Before.WindowPosition)+FVector2D(Before.GetDisplaySize())*0.5;
        const FVector2D Position=ExactWindowCenter-FVector2D(DesiredConfig.GetDisplaySize())*0.5;
        DesiredConfig.WindowPosition=FIntPoint(FMath::RoundToInt(Position.X),FMath::RoundToInt(Position.Y));
        bCenterAnchorValid=true;
    }
    else
    {
        ExactWindowCenter=FVector2D(DesiredConfig.WindowPosition)+FVector2D(DesiredConfig.GetDisplaySize())*0.5;
        bCenterAnchorValid=true;
    }
    CenterAnchorPosition=DesiredConfig.WindowPosition;
    bHasConfig=true;bConfigPending=true;
}
// 尺寸、位置和质量的快捷节点最终仍调用同一个 PetApplyRuntimeConfig。
void ADesktopPetActor::PetSetDisplaySize(FIntPoint Size)
{
    auto C=PetGetRuntimeConfig();
    C.WindowSize=FIntPoint(FMath::RoundToInt(Size.X/C.DisplayScale),FMath::RoundToInt(Size.Y/C.DisplayScale));
    PetApplyRuntimeConfig(C);
}
FIntPoint ADesktopPetActor::PetGetDisplaySize() const{return Runtime&&!bConfigPending?Runtime->DisplaySize:PetGetRuntimeConfig().GetDisplaySize();}
// 读真实 HWND 坐标，使拖拽之后再改质量不会把窗口跳回初始位置。
FIntPoint ADesktopPetActor::PetGetWindowPosition() const
{
    if(!Runtime)return PetGetRuntimeConfig().WindowPosition;
    FPetDpiScope Scope;RECT R{};GetWindowRect(Runtime->Window,&R);return FIntPoint(R.left,R.top);
}
// 手动拖拽接口可直接连接 Enhanced Input 的 Started / Completed。
bool ADesktopPetActor::PetBeginWindowDrag(){return Runtime&&Runtime->BeginDrag();}
void ADesktopPetActor::PetEndWindowDrag(){if(Runtime)Runtime->FinishDrag();}
bool ADesktopPetActor::PetIsWindowDragging() const{return Runtime&&Runtime->Dragging;}
void ADesktopPetActor::PetSetInteractionTiming(float Delay,float Seconds)
{
    auto C=PetGetRuntimeConfig();C.MenuCloseDelay=Delay;C.MenuAnimationSeconds=Seconds;PetApplyRuntimeConfig(C);
}
// 动作总线不硬编码退出、聊天、换动作等项目行为。
void ADesktopPetActor::PetDispatchAction(FName Action)
{
    if(Runtime){++Runtime->UIClicks;Runtime->LastAction=Action.ToString();}
    PetAction.Broadcast(Action);
}
// 诊断函数保留原来的调用方式。
void ADesktopPetActor::PetSaveDiagnosticFrame(){if(Runtime)Runtime->Report(true);}
float ADesktopPetActor::PetGetInteractionProgress() const{return Runtime?Runtime->OpenAmount:0.f;}
int64 ADesktopPetActor::PetGetPresentedFrameCount() const{return Runtime?Runtime->Frames:0;}
void* ADesktopPetActor::GetNativeWindowHandle() const{return Runtime?Runtime->Window:nullptr;}
// 查询与原生悬停使用同一条 UI 优先、Alpha、相机反投影和 Actor 白名单路径。
AActor* ADesktopPetActor::PetGetInteractionActorAtPixel(FVector2D Position) const
{
    return Runtime?Runtime->PickActor(Position):nullptr;
}
// 所有资源更改统一在安全的 Tick 边界处理，随后按限频捕获并输出。
void ADesktopPetActor::Tick(float Delta)
{
    Super::Tick(Delta);
    if(bRestartRequested){bRestartRequested=false;PetStopDesktopWindow();PetStartDesktopWindow();}
    if(!Runtime)return;
    // 蓝图事件允许停止显示；保留当前调用栈资源，并在回调后检查是否已更换运行时。
    const TSharedPtr<FDesktopPetRuntime> FrameRuntime=Runtime;
    if(Runtime->CloseRequested)
    {
        const bool Exit=Runtime->ExitOnClose&&!GIsEditor;
        if(Exit)FPlatformMisc::RequestExit(false);
        PetStopDesktopWindow();return;
    }
    if(FrameRuntime->RestoreRequested){FrameRuntime->RestoreRequested=false;PetRestoreFromTray();}
    if(Runtime!=FrameRuntime)return;
    if(bConfigPending)
    {
        bConfigPending=false;
        FDesktopPetMemory::Update(this,DesiredConfig);
    FDesktopPetFramePacing::Update(this,DesiredConfig,Runtime->Shell.IsInTray());
        Runtime->ApplyConfig(DesiredConfig);
        PetRuntimeConfigApplied.Broadcast(DesiredConfig);
    }
    if(Runtime!=FrameRuntime)return;
    UpdateOrbitCamera();UpdateDebugCamera();
    if(bWidgetPending){bWidgetPending=false;Runtime->SetWidget();}
    if(!FrameRuntime->Shell.IsInTray())
    {
        FrameRuntime->UpdateZoom();
        FrameRuntime->PollInput(Delta);
    }
    if(Runtime!=FrameRuntime)return;
    FrameRuntime->CaptureFrame(Delta);
    if(Runtime->Diagnostics&&FPlatformTime::Seconds()-Runtime->LastReport>2)
    {
        Runtime->LastReport=FPlatformTime::Seconds();Runtime->Report(true);
    }
}

// 运行时更新 WindowSize；保留其他实例参数。
void ADesktopPetActor::PetSetWindowSize(FIntPoint Value)
{
    auto C=PetGetRuntimeConfig();C.WindowSize=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 WindowPosition；保留其他实例参数。
void ADesktopPetActor::PetSetWindowPosition(FIntPoint Value)
{
    auto C=PetGetRuntimeConfig();C.WindowPosition=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 DisplayScale；保留其他实例参数。
void ADesktopPetActor::PetSetDisplayScale(float Value)
{
    auto C=PetGetRuntimeConfig();C.DisplayScale=Value;PetApplyRuntimeConfig(C);
}

// 运行时只替换系统窗口身份，其他实例参数及配置应用时序保持一致。
void ADesktopPetActor::PetSetWindowMode(EDesktopPetWindowMode Mode)
{
    auto C=PetGetRuntimeConfig();C.WindowMode=Mode;PetApplyRuntimeConfig(C);
}

// 运行时更新 bAlwaysOnTop；保留其他实例参数。
void ADesktopPetActor::PetSetAlwaysOnTop(bool Value)
{
    auto C=PetGetRuntimeConfig();C.bAlwaysOnTop=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 bHideGameWindow；保留其他实例参数。
void ADesktopPetActor::PetSetHideGameWindow(bool Value)
{
    auto C=PetGetRuntimeConfig();C.bHideGameWindow=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 bDraggable；保留其他实例参数。
void ADesktopPetActor::PetSetDraggable(bool Value)
{
    auto C=PetGetRuntimeConfig();C.bDraggable=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 SupersampleScale；保留其他实例参数。
void ADesktopPetActor::PetSetSupersampleScale(int32 Value)
{
    auto C=PetGetRuntimeConfig();C.SupersampleScale=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 TargetFrameRate；保留其他实例参数。
void ADesktopPetActor::PetSetTargetFrameRate(int32 Value)
{
    auto C=PetGetRuntimeConfig();C.TargetFrameRate=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 Exposure；保留其他实例参数。
void ADesktopPetActor::PetSetExposure(float Value)
{
    auto C=PetGetRuntimeConfig();C.Exposure=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 bClickThroughTransparentPixels；保留其他实例参数。
void ADesktopPetActor::PetSetClickThroughTransparentPixels(bool Value)
{
    auto C=PetGetRuntimeConfig();C.bClickThroughTransparentPixels=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 HitAlphaThreshold；保留其他实例参数。
void ADesktopPetActor::PetSetHitAlphaThreshold(int32 Value)
{
    auto C=PetGetRuntimeConfig();C.HitAlphaThreshold=Value;PetApplyRuntimeConfig(C);
}

// 运行时更新 bWriteDiagnostics；保留其他实例参数。
void ADesktopPetActor::PetSetWriteDiagnostics(bool Value)
{
    auto C=PetGetRuntimeConfig();C.bWriteDiagnostics=Value;PetApplyRuntimeConfig(C);
}

// 所有快捷节点最终走同一配置应用路径，运行时切换不重建窗口。
void ADesktopPetActor::PetSetCompactMemory(bool bEnabled)
{
    auto C=PetGetRuntimeConfig();C.bCompactMemory=bEnabled;PetApplyRuntimeConfig(C);
}
void ADesktopPetActor::PetSetMemoryCapacities(int32 ShadowPages,int32 SurfaceAtlasSize,int32 RadianceProbes,int32 IdlePoolMB)
{
    auto C=PetGetRuntimeConfig();C.ShadowPageCapacity=ShadowPages;C.SurfaceCacheCapacity=SurfaceAtlasSize;
    C.RadianceProbeCapacity=RadianceProbes;C.IdleRenderTargetPoolMB=IdlePoolMB;PetApplyRuntimeConfig(C);
}
bool ADesktopPetActor::PetIsCompactMemoryActive() const {return FDesktopPetMemory::IsActive(this);}

// 原生滚轮与增强输入共用同一入口、命中规则和物理屏幕坐标，不需要再连接缩放 Setter。
bool ADesktopPetActor::PetZoomAtScreenPosition(float WheelDelta,FVector2D ScreenPosition)
{return Runtime&&Runtime->ZoomAtScreenPosition(WheelDelta,ScreenPosition);}
bool ADesktopPetActor::PetIsZooming() const{return Runtime&&Runtime->Zooming;}
float ADesktopPetActor::PetGetZoomTargetScale() const{return Runtime&&Runtime->Zooming?Runtime->ZoomTargetScale:PetGetDisplayScale();}
void ADesktopPetActor::PetSetZoomAnimationSeconds(float Value)
{auto C=PetGetRuntimeConfig();C.ZoomAnimationSeconds=Value;PetApplyRuntimeConfig(C);}

// 新接口仍共用整组配置路径，不在 UMG 回调内重建渲染资源。
void ADesktopPetActor::PetSetShowInTaskbar(bool bShow)
{PetSetWindowMode(bShow?EDesktopPetWindowMode::Application:EDesktopPetWindowMode::ToolWindow);}
void ADesktopPetActor::PetSetAllowScreenCapture(bool bAllow)
{auto C=PetGetRuntimeConfig();C.bAllowScreenCapture=bAllow;PetApplyRuntimeConfig(C);}
void ADesktopPetActor::PetSetLimitEngineFrameRate(bool bEnabled)
{auto C=PetGetRuntimeConfig();C.bLimitEngineFrameRate=bEnabled;PetApplyRuntimeConfig(C);}
void ADesktopPetActor::PetSetTrayFrameRate(int32 FPS)
{auto C=PetGetRuntimeConfig();C.TrayFrameRate=FPS;PetApplyRuntimeConfig(C);}
float ADesktopPetActor::PetGetEffectiveEngineFrameRateLimit() const
{return FDesktopPetFramePacing::GetEffectiveLimit();}
bool ADesktopPetActor::PetIsHiddenToTray() const{return Runtime&&Runtime->Shell.IsInTray();}

// 托盘隐藏不销毁 Actor、角色、UI 或相机；先获得可用恢复入口，再释放输入。
bool ADesktopPetActor::PetHideToTray()
{
    const auto Current=Runtime;
    if(!Current||bStopping||!Current->Config.bTransparentWindowEnabled)return false;
    if(Current->Shell.IsInTray())return true;
    if(!Current->Shell.HideToTray())return false;
    Current->FinishZoom();Current->ReleaseHeldInput();
    if(Runtime!=Current||!Current->Shell.IsInTray())return false;
    PetCloseConfigWidget();
    if(Runtime!=Current||!Current->Shell.IsInTray())return false;
    UpdateHoveredActor(nullptr);
    if(Runtime!=Current||!Current->Shell.IsInTray())return false;
    FDesktopPetFramePacing::Update(this,Current->Config,true);
    PetTrayStateChanged.Broadcast(true);
    if(Runtime==Current&&Current->Shell.IsInTray())PetEventTrayStateChanged(true);
    return Runtime==Current&&Current->Shell.IsInTray();
}
// 首次恢复立刻安排新捕获，上一张有效图像保留到新帧完成，不闪回 UE 游戏窗口。
bool ADesktopPetActor::PetRestoreFromTray()
{
    const auto Current=Runtime;
    if(!Current||bStopping)return false;
    if(!Current->Shell.RestoreFromTray(Current->Config.bTransparentWindowEnabled))return false;
    Current->NextCaptureTime=0.;
    FDesktopPetFramePacing::Update(this,Current->Config,false);
    PetTrayStateChanged.Broadcast(false);
    if(Runtime==Current&&!Current->Shell.IsInTray())PetEventTrayStateChanged(false);
    return Runtime==Current&&!Current->Shell.IsInTray();
}
