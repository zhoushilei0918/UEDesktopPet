#pragma once
#include "DesktopPetActor.h"
#include "DesktopPetMemory.h"
#include "DesktopPetFramePacing.h"
#include "DesktopPetShell.h"
#include "DesktopPetCursor.h"
#include "DesktopPetCursorSurface.h"
#include "DesktopPetApplicationName.h"
#include "DesktopPetRenderPolicy.h"
#include "DesktopPetSettings.h"
#include "DesktopPetViewExtension.h"
#include "Widgets/SOverlay.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Animation/UMGSequenceTickManager.h"
#include "DesktopPetCompositor.h"
#include "DesktopPetGPUCompositor.h"
#include "DesktopPetPresentation.h"
#include "DesktopPetWindowGuard.h"
#include "Widgets/SNullWidget.h"
#include "Components/SceneCaptureComponent2D.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Slate/WidgetRenderer.h"
#include "Widgets/SVirtualWindow.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/HittestGrid.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "RenderResource.h"
// 显式引用渲染目标资源定义，确保独立插件构建不依赖项目预编译头。
#include "TextureResource.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"
#include "Windows/WindowsHWrapper.h"
#include <windowsx.h>

DEFINE_LOG_CATEGORY_STATIC(LogDesktopPet, Log, All);

// 临时使用物理像素坐标，防止 UE 游戏窗口的 DPI 策略影响桌宠与鼠标的对齐。
struct FPetDpiScope
{
    DPI_AWARENESS_CONTEXT Previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~FPetDpiScope()
    {
        if (Previous)
        {
            SetThreadDpiAwarenessContext(Previous);
        }
    }
};

/** 一次异步 GPU 回读的生命周期，渲染线程写入数据，游戏线程等待 Complete 后消费。 */
struct FPetReadback
{
    /** 只读回窗口分辨率的颜色和场景命中 Alpha，无需传输完整 SSAA 图像。 */
    FRHIGPUBufferReadback Packed{TEXT("DesktopPetPacked")};
    /** 在渲染线程直接复制紧密排列的颜色/Alpha 平面，游戏线程交换数组。 */
    TArray<FColor> OutputPixels;
    TArray<uint8> OutputAlpha;
    FDesktopPetConfig FrameConfig;
    bool UseGPU = false;
    FRHIGPUTextureReadback UI{TEXT("DesktopPetUI")};
    /** 与覆盖率同一帧的引擎最终颜色，单独读回而不牺牲 Alpha。 */
    FRHIGPUTextureReadback FinalColor{TEXT("DesktopPetFinalColor")};
    TArray<FFloat16Color> FinalColorPixels;
    FRHIGPUTextureReadback Geometry{TEXT("DesktopPetCoverage")};
    TArray<FFloat16> OpacityPixels;
    TAtomic<bool> Submitted{false}, Copying{false}, Complete{false}, Failed{false};
    TArray<FColor> UIPixels;
    int32 SceneWidth = 0, SceneHeight = 0, Width = 0, Height = 0;
    bool HasUI = false;
};

/** 输入隔离独立于桌宠 HWND 的生命周期；停止再启动显示时仍保留主视口原始策略。 仅影响打包游戏，编辑器中的关卡编辑视口保持原有操作方式。 */
inline void ApplyPetGameViewportPolicy(bool Hidden)
{
    if (GIsEditor || !GEngine || !GEngine->GameViewport)
    {
        return;
    }
    static TWeakObjectPtr<UGameViewportClient> SavedViewport;
    static bool SavedIgnore = false, SavedDisableWorldRendering = false;
    static EMouseCaptureMode SavedCapture = EMouseCaptureMode::NoCapture;
    static EMouseLockMode SavedLock = EMouseLockMode::DoNotLock;
    UGameViewportClient* Viewport = GEngine->GameViewport;
    if (Hidden)
    {
        if (SavedViewport.Get() != Viewport)
        {
            SavedViewport = Viewport;
            SavedIgnore = Viewport->IgnoreInput();
            SavedDisableWorldRendering = Viewport->bDisableWorldRendering;
            SavedCapture = Viewport->GetMouseCaptureMode();
            SavedLock = Viewport->GetMouseLockMode();
        }
        // 隐藏窗口不再重复渲染主场景；SceneCapture 仍独立绘制，不降低桌宠画质。
        Viewport->bDisableWorldRendering = true;
        Viewport->SetIgnoreInput(true);
        Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
        Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
    }
    else if (SavedViewport.Get() == Viewport)
    {
        Viewport->bDisableWorldRendering = SavedDisableWorldRendering;
        Viewport->SetIgnoreInput(SavedIgnore);
        Viewport->SetMouseCaptureMode(SavedCapture);
        Viewport->SetMouseLockMode(SavedLock);
        SavedViewport.Reset();
    }
}

