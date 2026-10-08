#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
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
void ADesktopPetActor::BeginPlay(){Super::BeginPlay();if(bAutoStart)StartDesktopWindow();}
// 切图、停止 PIE、销毁 Actor 都走同一套资源回收。
void ADesktopPetActor::EndPlay(const EEndPlayReason::Type Reason){StopDesktopWindow();Super::EndPlay(Reason);}

// 场景捕获的白名单对 StaticMesh、SkeletalMesh、Niagara 等 PrimitiveComponent 一视同仁。
void ADesktopPetActor::RefreshVisibleActors()
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
void ADesktopPetActor::AddVisibleActor(AActor* A){if(IsValid(A))VisibleActors.AddUnique(A);RefreshVisibleActors();}
// 组件级登记适用于一个 Actor 中只有部分组件应显示的情况。
void ADesktopPetActor::AddVisibleComponent(UPrimitiveComponent* C){if(IsValid(C))VisibleComponents.AddUnique(C);RefreshVisibleActors();}
// 通过 Tag 仍被包含的对象，移除时还应由项目删除相应 Tag。
void ADesktopPetActor::RemoveVisibleActor(AActor* A){VisibleActors.Remove(A);RefreshVisibleActors();}

// 启动只建立捕获、UI 绘制器和透明窗口，不创建项目角色或菜单。
bool ADesktopPetActor::StartDesktopWindow()
{
    if(Runtime||IsRunningCommandlet()||!FSlateApplication::IsInitialized())return false;
    if(!bHasConfig)
    {
        DesiredConfig=bOverrideDefaultConfig?InitialConfig:GetDefaultRuntimeConfig();
        FParse::Value(FCommandLine::Get(),TEXT("PetAA="),DesiredConfig.SupersampleScale);
        FParse::Value(FCommandLine::Get(),TEXT("PetFPS="),DesiredConfig.TargetFrameRate);
        DesiredConfig.bWriteDiagnostics|=FParse::Param(FCommandLine::Get(),TEXT("PetDiagnostics"));
        DesiredConfig.Normalize();bHasConfig=true;
    }
    if(!OverlayWidget)
    {
        UClass* Class=WidgetOverride.Get()?WidgetOverride.Get():GetDefault<UDesktopPetSettings>()->WidgetClass.LoadSynchronous();
        if(Class)OverlayWidget=CreateWidget<UUserWidget>(GetWorld(),Class);
    }
    Runtime=MakeShared<FDesktopPetRuntime>();Runtime->Owner=this;
    if(!Runtime->Create(DesiredConfig)){Runtime.Reset();return false;}
    SceneTarget=NewObject<UTextureRenderTarget2D>(this);
    SceneTarget->ClearColor=FLinearColor(0,0,0,1);
    SceneTarget->InitCustomFormat(Runtime->Width*Runtime->Scale,Runtime->Height*Runtime->Scale,PF_FloatRGBA,true);
    SceneTarget->UpdateResourceImmediate(true);
    UITarget=NewObject<UTextureRenderTarget2D>(this);
    UITarget->ClearColor=FLinearColor::Transparent;
    UITarget->InitCustomFormat(Runtime->Width,Runtime->Height,PF_B8G8R8A8,false);
    UITarget->UpdateResourceImmediate(true);
    Capture->TextureTarget=SceneTarget;
    Capture->CaptureSource=SCS_SceneColorHDR;
    Capture->CompositeMode=SCCM_Overwrite;
    Capture->bAlwaysPersistRenderingState=true;
    // 保留模型、粒子和半透明；关闭会填满背景或破坏透明轮廓的环境后处理。
    Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetVolumetricFog(false);Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetAntiAliasing(false);
    Capture->ShowFlags.SetEyeAdaptation(false);Capture->ShowFlags.SetBloom(false);
    Capture->ShowFlags.SetScreenSpaceReflections(false);
    Capture->ShowFlags.SetParticles(true);Capture->ShowFlags.SetNiagara(true);
    Capture->ShowFlags.SetTranslucency(DesiredConfig.bCaptureTranslucency);
    Capture->ShowFlags.SetSeparateTranslucency(DesiredConfig.bCaptureTranslucency);
    RefreshVisibleActors();
    bConfigPending=false;bWidgetPending=false;
    OnRuntimeConfigApplied.Broadcast(DesiredConfig);
    return true;
}

