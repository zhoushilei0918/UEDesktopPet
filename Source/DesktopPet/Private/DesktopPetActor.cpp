#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
#include "DesktopPetMemory.h"
#include "DesktopPetAudio.h"
#include "DesktopPetRuntime.h"

// 通用宿主不预设人物镜头；项目通过 Capture 组件自行布置。
ADesktopPetActor::ADesktopPetActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Capture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("Capture"));
    Capture->SetupAttachment(RootComponent);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
}

ADesktopPetActor::~ADesktopPetActor() = default;

/** 项目也可以关闭自动启动，先创建自己的 Widget / 角色，再手动 Start。 */
void ADesktopPetActor::BeginPlay()
{
    TryAutoLoadSavedRuntimeConfig();
    Super::BeginPlay();
    if (bAutoStart)
    {
        PetStartDesktopWindow();
    }
}

/** 切图、停止 PIE、销毁 Actor 都走同一套资源回收。 */
void ADesktopPetActor::EndPlay(const EEndPlayReason::Type Reason)
{
    PetStopDesktopWindow();
    Super::EndPlay(Reason);
}

/** 启动只建立捕获、UI 绘制器和透明窗口，不创建项目角色或菜单。 */
bool ADesktopPetActor::PetStartDesktopWindow()
{
    if (Runtime || IsRunningCommandlet() || !FSlateApplication::IsInitialized())
    {
        return false;
    }
    TryAutoLoadSavedRuntimeConfig();
    if (!bHasConfig)
    {
        DesiredConfig = bOverrideDefaultConfig ? InitialConfig : PetGetDefaultRuntimeConfig();
    }

    DesiredConfig.Normalize();
    bHasConfig = true;
    InitialCaptureTransform = Capture->GetComponentTransform();
    InitialOrbitRotation = OrbitRotation;
    InitialOrbitDistance = OrbitDistance;
    UpdateOrbitCamera();
    if (!OverlayWidget)
    {
        UClass* Class = WidgetOverride.Get() ? WidgetOverride.Get() :
                                               GetDefault<UDesktopPetSettings>()->WidgetClass.LoadSynchronous();
        if (Class)
        {
            OverlayWidget = CreateWidget<UUserWidget>(GetWorld(), Class);
        }
    }
    Runtime = MakeShared<FDesktopPetRuntime>();
    Runtime->Owner = this;
    if (!Runtime->Create(DesiredConfig))
    {
        Runtime.Reset();
        return false;
    }
    FDesktopPetMemory::Update(this, DesiredConfig);
    FDesktopPetAudio::Update(this, DesiredConfig);
    FDesktopPetFramePacing::Update(this, DesiredConfig, Runtime->Shell.IsInTray());
    SceneTarget = NewObject<UTextureRenderTarget2D>(this);
    SceneTarget->ClearColor = FLinearColor(0, 0, 0, 1);
    SceneTarget->InitCustomFormat(
        Runtime->Width * Runtime->Scale, Runtime->Height * Runtime->Scale, PF_FloatRGBA, true);
    SceneTarget->UpdateResourceImmediate(true);
    FinalColorTarget = NewObject<UTextureRenderTarget2D>(this);
    FinalColorTarget->ClearColor = FLinearColor::Transparent;
    FinalColorTarget->InitCustomFormat(
        Runtime->Width * Runtime->Scale, Runtime->Height * Runtime->Scale, PF_FloatRGBA, true);
    FinalColorTarget->UpdateResourceImmediate(true);
    GeometryTarget = NewObject<UTextureRenderTarget2D>(this);
    GeometryTarget->ClearColor = FLinearColor::White;
    GeometryTarget->bCanCreateUAV = true;
    GeometryTarget->InitCustomFormat(Runtime->Width * Runtime->Scale, Runtime->Height * Runtime->Scale, PF_R16F, true);
    GeometryTarget->UpdateResourceImmediate(true);
    ViewExtension = FSceneViewExtensions::NewExtension<FDesktopPetViewExtension>(
        FinalColorTarget->GameThread_GetRenderTargetResource(),
        GeometryTarget->GameThread_GetRenderTargetResource(),
        SceneTarget->GameThread_GetRenderTargetResource(),
        this);
    OpacityCapture = NewObject<USceneCaptureComponent2D>(this, NAME_None, RF_Transient);
    OpacityCapture->bCaptureEveryFrame = false;
    OpacityCapture->bCaptureOnMovement = false;
    OpacityCapture->RegisterComponent();
    UITarget = NewObject<UTextureRenderTarget2D>(this);
    UITarget->ClearColor = FLinearColor::Transparent;
    // Slate 已执行显示用的 Gamma 转换；线性 RT 禁止硬件再次 sRGB 编码，保留正确的颜色和预乘透明度。
    UITarget->InitCustomFormat(Runtime->Width, Runtime->Height, PF_B8G8R8A8, true);
    UITarget->UpdateResourceImmediate(true);
    ConfigureCapturePipeline();
    PetRefreshVisibleActors();
    bConfigPending = false;
    bWidgetPending = false;
    PetRuntimeConfigApplied.Broadcast(DesiredConfig);
    return true;
}