/** 一个显示实例的运行时资源；只关联通用宿主，不依赖任何项目角色、菜单或特效类。 */
struct FDesktopPetRuntime
{
public:
    /* 窗口创建、配置应用与资源释放 */
    /** 回调可能关闭窗口，后续工作必须确认该运行时仍属于宿主。 */
    bool IsActive() const;
    /** 创建窗口前先复制实例配置，不读取或修改项目全局默认对象。 */
    bool Create(const FDesktopPetConfig& InConfig);
    /** 先等待渲染线程再释放 DIB、虚拟窗口和读回对象，防止退出时资源悬空。 */
    ~FDesktopPetRuntime();
    /** 缓存配置标量，避免每个像素重复查 UObject 设置。 */
    void ReadConfig();
    /** 任务栏与捕获许可独立；旧枚举 ToolWindow 只表示隐藏任务栏按钮。 */
    void ApplyWindowMode();
    /** 绘制目标尺寸改变时等待旧 GPU 请求结束，避免新旧尺寸混用；HWND 与 Widget 都保持不变。 */
    void ApplyConfig(const FDesktopPetConfig& NewConfig);
    /* 原生消息、人物拾取与指针事件 */
    /** 通用指针数据保留给项目自己的输入系统，插件不再注入 Enhanced Input。 */
    void EmitPointer(EDesktopPetPointerEvent Type, FKey Key = FKey(), float Wheel = 0);
    /** 手动拖拽 API 不要求鼠标位于人物上，可用于项目自定义拖拽把手或增强输入。 */
    bool BeginDrag();
    /** 无论主动结束还是 Windows 捕获丢失，都发出一次结束通知。 */
    void FinishDrag();
    /** 原生窗口消息统一转换为 Slate 事件和项目指针事件。 */
    static LRESULT CALLBACK WindowProc(HWND InWindow, UINT InMessage, WPARAM InWParam, LPARAM InLParam);
    /** Slate 命中网格属于最近一次 UI 绘制；显示层缩放时将坐标映射回该网格。 */
    FVector2D ToUI(FVector2D P) const;
    /** 空白处、装饰 Widget 和真实可交互控件仍使用同一个命中路径。 */
    FWidgetPath UIPathAt(FVector2D Position) const;
    /** 当前指针与外部蓝图查询使用相同的物理坐标系。 */
    FWidgetPath UIPath() const;
    /** 装饰文字不会抢占桌面输入，可交互的 Slate 控件优先于场景拖拽。 */
    bool HitsUI(const FWidgetPath& Path) const;
    /** 为离屏虚拟 Slate 用户构造事件，避免依赖 UE 游戏窗口的焦点。 */
    FPointerEvent Pointer(FKey Key = FKey()) const;
    /** 命中基于最终合成前的场景 Alpha，也适用于半透明模型及粒子。 */
    bool HitsScene() const;
    /** 可见像素与组件包围盒联合拾取；可见名单自动获得交互权，不依赖查询碰撞。 */
    AActor* PickActor(FVector2D Position) const;
    /** 两种鼠标键统一判定；Actor 点击定义为按下，左键才参与默认窗口拖拽。 */
    void PointerDown(FKey Key = EKeys::LeftMouseButton);
    /** 按钮在 Slate 中正常触发 OnClicked，并释放原生鼠标捕获。 */
    void PointerUp();
    /** 系统夺走捕获、主动停止显示时统一释放，避免增强输入动作残留在 Triggered。 */
    void ReleaseHeldInput();
    /** 使用屏幕坐标增量环绕，按显示缩放归一化，不移动或锁定用户系统光标。 */
    void MoveOrbit(FVector2D Delta);
    /** 光标覆盖不能改变原窗口零 Alpha 的穿透语义；拖拽/已按下的输入仍完整交还。 */
    bool PassThroughBasePixel(FIntPoint Point) const;
    /** 每帧检测指针；完全穿透时 HWND 收不到移动消息，仍需轮询系统光标。 */
    void PollInput(float Delta);
    /* 窗口呈现、光标叠加与平滑缩放 */
    /** 光标使用物理像素与系统 DPI，人物缩放不会把光标一起放大。 */
    bool GetCursorFrame(FIntPoint Position, FDesktopPetCursorFrame& Frame, FIntPoint& Origin) const;
    /** 比较可见性、位置、尺寸和图像版本，跳过没有变化的光标提交。 */
    bool CursorChanged(bool Visible, const FDesktopPetCursorFrame& Frame, FIntPoint Origin) const;
    /** 一次系统提交同时更新图像、尺寸和位置，禁止先移动 HWND 再等待 GPU 新图。 */
    bool Present(FIntPoint Size, FIntPoint Position);
    /** 新滚轮手势只从真实显示像素采样锚点；UI、透明区域、拖拽时均不接受缩放。 */
    bool ZoomAtScreenPosition(float Delta, FVector2D Screen);
    /** 停止手势后仅重建一次捕获尺寸，缩放过程不反复分配 RT 或清空有效命中 Alpha。 */
    void FinishZoom();
    /** 动画按真实时间推进，与场景帧率、慢动作无关；屏幕锚点始终为 Screen+半个像素。 */
    void UpdateZoom();
    /** 没有尺寸变化也要呈现角色/UMG 的新动画帧；托盘中不提交隐藏窗口。 */
    void PresentLatestFrame();
    /* 限频捕获、GPU 回读与像素合成 */
    /** 把纯像素算法与原生窗口操作分离；合成器不认识项目中的角色或 Widget。 */
    void Composite(FPetReadback& F);
    /** 模型与 UI 分开绘制；兼容 DX11/DX12，并用固定容量隔离回读延迟。 */
    void CaptureFrame(float Delta);
    /* UMG 图层与动画时钟 */
    void MarkPetWidgetAnimationTicks(float);
    /** 接收项目 Widget，空指针对应完全透明的空 UI 层。 */
    void SetWidget();

private:
    friend class ADesktopPetActor;