// 项目的 Widget 实例保留，便于停止/启动显示时保留菜单状态。
void ADesktopPetActor::StopDesktopWindow()
{
    if(Runtime)Runtime->ReleaseHeldInput();
    Runtime.Reset();
    if(Capture)Capture->TextureTarget=nullptr;
    SceneTarget=nullptr;UITarget=nullptr;
}
// 仅显式重启才替换 HWND；尺寸和 SSAA 的运行时更改不走这里。
void ADesktopPetActor::RestartDesktopWindow(){bRestartRequested=true;}
// 窗口关闭策略延后到 Tick 执行，允许从 UMG 按钮回调调用。
void ADesktopPetActor::RequestClose(){if(Runtime)Runtime->CloseRequested=true;}
bool ADesktopPetActor::IsDesktopWindowRunning() const{return Runtime.IsValid();}

// UI 更换在下一 Tick 执行，避免正在分发 OnClicked 时释放其 Slate 路径。
void ADesktopPetActor::SetOverlayWidget(UUserWidget* Widget){OverlayWidget=Widget;bWidgetPending=true;}
// 未启动的实例也可以先读取并修改默认配置。
FDesktopPetConfig ADesktopPetActor::GetDefaultRuntimeConfig(){return GetDefault<UDesktopPetSettings>()->MakeRuntimeConfig();}
FDesktopPetConfig ADesktopPetActor::GetRuntimeConfig() const
{
    FDesktopPetConfig Result=bHasConfig?DesiredConfig:(bOverrideDefaultConfig?InitialConfig:GetDefaultRuntimeConfig());
    if(Runtime&&!bConfigPending)Result.WindowPosition=GetWindowPosition();
    return Result;
}
// 任何字段都可通过整组配置运行时修改；配置只属于本实例。
void ADesktopPetActor::ApplyRuntimeConfig(const FDesktopPetConfig& Config)
{
    DesiredConfig=Config;DesiredConfig.Normalize();bHasConfig=true;bConfigPending=true;
}
// 尺寸、位置和质量的快捷节点最终仍调用同一个 ApplyRuntimeConfig。
void ADesktopPetActor::SetDisplaySize(FIntPoint Size)
{
    auto C=GetRuntimeConfig();
    C.WindowSize=FIntPoint(FMath::RoundToInt(Size.X/C.DisplayScale),FMath::RoundToInt(Size.Y/C.DisplayScale));
    ApplyRuntimeConfig(C);
}
FIntPoint ADesktopPetActor::GetDisplaySize() const{return GetRuntimeConfig().GetDisplaySize();}
// 读真实 HWND 坐标，使拖拽之后再改质量不会把窗口跳回初始位置。
FIntPoint ADesktopPetActor::GetWindowPosition() const
{
    if(!Runtime)return GetRuntimeConfig().WindowPosition;
    FPetDpiScope Scope;RECT R{};GetWindowRect(Runtime->Window,&R);return FIntPoint(R.left,R.top);
}
// 手动拖拽接口可直接连接 Enhanced Input 的 Started / Completed。
bool ADesktopPetActor::BeginWindowDrag(){return Runtime&&Runtime->BeginDrag();}
void ADesktopPetActor::EndWindowDrag(){if(Runtime)Runtime->FinishDrag();}
bool ADesktopPetActor::IsWindowDragging() const{return Runtime&&Runtime->Dragging;}
void ADesktopPetActor::SetInteractionTiming(float Delay,float Seconds)
{
    auto C=GetRuntimeConfig();C.MenuCloseDelay=Delay;C.MenuAnimationSeconds=Seconds;ApplyRuntimeConfig(C);
}
// 动作总线不硬编码退出、聊天、换动作等项目行为。
void ADesktopPetActor::DispatchAction(FName Action)
{
    if(Runtime){++Runtime->UIClicks;Runtime->LastAction=Action.ToString();}
    OnAction.Broadcast(Action);
}
// 诊断函数保留原来的调用方式。
void ADesktopPetActor::SaveDiagnosticFrame(){if(Runtime)Runtime->Report(true);}
float ADesktopPetActor::GetInteractionProgress() const{return Runtime?Runtime->OpenAmount:0.f;}
int64 ADesktopPetActor::GetPresentedFrameCount() const{return Runtime?Runtime->Frames:0;}
void* ADesktopPetActor::GetNativeWindowHandle() const{return Runtime?Runtime->Window:nullptr;}
uint8 ADesktopPetActor::GetSceneAlphaAt(FIntPoint P) const
{
    if(!Runtime||P.X<0||P.Y<0||P.X>=Runtime->Width||P.Y>=Runtime->Height)return 0;
    return Runtime->SceneAlpha[P.Y*Runtime->Width+P.X];
}
// 最终输出包含 UI；不能只用场景 Alpha 判断一个桌面像素是否透明。
uint8 ADesktopPetActor::GetCompositedAlphaAt(FIntPoint P) const
{
    if(!Runtime||P.X<0||P.Y<0||P.X>=Runtime->Width||P.Y>=Runtime->Height)return 0;
    return Runtime->Pixels[P.Y*Runtime->Width+P.X].A;
}
// 只供项目的 Development 自动验收，不影响发布版真实鼠标输入。
void ADesktopPetActor::DebugSetPointerOverride(TOptional<FVector2D> P)
{
#if !UE_BUILD_SHIPPING
    if(Runtime)Runtime->PointerOverride=P;
#endif
}
// 所有资源更改统一在安全的 Tick 边界处理，随后按限频捕获并输出。
void ADesktopPetActor::Tick(float Delta)
{
    Super::Tick(Delta);
    if(bRestartRequested){bRestartRequested=false;StopDesktopWindow();StartDesktopWindow();}
    if(!Runtime)return;
    if(Runtime->CloseRequested)
    {
        const bool Exit=Runtime->ExitOnClose&&!GIsEditor;
        if(Exit)FPlatformMisc::RequestExit(false);
        StopDesktopWindow();return;
    }
    if(bConfigPending)
    {
        bConfigPending=false;Runtime->ApplyConfig(DesiredConfig);
        OnRuntimeConfigApplied.Broadcast(DesiredConfig);
    }
    if(bWidgetPending){bWidgetPending=false;Runtime->SetWidget();}
    Runtime->PollInput(Delta);Runtime->CaptureFrame(Delta);
    if(Runtime->Diagnostics&&FPlatformTime::Seconds()-Runtime->LastReport>2)
    {
        Runtime->LastReport=FPlatformTime::Seconds();Runtime->Report(true);
    }
}