/** 项目的 Widget 实例保留，便于停止/启动显示时保留菜单状态。 */
void ADesktopPetActor::PetStopDesktopWindow()
{
    if (bStopping)
    {
        return;
    }
    bStopping = true;
    PetCompleteConfigWidgetClose();
    UpdateHoveredActor(nullptr);
    if (Runtime)
    {
        Runtime->ReleaseHeldInput();
    }
    Runtime.Reset();
    ViewExtension.Reset();
    if (Capture)
    {
        Capture->TextureTarget = nullptr;
    }
    if (OpacityCapture)
    {
        OpacityCapture->TextureTarget = nullptr;
        OpacityCapture->DestroyComponent();
        OpacityCapture = nullptr;
    }
    SceneTarget = nullptr;
    FinalColorTarget = nullptr;
    GeometryTarget = nullptr;
    UITarget = nullptr;
    FDesktopPetMemory::Release(this);
    FDesktopPetAudio::Release(this);
    FDesktopPetFramePacing::Release(this);
    bStopping = false;
}

/** 仅显式重启才替换 HWND；尺寸和 SSAA 的运行时更改不走这里。 */
void ADesktopPetActor::PetRestartDesktopWindow()
{
    bRestartRequested = true;
}

/** 窗口关闭策略延后到 Tick 执行，允许从 UMG 按钮回调调用。 */
void ADesktopPetActor::PetRequestClose()
{
    if (Runtime)
    {
        Runtime->CloseRequested = true;
    }
}

bool ADesktopPetActor::PetIsDesktopWindowRunning() const
{
    return Runtime.IsValid();
}

/** 所有资源更改统一在安全的 Tick 边界处理，随后按限频捕获并输出。 */
void ADesktopPetActor::Tick(float Delta)
{
    Super::Tick(Delta);
    if (bRestartRequested)
    {
        bRestartRequested = false;
        PetStopDesktopWindow();
        PetStartDesktopWindow();
    }
    if (!Runtime)
    {
        return;
    }
    // 蓝图事件允许停止显示；保留当前调用栈资源，并在回调后检查是否已更换运行时。
    const TSharedPtr<FDesktopPetRuntime> FrameRuntime = Runtime;
    if (Runtime->CloseRequested)
    {
        const bool Exit = Runtime->ExitOnClose && !GIsEditor;
        if (Exit)
        {
            FPlatformMisc::RequestExit(false);
        }
        PetStopDesktopWindow();
        return;
    }
    if (FrameRuntime->RestoreRequested)
    {
        FrameRuntime->RestoreRequested = false;
        PetRestoreFromTray();
    }
    if (Runtime != FrameRuntime)
    {
        return;
    }
    if (bConfigPending)
    {
        bConfigPending = false;
        FDesktopPetMemory::Update(this, DesiredConfig);
        FDesktopPetAudio::Update(this, DesiredConfig);
        FDesktopPetFramePacing::Update(this, DesiredConfig, Runtime->Shell.IsInTray());
        Runtime->ApplyConfig(DesiredConfig);
        PetRuntimeConfigApplied.Broadcast(DesiredConfig);
    }
    if (Runtime != FrameRuntime)
    {
        return;
    }
    // 编辑器会按自己的偏好重设全局音量；在世界音频更新前应用临时后台策略，不修改编辑器偏好。
    FDesktopPetAudio::Tick();
    UpdateOrbitCamera();
    if (bWidgetPending)
    {
        bWidgetPending = false;
        Runtime->SetWidget();
    }
    if (!FrameRuntime->Shell.IsInTray())
    {
        FrameRuntime->PollInput(Delta);
    }
    if (Runtime != FrameRuntime)
    {
        return;
    }
    FrameRuntime->CaptureFrame(Delta);
    if (!FrameRuntime->Shell.IsInTray())
    {
        // 先收取新图再推进显示动画，两者最终只进行一次窗口位图提交。
        FrameRuntime->UpdateZoom();
        FrameRuntime->PresentLatestFrame();
    }
}