    /* 实例状态保持原有声明与初始化顺序。 */

    ADesktopPetActor* Owner = nullptr;
    HWND Window = nullptr;
    FDesktopPetShell Shell;
    /** 项目 Cursor 只在桌宠实际接收鼠标的区域生效。 */
    FDesktopPetCursor NativeCursor;
    /** 光标只覆盖现有窗口的小矩形，最后绘制且不参与人物/UI 命中。 */
    FDesktopPetCursorSurface CursorSurface;
    bool CursorPresented = false;
    FIntPoint CursorOrigin = FIntPoint::ZeroValue, CursorSize = FIntPoint::ZeroValue;
    uint64 CursorRevision = 0;
    double LastCursorPresentation = 0.;
    FDelegateHandle PetCursorPostTickHandle;
    bool RestoreRequested = false;
    FDesktopPetConfig Config;
    HDC MemoryDC = nullptr;
    HBITMAP Bitmap = nullptr;
    HGDIOBJ OldBitmap = nullptr;
    void* Bits = nullptr;
    int32 Width = 600, Height = 760, Scale = 2, FrameRate = 30, Threshold = 8;
    float Exposure = 1, CloseDelay = .4f, AnimationSeconds = .18f;
    bool Topmost = true, ClickThrough = true, Draggable = true, ExitOnClose = true;
    bool CloseRequested = false, Dragging = false, OrbitDragging = false, UIPressed = false, Hovered = false,
         MenuOpen = false;
    bool WasInputTransparent = false;
    POINT DragOffset{};
    FVector2D Cursor = FVector2D::ZeroVector, LastCursor = FVector2D::ZeroVector;
    double LastCapture = 0, NextCaptureTime = 0, LastHover = 0;
    float OpenAmount = 0;

    bool HasUI = false;

    // Width/Height 是捕获尺寸，DisplaySize 是已经提交给 Windows 的真实尺寸。
    // 两者在平滑缩放期间可以不同；GPU 完成新帧之前继续使用上一帧有效图像。
    FIntPoint DisplaySize = FIntPoint(600, 760), BitmapSize = FIntPoint::ZeroValue, CapturedSize = FIntPoint::ZeroValue,
              UIHitSize = FIntPoint(600, 760);
    float PresentedScale = 1.f, ZoomStartScale = 1.f, ZoomTargetScale = 1.f;
    bool Zooming = false, ZoomCommitPending = false, ZoomAnchorValid = false;
    double ZoomStarted = 0., LastZoomPresentation = 0.;
    FVector2D ZoomScreen = FVector2D::ZeroVector, ZoomUV = FVector2D::ZeroVector;

    TArray<FColor> CapturedPixels, PetScratch;
    bool bFrameNeedsPresentation = false;
    TArray<uint8> CapturedAlpha;
    TArray<uint8> SceneAlpha;
    /** 最多两个在途请求，忙时跳过过期节拍，禁止旋转或多个特效把队列无限堆积。 */
    static constexpr int32 MaxReadbacks = 2;
    TArray<TSharedPtr<FPetReadback, ESPMode::ThreadSafe>> PendingReadbacks, AvailableReadbacks;

    TUniquePtr<FWidgetRenderer> Renderer;
    /** 离屏捕获可以限帧，但 UMG 时间轴仍由 Slate 的真实帧时钟推进。 */
    FDelegateHandle PetAnimationPreTickHandle;

    TSharedPtr<SVirtualWindow> VirtualWindow;
    TSharedPtr<FSlateVirtualUserHandle> VirtualUser;
    TSet<FKey> Pressed;
    /** 原生按键保持与 Slate UI 按键分开，普通输入动作也能在移出窗口后收到抬起。 */
    TSet<FKey> NativePressed;
};