// 运行时更新 WindowSize；保留其他实例参数。
void ADesktopPetActor::SetWindowSize(FIntPoint Value)
{
    auto C=GetRuntimeConfig();C.WindowSize=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 WindowPosition；保留其他实例参数。
void ADesktopPetActor::SetWindowPosition(FIntPoint Value)
{
    auto C=GetRuntimeConfig();C.WindowPosition=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 DisplayScale；保留其他实例参数。
void ADesktopPetActor::SetDisplayScale(float Value)
{
    auto C=GetRuntimeConfig();C.DisplayScale=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 bAlwaysOnTop；保留其他实例参数。
void ADesktopPetActor::SetAlwaysOnTop(bool Value)
{
    auto C=GetRuntimeConfig();C.bAlwaysOnTop=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 bHideGameWindow；保留其他实例参数。
void ADesktopPetActor::SetHideGameWindow(bool Value)
{
    auto C=GetRuntimeConfig();C.bHideGameWindow=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 bDraggable；保留其他实例参数。
void ADesktopPetActor::SetDraggable(bool Value)
{
    auto C=GetRuntimeConfig();C.bDraggable=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 SupersampleScale；保留其他实例参数。
void ADesktopPetActor::SetSupersampleScale(int32 Value)
{
    auto C=GetRuntimeConfig();C.SupersampleScale=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 TargetFrameRate；保留其他实例参数。
void ADesktopPetActor::SetTargetFrameRate(int32 Value)
{
    auto C=GetRuntimeConfig();C.TargetFrameRate=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 Exposure；保留其他实例参数。
void ADesktopPetActor::SetExposure(float Value)
{
    auto C=GetRuntimeConfig();C.Exposure=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 bClickThroughTransparentPixels；保留其他实例参数。
void ADesktopPetActor::SetClickThroughTransparentPixels(bool Value)
{
    auto C=GetRuntimeConfig();C.bClickThroughTransparentPixels=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 HitAlphaThreshold；保留其他实例参数。
void ADesktopPetActor::SetHitAlphaThreshold(int32 Value)
{
    auto C=GetRuntimeConfig();C.HitAlphaThreshold=Value;ApplyRuntimeConfig(C);
}

// 运行时更新 bWriteDiagnostics；保留其他实例参数。
void ADesktopPetActor::SetWriteDiagnostics(bool Value)
{
    auto C=GetRuntimeConfig();C.bWriteDiagnostics=Value;ApplyRuntimeConfig(C);
}
