#pragma once
#include "HAL/IConsoleManager.h"
#include "DesktopPetActor.h"
#include "DesktopPetMemory.h"
#include "DesktopPetFramePacing.h"
#include "DesktopPetShell.h"
#include "DesktopPetApplicationName.h"
#include "DesktopPetRenderPolicy.h"
#include "DesktopPetSettings.h"
#include "DesktopPetViewExtension.h"
#include "Widgets/SOverlay.h"
#include "Blueprint/UserWidget.h"
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
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Async/ParallelFor.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Windows/WindowsHWrapper.h"
#include <windowsx.h>

DEFINE_LOG_CATEGORY_STATIC(LogDesktopPet, Log, All);

// 临时使用物理像素坐标，防止 UE 游戏窗口的 DPI 策略影响桌宠与鼠标的对齐。
struct FPetDpiScope
{
    DPI_AWARENESS_CONTEXT Previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ~FPetDpiScope(){if(Previous)SetThreadDpiAwarenessContext(Previous);}
};

/** 一次异步 GPU 回读的生命周期，渲染线程写入数据，游戏线程等待 Complete 后消费。 */
struct FPetReadback
{
    /** 只读回窗口分辨率的颜色和场景命中 Alpha，无需传输完整 SSAA 图像。 */
    FRHIGPUBufferReadback Packed{TEXT("DesktopPetPacked")};
    TArray<DesktopPetGPUCompositor::FPackedPixel> PackedPixels;
    FDesktopPetConfig FrameConfig;
    bool UseGPU=false;
    FRHIGPUTextureReadback UI{TEXT("DesktopPetUI")};
    /** 与覆盖率同一帧的引擎最终颜色，单独读回而不牺牲 Alpha。 */
    FRHIGPUTextureReadback FinalColor{TEXT("DesktopPetFinalColor")};
    TArray<FFloat16Color> FinalColorPixels;
    FRHIGPUTextureReadback Geometry{TEXT("DesktopPetCoverage")};
    TArray<FFloat16> OpacityPixels;
    TAtomic<bool> Submitted{false}, Copying{false}, Complete{false}, Failed{false};
    TArray<FColor> UIPixels;
    int32 SceneWidth=0,SceneHeight=0,Width=0,Height=0;
    bool HasUI=false;
};

// 输入隔离独立于桌宠 HWND 的生命周期；停止再启动显示时仍保留主视口原始策略。
// 仅影响打包游戏，编辑器中的关卡编辑视口保持原有操作方式。
inline void ApplyPetGameViewportPolicy(bool Hidden)
{
    if(GIsEditor||!GEngine||!GEngine->GameViewport)return;
    static TWeakObjectPtr<UGameViewportClient> SavedViewport;
    static bool SavedIgnore=false,SavedDisableWorldRendering=false;
    static EMouseCaptureMode SavedCapture=EMouseCaptureMode::NoCapture;
    static EMouseLockMode SavedLock=EMouseLockMode::DoNotLock;
    UGameViewportClient* Viewport=GEngine->GameViewport;
    if(Hidden)
    {
        if(SavedViewport.Get()!=Viewport)
        {
            SavedViewport=Viewport;SavedIgnore=Viewport->IgnoreInput();SavedDisableWorldRendering=Viewport->bDisableWorldRendering;
            SavedCapture=Viewport->GetMouseCaptureMode();SavedLock=Viewport->GetMouseLockMode();
        }
        // 隐藏窗口不再重复渲染主场景；SceneCapture 仍独立绘制，不降低桌宠画质。
        Viewport->bDisableWorldRendering=true;
        Viewport->SetIgnoreInput(true);
        Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
        Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
    }
    else if(SavedViewport.Get()==Viewport)
    {
        Viewport->bDisableWorldRendering=SavedDisableWorldRendering;Viewport->SetIgnoreInput(SavedIgnore);Viewport->SetMouseCaptureMode(SavedCapture);
        Viewport->SetMouseLockMode(SavedLock);SavedViewport.Reset();
    }
}

/** 一个显示实例的运行时资源；只关联通用宿主，不依赖任何项目角色、菜单或特效类。 */
struct FDesktopPetRuntime
{
    ADesktopPetActor* Owner=nullptr;
    HWND Window=nullptr;
    FDesktopPetShell Shell;
    bool RestoreRequested=false;
    FDesktopPetConfig Config;
    HDC MemoryDC=nullptr;
    HBITMAP Bitmap=nullptr;
    HGDIOBJ OldBitmap=nullptr;
    void* Bits=nullptr;
    int32 Width=600,Height=760,Scale=2,FrameRate=30,Threshold=8;
    float Exposure=1,CloseDelay=.4f,AnimationSeconds=.18f;
    bool Topmost=true,ClickThrough=true,Draggable=true,ExitOnClose=true,Diagnostics=false;
    bool CloseRequested=false,Dragging=false,OrbitDragging=false,UIPressed=false,Hovered=false,MenuOpen=false;
    bool WasInputTransparent=false;
    POINT DragOffset{};
    FVector2D Cursor=FVector2D::ZeroVector,LastCursor=FVector2D::ZeroVector;
    double LastCapture=0,NextCaptureTime=0,LastHover=0,LastReport=0,StartTime=0;
    float OpenAmount=0;
    uint64 Frames=0,PetClicks=0,UIClicks=0;
    // 诊断记录合并后的覆盖率与读回开销，仅在显式开启诊断时统计。
    int32 CoverageSamples=0;
    uint64 ReadbackPayloadBytes=0,ReadbackAllocations=0;
    double CompositeMilliseconds=0;
    bool LastFrameUsedGPU=false;
    bool HasUI=false;
    FString Backend,LastAction;
    // Width/Height 是捕获尺寸，DisplaySize 是已经提交给 Windows 的真实尺寸。
    // 两者在平滑缩放期间可以不同；GPU 完成新帧之前继续使用上一帧有效图像。
    FIntPoint DisplaySize=FIntPoint(600,760),BitmapSize=FIntPoint::ZeroValue,CapturedSize=FIntPoint::ZeroValue,UIHitSize=FIntPoint(600,760);
    float PresentedScale=1.f,ZoomStartScale=1.f,ZoomTargetScale=1.f;
    bool Zooming=false,ZoomCommitPending=false,ZoomAnchorValid=false;
    double ZoomStarted=0.,LastZoomPresentation=0.;
    FVector2D ZoomScreen=FVector2D::ZeroVector,ZoomUV=FVector2D::ZeroVector;
    uint64 Presentations=0;
    TArray<FColor> CapturedPixels,Pixels,PetScratch;
    TArray<uint8> CapturedAlpha;
    TArray<uint8> SceneAlpha;
    TSharedPtr<FPetReadback,ESPMode::ThreadSafe> Pending,ReusableReadback;
    TUniquePtr<FWidgetRenderer> Renderer;
    TSharedPtr<SVirtualWindow> VirtualWindow;
    TSharedPtr<FSlateVirtualUserHandle> VirtualUser;
    TSet<FKey> Pressed;
    /** 原生按键保持与 Slate UI 按键分开，普通输入动作也能在移出窗口后收到抬起。 */
    TSet<FKey> NativePressed;

    // 回调可能关闭窗口，后续工作必须确认该运行时仍属于宿主。
    bool IsActive() const {return Owner&&!Owner->bStopping&&Owner->Runtime.Get()==this&&!Owner->IsActorBeingDestroyed();}
    // 通用指针数据保留给项目自己的输入系统，插件不再注入 Enhanced Input。
    void EmitPointer(EDesktopPetPointerEvent Type,FKey Key=FKey(),float Wheel=0)
    {
        if(!Owner->PetPointerInput.IsBound())return;
        FDesktopPetPointerEvent E;
        E.Type=Type;E.Key=Key;E.PixelPosition=Cursor;E.CanvasPosition=FVector2D(Cursor.X*Config.WindowSize.X/DisplaySize.X,Cursor.Y*Config.WindowSize.Y/DisplaySize.Y);
        E.Delta=Cursor-LastCursor;E.WheelDelta=Wheel;E.bOverUI=HitsUI(UIPath());E.bOverScene=HitsScene();
        E.HitActor=PickActor(Cursor);
        Owner->PetPointerInput.Broadcast(E);
    }
    // 缓存配置标量，避免每个像素重复查 UObject 设置。
    void ReadConfig()
    {
        const FIntPoint Size=Config.GetDisplaySize();
        Width=Size.X;Height=Size.Y;Scale=Config.SupersampleScale;FrameRate=Config.TargetFrameRate;
        Exposure=Config.Exposure;Threshold=Config.HitAlphaThreshold;
        CloseDelay=Config.MenuCloseDelay;AnimationSeconds=Config.MenuAnimationSeconds;
        Topmost=Config.bAlwaysOnTop;ClickThrough=Config.bClickThroughTransparentPixels;
        Draggable=Config.bDraggable;ExitOnClose=Config.bExitApplicationOnClose;
        Diagnostics=Config.bWriteDiagnostics;
    }
    // 任务栏与捕获许可独立；旧枚举 ToolWindow 只表示隐藏任务栏按钮。
    void ApplyWindowMode()
    {Shell.ApplyFlags(Config.WindowMode!=EDesktopPetWindowMode::ToolWindow,Config.bAllowScreenCapture);}
    // 接收项目 Widget，空指针对应完全透明的空 UI 层。
    void SetWidget()
    {
        HasUI=Owner->OverlayWidget||(Owner->bConfigWidgetOpen&&Owner->ConfigWidget);
        if(!HasUI)VirtualWindow->GetHittestGrid().Clear();
        TSharedRef<SOverlay> Layers=SNew(SOverlay);
        if(Owner->OverlayWidget)Layers->AddSlot()[Owner->OverlayWidget->TakeWidget()];
        if(Owner->bConfigWidgetOpen&&Owner->ConfigWidget)Layers->AddSlot()[Owner->ConfigWidget->TakeWidget()];
        VirtualWindow->SetContent(Layers);
    }
    // 绘制目标尺寸改变时等待旧 GPU 请求结束，避免新旧尺寸混用；HWND 与 Widget 都保持不变。
    void ApplyConfig(const FDesktopPetConfig& NewConfig)
    {
        FPetDpiScope DpiScope;
        const bool Resize=NewConfig.GetDisplaySize()!=Config.GetDisplaySize()||NewConfig.SupersampleScale!=Config.SupersampleScale;
        RECT ActualRect{};GetWindowRect(Window,&ActualRect);
        const bool Move=NewConfig.WindowPosition!=FIntPoint(ActualRect.left,ActualRect.top);
        const bool WindowModeChanged=NewConfig.WindowMode!=Config.WindowMode||NewConfig.bAllowScreenCapture!=Config.bAllowScreenCapture;
        const bool ModeChanged=NewConfig.bTransparentWindowEnabled!=Config.bTransparentWindowEnabled;
        const bool PreserveGesture=Zooming&&ZoomCommitPending;
        if(!ZoomCommitPending)ZoomAnchorValid=false;
        ZoomCommitPending=false;
        if(!PreserveGesture)Zooming=false;
        if(NewConfig.TargetFrameRate!=Config.TargetFrameRate||Resize)NextCaptureTime=0.;
        // 切换兼容路径时回收旧 staging 资源，避免保留两套回读缓存。
        if(NewConfig.bUseGPUCompositing!=Config.bUseGPUCompositing)
        {FlushRenderingCommands();Pending.Reset();ReusableReadback.Reset();}
        Config=NewConfig;
        ReadConfig();
        if(WindowModeChanged){ReleaseHeldInput();ApplyWindowMode();}
        if(!PreserveGesture)
        {
            PresentedScale=ZoomTargetScale=Config.DisplayScale;
            if(CapturedPixels.IsEmpty())DisplaySize=Config.GetDisplaySize();
            Present(Config.GetDisplaySize(),Config.WindowPosition);
        }
        if(!Draggable)FinishDrag();
        if(ModeChanged)
        {
            ReleaseHeldInput();Owner->PetCloseConfigWidget();
            // 调试窗口模式不能留在托盘里，否则普通游戏窗口和收起状态会相互矛盾。
            if(Shell.IsInTray())RestoreRequested=true;
            ShowWindow(Window,Config.bTransparentWindowEnabled&&!Shell.IsInTray()?SW_SHOWNOACTIVATE:SW_HIDE);
            ApplyWindowMode();
        }
        if(Resize)
        {
            FlushRenderingCommands();Pending.Reset();ReusableReadback.Reset();Owner->ViewExtension.Reset();
            Owner->SceneTarget->InitCustomFormat(Width*Scale,Height*Scale,PF_FloatRGBA,true);
            Owner->SceneTarget->UpdateResourceImmediate(true);
            Owner->FinalColorTarget->InitCustomFormat(Width*Scale,Height*Scale,PF_FloatRGBA,true);
            Owner->FinalColorTarget->UpdateResourceImmediate(true);
            Owner->GeometryTarget->InitCustomFormat(Width*Scale,Height*Scale,PF_R16F,true);
            Owner->GeometryTarget->UpdateResourceImmediate(true);
            Owner->ViewExtension=FSceneViewExtensions::NewExtension<FDesktopPetViewExtension>(Owner->FinalColorTarget->GameThread_GetRenderTargetResource(),Owner->GeometryTarget->GameThread_GetRenderTargetResource(),Owner->SceneTarget->GameThread_GetRenderTargetResource(),Owner);
            Owner->UITarget->InitCustomFormat(Width,Height,PF_B8G8R8A8,false);
            Owner->UITarget->UpdateResourceImmediate(true);
        }
        VirtualWindow->Resize(FVector2D(Config.WindowSize));
        Owner->ConfigureCapturePipeline();
        SetWindowPos(Window,Topmost?HWND_TOPMOST:HWND_NOTOPMOST,Config.WindowPosition.X,Config.WindowPosition.Y,
                     0,0,SWP_NOACTIVATE|SWP_NOSIZE|((Move&&!CapturedPixels.Num())?0:SWP_NOMOVE));
        if(Dragging){POINT P{};GetCursorPos(&P);ScreenToClient(Window,&P);DragOffset=P;}
        FDesktopPetWindowGuard::SetHidden(Config.bHideGameWindow&&Config.bTransparentWindowEnabled);
        ApplyPetGameViewportPolicy(Config.bHideGameWindow&&Config.bTransparentWindowEnabled);
    }
    // 手动拖拽 API 不要求鼠标位于人物上，可用于项目自定义拖拽把手或增强输入。
    bool BeginDrag()
    {
        if(Shell.IsInTray()||Owner->bConfigWidgetOpen||!Config.bTransparentWindowEnabled)return false;
        if(!Draggable||Dragging||OrbitDragging)return Dragging;
        FinishZoom();
        Dragging=true;DragOffset={static_cast<LONG>(Cursor.X),static_cast<LONG>(Cursor.Y)};
        if(GetCapture()!=Window)SetCapture(Window);
        Owner->PetDragStateChanged.Broadcast(true);return true;
    }
    // 无论主动结束还是 Windows 捕获丢失，都发出一次结束通知。
    void FinishDrag()
    {
        if(!Dragging)return;
        Dragging=false;
        if(NativePressed.IsEmpty()&&GetCapture()==Window)ReleaseCapture();
        Owner->PetDragStateChanged.Broadcast(false);
    }

    // 原生窗口消息统一转换为 Slate 事件和项目指针事件。
    static LRESULT CALLBACK WindowProc(HWND H,UINT M,WPARAM W,LPARAM L)
    {
        FDesktopPetRuntime* R=reinterpret_cast<FDesktopPetRuntime*>(GetWindowLongPtr(H,GWLP_USERDATA));
        if(M==WM_NCCREATE)
        {
            R=static_cast<FDesktopPetRuntime*>(reinterpret_cast<CREATESTRUCT*>(L)->lpCreateParams);
            SetWindowLongPtr(H,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(R));
        }
        if(!R)return DefWindowProc(H,M,W,L);
        // 原生消息内触发蓝图 Stop 时，运行时至少保留到本条消息返回。
        const TSharedPtr<FDesktopPetRuntime> MessageRuntime=R->Owner?R->Owner->Runtime:nullptr;
        if(!R->IsActive())return DefWindowProc(H,M,W,L);
        const auto ShellAction=R->Shell.HandleMessage(M,W,L);
        if(ShellAction==FDesktopPetShell::EAction::Restore){R->RestoreRequested=true;return 0;}
        if(ShellAction==FDesktopPetShell::EAction::Close){R->CloseRequested=true;return 0;}
        // 收起期间只保留系统/托盘消息，既不拾取人物也不响应残留鼠标按键。
        if(R->Shell.IsInTray()&&M>=WM_MOUSEFIRST&&M<=WM_MOUSELAST)return 0;
        // 移动统一由 PollInput 采样；提前覆盖移动坐标会丢失增强输入的位移增量。
        if((M==WM_LBUTTONDOWN||M==WM_LBUTTONDBLCLK||M==WM_LBUTTONUP||M==WM_RBUTTONDOWN||M==WM_RBUTTONUP||M==WM_MOUSEWHEEL))
        {
            R->LastCursor=R->Cursor;
            POINT Point{GET_X_LPARAM(L),GET_Y_LPARAM(L)};
            if(M==WM_MOUSEWHEEL)ScreenToClient(H,&Point);
            R->Cursor=FVector2D(Point.x,Point.y);
        }
        switch(M)
        {
        case WM_ERASEBKGND:return 1;
        case WM_CLOSE:R->CloseRequested=true;return 0;
        case WM_LBUTTONDOWN:R->PointerDown();return 0;
        case WM_LBUTTONDBLCLK:
            if(R->PickActor(R->Cursor)&&R->Owner->ConfigWidgetClass)R->Owner->PetOpenConfigWidget();
            else R->PointerDown();
            return 0;
        case WM_MOUSEMOVE:
            if(R->OrbitDragging)
            {
                const FVector2D Position(GET_X_LPARAM(L),GET_Y_LPARAM(L));
                R->MoveOrbit(Position-R->Cursor);R->LastCursor=R->Cursor;R->Cursor=Position;
            }
            return 0;
        case WM_LBUTTONUP:R->PointerUp();return 0;
        case WM_RBUTTONDOWN:
            R->PointerDown(EKeys::RightMouseButton);return 0;
        case WM_RBUTTONUP:
            R->OrbitDragging=false;R->NativePressed.Remove(EKeys::RightMouseButton);
            R->EmitPointer(EDesktopPetPointerEvent::Release,EKeys::RightMouseButton);
            if(R->NativePressed.IsEmpty()&&GetCapture()==H)ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            R->ZoomAtScreenPosition(GET_WHEEL_DELTA_WPARAM(W)/float(WHEEL_DELTA),FVector2D(GET_X_LPARAM(L),GET_Y_LPARAM(L)));
            R->EmitPointer(EDesktopPetPointerEvent::Wheel,FKey(),GET_WHEEL_DELTA_WPARAM(W)/float(WHEEL_DELTA));
            if(R->VirtualUser.IsValid())
            {
                FPointerEvent Event(R->VirtualUser->GetUserIndex(),0,R->ToUI(R->Cursor),R->ToUI(R->LastCursor),R->Pressed,FKey(),GET_WHEEL_DELTA_WPARAM(W)/float(WHEEL_DELTA),FModifierKeysState());
                FSlateApplication::Get().RouteMouseWheelOrGestureEvent(R->UIPath(),Event);
            }
            return 0;
        // 键盘退出等业务交给项目，不在透明核心中硬编码 Escape。
        // 捕获被系统夺走时也清理保持输入，避免增强输入动作一直停留在按下状态。
        case WM_CAPTURECHANGED:
            // 重复申请当前 HWND 的捕获不代表捕获被其他窗口夺走。
            if(reinterpret_cast<HWND>(L)!=H)R->ReleaseHeldInput();
            break;
        }
        return DefWindowProc(H,M,W,L);
    }
    // Slate 命中网格属于最近一次 UI 绘制；显示层缩放时将坐标映射回该网格。
    FVector2D ToUI(FVector2D P) const {return FVector2D(P.X*UIHitSize.X/DisplaySize.X,P.Y*UIHitSize.Y/DisplaySize.Y);}
    // 空白处、装饰 Widget 和真实可交互控件仍使用同一个命中路径。
    FWidgetPath UIPathAt(FVector2D Position) const
    {
        if(!VirtualWindow.IsValid())return FWidgetPath();
        auto Hits=VirtualWindow->GetHittestGrid().GetBubblePath(ToUI(Position),0,false,VirtualUser->GetUserIndex());
        return FWidgetPath(Hits);
    }
    // 当前指针与外部蓝图查询使用相同的物理坐标系。
    FWidgetPath UIPath() const {return UIPathAt(Cursor);}
    // 装饰文字不会抢占桌面输入，可交互的 Slate 控件优先于场景拖拽。
    bool HitsUI(const FWidgetPath& Path)const
    {
        for(const FArrangedWidget& W:Path.Widgets.GetInternalArray())if(W.Widget->IsInteractable())return true;
        return false;
    }
    // 为离屏虚拟 Slate 用户构造事件，避免依赖 UE 游戏窗口的焦点。
    FPointerEvent Pointer(FKey Key=FKey()) const
    {
        return FPointerEvent(VirtualUser->GetUserIndex(),0,ToUI(Cursor),ToUI(LastCursor),Pressed,Key,0,FModifierKeysState());
    }
    // 命中基于最终合成前的场景 Alpha，也适用于半透明模型及粒子。
    bool HitsScene()const
    {
        const int32 X=FMath::FloorToInt(Cursor.X),Y=FMath::FloorToInt(Cursor.Y);
        return X>=0&&Y>=0&&X<DisplaySize.X&&Y<DisplaySize.Y&&SceneAlpha.IsValidIndex(Y*DisplaySize.X+X)&&SceneAlpha[Y*DisplaySize.X+X]>=Threshold;
    }
    // 可见像素与组件包围盒联合拾取；可见名单自动获得交互权，不依赖查询碰撞。
    AActor* PickActor(FVector2D Position) const
    {
        if(Shell.IsInTray()||Owner->bConfigWidgetOpen||!Config.bTransparentWindowEnabled)return nullptr;
        if(!FMath::IsFinite(Position.X)||!FMath::IsFinite(Position.Y))return nullptr;
        const int32 X=FMath::FloorToInt(Position.X),Y=FMath::FloorToInt(Position.Y);
        if(X<0||Y<0||X>=DisplaySize.X||Y>=DisplaySize.Y||!SceneAlpha.IsValidIndex(Y*DisplaySize.X+X)||SceneAlpha[Y*DisplaySize.X+X]<Threshold)return nullptr;
        if(HitsUI(UIPathAt(Position)))return nullptr;
        return Owner->TraceInteractionActor(FVector2D((Position.X+.5)/DisplaySize.X,(Position.Y+.5)/DisplaySize.Y));
    }
    // 两种鼠标键统一判定；Actor 点击定义为按下，左键才参与默认窗口拖拽。
    void PointerDown(FKey Key=EKeys::LeftMouseButton)
    {
        if(!Owner||!VirtualUser.IsValid()||Shell.IsInTray())return;
        Owner->UpdateHoveredActor(PickActor(Cursor));
        if(!IsActive())return;
        EmitPointer(EDesktopPetPointerEvent::Press,Key);
        if(!IsActive())return;
        FWidgetPath Path=UIPath();
        if(HitsUI(Path))
        {
            if(Key!=EKeys::LeftMouseButton)return;
            NativePressed.Add(Key);
            UIPressed=true;Pressed.Add(Key);
            FSlateApplication::Get().RoutePointerDownEvent(Path,Pointer(Key));
            if(!IsActive())return;
            if(!Shell.IsInTray()&&GetCapture()!=Window)SetCapture(Window);
        }
        else if(AActor* Actor=PickActor(Cursor))
        {
            // 保持普通按键也需要捕获；是否进入自动窗口拖拽由 Draggable 配置决定。
            NativePressed.Add(Key);if(GetCapture()!=Window)SetCapture(Window);
            if(Key==EKeys::LeftMouseButton)
            {
                ++PetClicks;Owner->PetActorLeftClicked.Broadcast(Actor);Owner->PetEventActorLeftClicked(Actor);
                if(!IsActive())return;
                if(IsValid(Actor)&&Owner->PetGetInteractionActors().Contains(Actor))Owner->PetClicked.Broadcast();
                if(IsActive()&&IsValid(Actor)&&Owner->PetGetInteractionActors().Contains(Actor)&&Draggable&&!Owner->bConfigWidgetOpen)BeginDrag();
            }
            else if(Key==EKeys::RightMouseButton)
            {
                Owner->PetActorRightClicked.Broadcast(Actor);Owner->PetEventActorRightClicked(Actor);
                if(IsActive()&&!Shell.IsInTray()&&!Owner->bConfigWidgetOpen&&!Dragging&&IsValid(Owner->OrbitFocusActor))OrbitDragging=true;
            }
        }
    }
    // 按钮在 Slate 中正常触发 OnClicked，并释放原生鼠标捕获。
    void PointerUp()
    {
        // 在回调前清理保持状态，避免项目在 Release/OnClicked 内停止窗口时递归发送抬起。
        const bool WasUIPressed=UIPressed;
        UIPressed=false;
        NativePressed.Remove(EKeys::LeftMouseButton);
        Pressed.Remove(EKeys::LeftMouseButton);
        EmitPointer(EDesktopPetPointerEvent::Release,EKeys::LeftMouseButton);
        if(!IsActive()&&!Owner->bStopping)return;
        if(WasUIPressed)
        {
            FSlateApplication::Get().RoutePointerUpEvent(UIPath(),Pointer(EKeys::LeftMouseButton));
        }
        FinishDrag();if(NativePressed.IsEmpty()&&GetCapture()==Window)ReleaseCapture();
    }
    // 系统夺走捕获、主动停止显示时统一释放，避免增强输入动作残留在 Triggered。
    void ReleaseHeldInput()
    {
        const bool Left=NativePressed.Contains(EKeys::LeftMouseButton)||UIPressed||Dragging;
        const bool Right=NativePressed.Contains(EKeys::RightMouseButton);
        NativePressed.Empty();OrbitDragging=false;
        if(Left)PointerUp();
        if(Right)EmitPointer(EDesktopPetPointerEvent::Release,EKeys::RightMouseButton);
        if(GetCapture()==Window)ReleaseCapture();
    }
    // 创建窗口前先复制实例配置，不读取或修改项目全局默认对象。
    bool Create(const FDesktopPetConfig& InConfig)
    {
        FPetDpiScope DpiScope;
        Config=InConfig;
        ReadConfig();
        DisplaySize=BitmapSize=UIHitSize=FIntPoint(Width,Height);PresentedScale=ZoomTargetScale=Config.DisplayScale;
        Backend=GDynamicRHI?GDynamicRHI->GetName():TEXT("Unknown");
        WNDCLASSEXW WC{};WC.cbSize=sizeof(WC);WC.lpfnWndProc=WindowProc;WC.hInstance=GetModuleHandle(nullptr);
        WC.style=CS_DBLCLKS;
        WC.lpszClassName=L"UE58DesktopPetWindow";WC.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassExW(&WC);
        const FString Title=DesktopPetApplicationName::Get();
        Window=CreateWindowExW(WS_EX_LAYERED|WS_EX_APPWINDOW|(Topmost?WS_EX_TOPMOST:0),WC.lpszClassName,*Title,WS_POPUP,Config.WindowPosition.X,Config.WindowPosition.Y,Width,Height,nullptr,nullptr,WC.hInstance,this);
        if(!Window){UE_LOG(LogDesktopPet,Error,TEXT("CreateWindow failed: %u"),GetLastError());return false;}
        Shell.Attach(Window);
        MemoryDC=CreateCompatibleDC(nullptr);
        BITMAPINFO Info{};Info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);Info.bmiHeader.biWidth=Width;Info.bmiHeader.biHeight=-Height;
        Info.bmiHeader.biPlanes=1;Info.bmiHeader.biBitCount=32;Info.bmiHeader.biCompression=BI_RGB;
        Bitmap=CreateDIBSection(MemoryDC,&Info,DIB_RGB_COLORS,&Bits,nullptr,0);
        if(!Bitmap||!MemoryDC)return false;
        OldBitmap=SelectObject(MemoryDC,Bitmap);
        Pixels.SetNumZeroed(Width*Height);SceneAlpha.SetNumZeroed(Width*Height);
        FMemory::Memzero(Bits,Width*Height*4);
        VirtualUser=FSlateApplication::Get().FindOrCreateVirtualUser(Owner->GetUniqueID());
        VirtualWindow=SNew(SVirtualWindow).Size(FVector2D(Config.WindowSize));
        VirtualWindow->SetIsFocusable(true);
        SetWidget();
        FSlateApplication::Get().RegisterVirtualWindow(VirtualWindow.ToSharedRef());
        Renderer=MakeUnique<FWidgetRenderer>(true,true);
        ApplyWindowMode();
        ShowWindow(Window,Config.bTransparentWindowEnabled?SW_SHOWNOACTIVATE:SW_HIDE);
        ApplyWindowMode();
        FDesktopPetWindowGuard::SetHidden(Config.bHideGameWindow&&Config.bTransparentWindowEnabled);
        ApplyPetGameViewportPolicy(Config.bHideGameWindow&&Config.bTransparentWindowEnabled);
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground")))C->Set(0,ECVF_SetByCode);
        StartTime=FPlatformTime::Seconds();
        UE_LOG(LogDesktopPet,Display,TEXT("Started %s %dx%d SSAA=%dx%d FPS=%d Native layered window; model and UMG are live"),*Backend,Width,Height,Scale,Scale,FrameRate);
        return true;
    }
    // 先等待渲染线程再释放 DIB、虚拟窗口和读回对象，防止退出时资源悬空。
    ~FDesktopPetRuntime()
    {
        FlushRenderingCommands();
        Pending.Reset();ReusableReadback.Reset();
        if(FSlateApplication::IsInitialized()&&VirtualWindow.IsValid())FSlateApplication::Get().UnregisterVirtualWindow(VirtualWindow.ToSharedRef());
        VirtualWindow.Reset();VirtualUser.Reset();Renderer.Reset();
        Shell.Detach();
        if(Window){SetWindowLongPtr(Window,GWLP_USERDATA,0);DestroyWindow(Window);}
        if(OldBitmap&&MemoryDC)SelectObject(MemoryDC,OldBitmap);
        if(Bitmap)DeleteObject(Bitmap);
        if(MemoryDC)DeleteDC(MemoryDC);
        // 停止或重建桌宠也不恢复 UE 主窗口；只有显式 PetSetHideGameWindow(false) 才恢复。
    }
    // 使用屏幕坐标增量环绕，按显示缩放归一化，不移动或锁定用户系统光标。
    void MoveOrbit(FVector2D Delta)
    {
        if(!OrbitDragging||Owner->bConfigWidgetOpen||!IsValid(Owner->OrbitFocusActor))return;
        FRotator R=Owner->OrbitRotation;
        R.Yaw+=Delta.X*Owner->OrbitSensitivity/PresentedScale;
        R.Pitch-=Delta.Y*Owner->OrbitSensitivity/PresentedScale;
        Owner->PetSetOrbitRotation(R);
    }
    // 每帧检测指针；完全穿透时 HWND 收不到移动消息，仍需轮询系统光标。
    void PollInput(float Delta)
    {
        FPetDpiScope DpiScope;
        if(!Config.bTransparentWindowEnabled){Owner->UpdateHoveredActor(nullptr);return;}
        ApplyPetGameViewportPolicy(Config.bHideGameWindow&&Config.bTransparentWindowEnabled);
        POINT P{};GetCursorPos(&P);
        if(Dragging)SetWindowPos(Window,Topmost?HWND_TOPMOST:HWND_TOP,P.x-DragOffset.x,P.y-DragOffset.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        ScreenToClient(Window,&P);LastCursor=Cursor;Cursor=FVector2D(P.x,P.y);
        MoveOrbit(Cursor-LastCursor);
        const bool Inside=Cursor.X>=0&&Cursor.Y>=0&&Cursor.X<DisplaySize.X&&Cursor.Y<DisplaySize.Y;
        if(Owner->bConfigWidgetOpen&&!Inside)Owner->PetCloseConfigWidget();
        if(!IsActive())return;
        FWidgetPath Path=UIPath();
        Owner->UpdateHoveredActor(PickActor(Cursor));
        if(!IsActive())return;
        const bool OnUI=HitsUI(Path),OnScene=Owner->PetGetHoveredActor()!=nullptr;
        if(OnScene&&!Owner->bConfigWidgetOpen)
        {
            AActor* HoverActor=Owner->PetGetHoveredActor();
            Owner->PetActorMouseHover.Broadcast(HoverActor);Owner->PetEventActorMouseHover(HoverActor);
        }
        if(!IsActive())return;
        const double Now=FPlatformTime::Seconds();
        if(OnUI||OnScene||UIPressed||Dragging)LastHover=Now;
        Hovered=OnScene;
        MenuOpen=Owner->bConfigWidgetOpen;
        OpenAmount=FMath::FInterpConstantTo(OpenAmount,MenuOpen?1.f:0.f,Delta,1.f/AnimationSeconds);
        Owner->PetInteractionProgress.Broadcast(FMath::SmoothStep(0.f,1.f,OpenAmount),FVector2D(Config.WindowSize));
        if(!IsActive())return;
        // 即使指针静止，动画/粒子也可能改变其下方的有效 Alpha，需要刷新 Actor 命中状态。
        EmitPointer(EDesktopPetPointerEvent::Move);
        if(!IsActive())return;
        FSlateApplication::Get().RoutePointerMoveEvent(Path,Pointer(),false);
        const bool Transparent=ClickThrough&&!Owner->bConfigWidgetOpen&&!OnUI&&!OnScene&&!Dragging&&!OrbitDragging&&!UIPressed&&NativePressed.IsEmpty();
        if(Transparent!=WasInputTransparent)
        {
            LONG_PTR Style=GetWindowLongPtr(Window,GWL_EXSTYLE);
            SetWindowLongPtr(Window,GWL_EXSTYLE,Transparent?(Style|WS_EX_TRANSPARENT):(Style&~WS_EX_TRANSPARENT));
            WasInputTransparent=Transparent;
        }
    }
    // 一次系统提交同时更新图像、尺寸和位置，禁止先移动 HWND 再等待 GPU 新图。
    bool Present(FIntPoint Size,FIntPoint Position)
    {
        if(CapturedPixels.IsEmpty())return false;
        FPetDpiScope DpiScope;
        if(BitmapSize!=Size)
        {
            BITMAPINFO Info{};Info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);Info.bmiHeader.biWidth=Size.X;Info.bmiHeader.biHeight=-Size.Y;
            Info.bmiHeader.biPlanes=1;Info.bmiHeader.biBitCount=32;Info.bmiHeader.biCompression=BI_RGB;
            void* NewBits=nullptr;HBITMAP NewBitmap=CreateDIBSection(MemoryDC,&Info,DIB_RGB_COLORS,&NewBits,nullptr,0);
            if(!NewBitmap||!NewBits){if(NewBitmap)DeleteObject(NewBitmap);return false;}
            SelectObject(MemoryDC,NewBitmap);DeleteObject(Bitmap);Bitmap=NewBitmap;Bits=NewBits;BitmapSize=Size;
        }
        DesktopPetPresentation::Resample(CapturedPixels,CapturedAlpha,CapturedSize,Size,Pixels,SceneAlpha);
        FMemory::Memcpy(Bits,Pixels.GetData(),Pixels.Num()*sizeof(FColor));
        SIZE NativeSize{Size.X,Size.Y};POINT Destination{Position.X,Position.Y},Origin{0,0};BLENDFUNCTION Blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        if(!UpdateLayeredWindow(Window,nullptr,&Destination,&NativeSize,MemoryDC,&Origin,0,&Blend,ULW_ALPHA))
        {UE_LOG(LogDesktopPet,Error,TEXT("UpdateLayeredWindow failed: %u"),GetLastError());CloseRequested=true;return false;}
        DisplaySize=Size;++Presentations;return true;
    }
    // 新滚轮手势只从真实显示像素采样锚点；UI、透明区域、拖拽时均不接受缩放。
    bool ZoomAtScreenPosition(float Delta,FVector2D Screen)
    {
        if(!Config.bEnableWheelZoom||Dragging||OrbitDragging||UIPressed||!FMath::IsFinite(Delta)||Delta==0.f||!FMath::IsFinite(Screen.X)||!FMath::IsFinite(Screen.Y))return false;
        FPetDpiScope DpiScope;RECT R{};GetWindowRect(Window,&R);
        const FVector2D Local=Screen-FVector2D(R.left,R.top);
        if(!PickActor(Local)||CapturedPixels.IsEmpty())return false;
        FDesktopPetConfig Target=Config;
        Target.DisplayScale=(Zooming?ZoomTargetScale:PresentedScale)*FMath::Pow(1.f+Config.WheelZoomStep,FMath::Clamp(Delta,-64.f,64.f));Target.Normalize();
        if(FMath::IsNearlyEqual(Target.DisplayScale,Zooming?ZoomTargetScale:PresentedScale))return false;
        // 连续滚动且鼠标未移动时保留原始 UV，避免窗口整数取整误差逐次累积。
        const FVector2D Expected=ZoomScreen+FVector2D(.5,.5)-ZoomUV*FVector2D(DisplaySize);
        const bool Moved=FMath::Abs(Expected.X-R.left)>.501||FMath::Abs(Expected.Y-R.top)>.501;
        if(!ZoomAnchorValid||!ZoomScreen.Equals(Screen,0.001)||Moved)
        {ZoomScreen=Screen;ZoomUV=FVector2D((Local.X+.5)/DisplaySize.X,(Local.Y+.5)/DisplaySize.Y);ZoomAnchorValid=true;}
        ZoomStartScale=PresentedScale;ZoomTargetScale=Target.DisplayScale;ZoomStarted=FPlatformTime::Seconds();Zooming=true;
        return true;
    }
    // 停止手势后仅重建一次捕获尺寸，缩放过程不反复分配 RT 或清空有效命中 Alpha。
    void FinishZoom()
    {
        if(!Zooming)return;
        Zooming=false;ZoomTargetScale=PresentedScale;
        Owner->DesiredConfig=Config;Owner->DesiredConfig.DisplayScale=PresentedScale;
        Owner->DesiredConfig.WindowPosition=Owner->PetGetWindowPosition();
        Owner->bHasConfig=true;Owner->bConfigPending=true;Owner->bCenterAnchorValid=false;ZoomCommitPending=true;
    }
    // 动画按真实时间推进，与场景帧率、慢动作无关；屏幕锚点始终为 Screen+半个像素。
    void UpdateZoom()
    {
        if(!Zooming)return;
        if(Owner->bConfigWidgetOpen){FinishZoom();return;}
        const double T=Config.ZoomAnimationSeconds>0.f?FMath::Clamp((FPlatformTime::Seconds()-ZoomStarted)/Config.ZoomAnimationSeconds,0.,1.):1.;
        // 显示动画最多 60 Hz；场景继续遵守 TargetFrameRate，避免空闲高 Tick 频率浪费 CPU。
        const double Now=FPlatformTime::Seconds();
        if(T<1.&&Now-LastZoomPresentation<1./60.)return;
        LastZoomPresentation=Now;
        const double Ease=T*T*(3.-2.*T);
        FDesktopPetConfig C=Config;C.DisplayScale=FMath::Exp(FMath::Lerp(FMath::Loge(double(ZoomStartScale)),FMath::Loge(double(ZoomTargetScale)),Ease));
        const FIntPoint Size=C.GetDisplaySize();
        const FVector2D P=ZoomScreen+FVector2D(.5,.5)-ZoomUV*FVector2D(Size);
        const FIntPoint Position(FMath::RoundToInt(P.X),FMath::RoundToInt(P.Y));
        if(Size==DisplaySize||Present(Size,Position))PresentedScale=C.DisplayScale;
        if(T>=1.)FinishZoom();
    }

    // 把纯像素算法与原生窗口操作分离；合成器不认识项目中的角色或 Widget。
    void Composite(const FPetReadback& F)
    {
        FPetDpiScope DpiScope;
        const double Begin=FPlatformTime::Seconds();
        LastFrameUsedGPU=F.UseGPU;
        CoverageSamples=F.UseGPU?-1:0;
        if(F.UseGPU)
        {
            const int32 Count=F.Width*F.Height;
            CapturedPixels.SetNumUninitialized(Count);CapturedAlpha.SetNumUninitialized(Count);
            // GPU 已经完成量化和合成；CPU 只拆分 Windows 像素与交互 Alpha，不做颜色运算。
            for(int32 I=0;I<Count;++I){CapturedPixels[I].DWColor()=F.PackedPixels[I].BGRA;CapturedAlpha[I]=uint8(F.PackedPixels[I].SceneAlpha);}
            ReadbackPayloadBytes=uint64(Count)*sizeof(DesktopPetGPUCompositor::FPackedPixel);
        }
        else
        {
            if(Diagnostics)for(const FFloat16 A:F.OpacityPixels)CoverageSamples+=float(A)<1.f?1:0;
            ReadbackPayloadBytes=uint64(F.SceneWidth)*F.SceneHeight*10+(F.HasUI?uint64(Width)*Height*4:0);
            DesktopPetCompositor::Composite(F.OpacityPixels,F.UIPixels,Width,Height,F.FrameConfig,CapturedPixels,CapturedAlpha,F.FinalColorPixels,PetScratch);
        }
        CapturedSize=FIntPoint(Width,Height);
        CompositeMilliseconds=(FPlatformTime::Seconds()-Begin)*1000.;
        RECT Rect{};GetWindowRect(Window,&Rect);
        Present(DisplaySize,FIntPoint(Rect.left,Rect.top));
        ++Frames;
        FDesktopPetFramePacing::ObserveFrames(Owner,Frames);
    }
    // 模型与 UI 分开绘制，RHI 接口同时兼容 DX11/DX12；只保留一个在途请求。
    void CaptureFrame(float Delta)
    {
        if(CloseRequested||!Config.bTransparentWindowEnabled)return;
        if(Pending.IsValid())
        {
            if(Pending->Failed.Load())
            {
                // 丢弃失败帧并允许下次重新捕获，不能让一次 GPU 锁定失败永久卡住显示。
                UE_LOG(LogDesktopPet,Warning,TEXT("GPU readback lock failed; discarding frame"));
                Pending.Reset();
            }
            else if(Pending->Complete.Load())
            {if(!Shell.IsInTray())Composite(*Pending);ReusableReadback=Pending;Pending.Reset();}
            else if(Pending->Submitted.Load()&&!Pending->Copying.Load()&&(Pending->UseGPU?Pending->Packed.IsReady():((!Pending->HasUI||Pending->UI.IsReady())&&Pending->FinalColor.IsReady()&&Pending->Geometry.IsReady())))
            {
                Pending->Copying.Store(true);auto Frame=Pending;
                ENQUEUE_RENDER_COMMAND(DesktopPetReadback)([Frame](FRHICommandListImmediate& Cmd)
                {
                    if(Frame->UseGPU)
                    {
                        const int32 Count=Frame->Width*Frame->Height;
                        const uint32 Bytes=Count*sizeof(DesktopPetGPUCompositor::FPackedPixel);
                        const void* Data=Frame->Packed.Lock(Bytes);
                        if(!Data){Frame->Failed.Store(true);return;}
                        Frame->PackedPixels.SetNumUninitialized(Count);
                        FMemory::Memcpy(Frame->PackedPixels.GetData(),Data,Bytes);
                        Frame->Packed.Unlock();Frame->Complete.Store(true);return;
                    }
                    int32 Pitch=0;
                    {
                        const auto* C=static_cast<const FFloat16Color*>(Frame->FinalColor.Lock(Pitch));
                        if(!C){Frame->Failed.Store(true);return;}
                        Frame->FinalColorPixels.SetNumUninitialized(Frame->SceneWidth*Frame->SceneHeight);
                        for(int32 Y=0;Y<Frame->SceneHeight;++Y)FMemory::Memcpy(Frame->FinalColorPixels.GetData()+Y*Frame->SceneWidth,C+Y*Pitch,Frame->SceneWidth*sizeof(FFloat16Color));
                        Frame->FinalColor.Unlock();
                    }
                    const auto* G=static_cast<const FFloat16*>(Frame->Geometry.Lock(Pitch));
                    if(!G){Frame->Failed.Store(true);return;}
                    Frame->OpacityPixels.SetNumUninitialized(Frame->SceneWidth*Frame->SceneHeight);
                    for(int32 Y=0;Y<Frame->SceneHeight;++Y)FMemory::Memcpy(Frame->OpacityPixels.GetData()+Y*Frame->SceneWidth,G+Y*Pitch,Frame->SceneWidth*sizeof(FFloat16));
                    Frame->Geometry.Unlock();
                    if(Frame->HasUI)
                    {
                    const auto* U=static_cast<const FColor*>(Frame->UI.Lock(Pitch));
                    if(!U){Frame->Failed.Store(true);return;}
                    Frame->UIPixels.SetNumUninitialized(Frame->Width*Frame->Height);
                    for(int32 Y=0;Y<Frame->Height;++Y)FMemory::Memcpy(Frame->UIPixels.GetData()+Y*Frame->Width,U+Y*Pitch,Frame->Width*sizeof(FColor));
                    Frame->UI.Unlock();
                    }
                    else Frame->UIPixels.Reset();
                    Frame->Complete.Store(true);
                });
            }
        }
        const double Now=FPlatformTime::Seconds();
        if(Shell.IsInTray()||Pending.IsValid()||Now<NextCaptureTime)return;
        const double Interval=1.0/FrameRate;
        const float CaptureDelta=LastCapture>0.?float(Now-LastCapture):float(Interval);
        // 按绝对时间推进目标节拍，不把 Tick 量化的延迟累加到每一帧；慢帧跳过过期节拍，不突发补帧。
        if(NextCaptureTime<=0.)NextCaptureTime=Now;
        NextCaptureTime+=(FMath::FloorToDouble((Now-NextCaptureTime)/Interval)+1.)*Interval;
        LastCapture=Now;
        FDesktopPetMemory::Update(Owner,Config);
        Owner->SyncOpacityCapture();
        Owner->OpacityCapture->CaptureScene();
        Owner->Capture->CaptureScene();
        if(HasUI)UIHitSize=FIntPoint(Width,Height);
        if(HasUI)Renderer->DrawWindow(Owner->UITarget,VirtualWindow->GetHittestGrid(),VirtualWindow.ToSharedRef(),Config.DisplayScale,FVector2D(Width,Height),CaptureDelta,false);
        // 复用 staging 纹理及 CPU 数组，避免每帧分配数十 MB 的读回对象。
        auto Frame=ReusableReadback;ReusableReadback.Reset();
        if(!Frame){Frame=MakeShared<FPetReadback,ESPMode::ThreadSafe>();++ReadbackAllocations;}
        Frame->Submitted.Store(false);Frame->Copying.Store(false);Frame->Complete.Store(false);Frame->Failed.Store(false);Frame->HasUI=HasUI;
        Frame->UseGPU=DesktopPetGPUCompositor::IsEnabled(Config);Frame->FrameConfig=Config;
        Pending=Frame;
        Frame->Width=Width;Frame->Height=Height;Frame->SceneWidth=Width*Scale;Frame->SceneHeight=Height*Scale;
        FTextureRenderTargetResource* UR=Owner->UITarget->GameThread_GetRenderTargetResource();
        FTextureRenderTargetResource* CR=Owner->FinalColorTarget->GameThread_GetRenderTargetResource();
        FTextureRenderTargetResource* GR=Owner->GeometryTarget->GameThread_GetRenderTargetResource();
        ENQUEUE_RENDER_COMMAND(DesktopPetCopy)([Frame,UR,CR,GR](FRHICommandListImmediate& Cmd)
        {
            if(Frame->UseGPU)
            {
                DesktopPetGPUCompositor::Enqueue(Cmd,CR->GetRenderTargetTexture(),GR->GetRenderTargetTexture(),
                    Frame->HasUI?UR->GetRenderTargetTexture():nullptr,FIntPoint(Frame->Width,Frame->Height),Frame->FrameConfig,Frame->Packed);
                Frame->Submitted.Store(true);return;
            }
            if(Frame->HasUI)
            {
                FRHITexture* UT=UR->GetRenderTargetTexture();
                Cmd.Transition(FRHITransitionInfo(UT,ERHIAccess::Unknown,ERHIAccess::CopySrc));
                Frame->UI.EnqueueCopy(Cmd,UT);
                Cmd.Transition(FRHITransitionInfo(UT,ERHIAccess::CopySrc,ERHIAccess::SRVMask));
            }
            {
                FRHITexture* CT=CR->GetRenderTargetTexture();
                Cmd.Transition(FRHITransitionInfo(CT,ERHIAccess::Unknown,ERHIAccess::CopySrc));
                Frame->FinalColor.EnqueueCopy(Cmd,CT);
                Cmd.Transition(FRHITransitionInfo(CT,ERHIAccess::CopySrc,ERHIAccess::SRVMask));
            }
            FRHITexture* GT=GR->GetRenderTargetTexture();
            Cmd.Transition(FRHITransitionInfo(GT,ERHIAccess::Unknown,ERHIAccess::CopySrc));
            Frame->Geometry.EnqueueCopy(Cmd,GT);
            Cmd.Transition(FRHITransitionInfo(GT,ERHIAccess::CopySrc,ERHIAccess::SRVMask));
            Frame->Submitted.Store(true);
        });
    }
    // 诊断只记录当前实例和本进程主窗口状态，不修改任何项目业务。
    void Report(bool Image)
    {
        FPetDpiScope DpiScope;
        const int32 RenderWidth=Width,RenderHeight=Height;
        const int32 ImageWidth=DisplaySize.X,ImageHeight=DisplaySize.Y;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("DesktopPet");IFileManager::Get().MakeDirectory(*Folder,true);
        int32 Empty=0,Solid=0,Edge=0;FIntPoint TestPoint(-1,-1);int32 MinX=ImageWidth,MaxX=0,MinY=ImageHeight,MaxY=0;
        for(int32 I=0;I<SceneAlpha.Num();++I)
        {
            uint8 A=SceneAlpha[I];if(!A)++Empty;else if(A==255)++Solid;else ++Edge;
            if(A>=Threshold){const int32 X=I%ImageWidth,Y=I/ImageWidth;MinX=FMath::Min(MinX,X);MaxX=FMath::Max(MaxX,X);MinY=FMath::Min(MinY,Y);MaxY=FMath::Max(MaxY,Y);if(Y>ImageHeight/3&&Y<ImageHeight*2/3&&X>ImageWidth/3&&X<ImageWidth*2/3)TestPoint=FIntPoint(X,Y);}
        }
        RECT Rect{};GetWindowRect(Window,&Rect);
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("backend"),Backend);J->SetNumberField(TEXT("frames"),Frames);
        J->SetBoolField(TEXT("hidden_to_tray"),Shell.IsInTray());
        J->SetBoolField(TEXT("show_in_taskbar"),Config.WindowMode!=EDesktopPetWindowMode::ToolWindow);
        J->SetBoolField(TEXT("taskbar_policy_applied"),Shell.IsTaskbarPolicyApplied());
        J->SetBoolField(TEXT("allow_screen_capture"),Config.bAllowScreenCapture);
        J->SetBoolField(TEXT("capture_policy_applied"),Shell.IsCapturePolicyApplied());
        J->SetNumberField(TEXT("effective_engine_fps_limit"),FDesktopPetFramePacing::GetEffectiveLimit());
        J->SetNumberField(TEXT("average_fps"),Frames/FMath::Max(.001,FPlatformTime::Seconds()-StartTime));
        J->SetNumberField(TEXT("width"),ImageWidth);J->SetNumberField(TEXT("height"),ImageHeight);J->SetNumberField(TEXT("ssaa_scale"),Scale);
        J->SetNumberField(TEXT("scene_zero_alpha"),Empty);J->SetNumberField(TEXT("scene_opaque_pixels"),Solid);J->SetNumberField(TEXT("scene_fractional_alpha"),Edge);
        J->SetNumberField(TEXT("window_x"),Rect.left);J->SetNumberField(TEXT("window_y"),Rect.top);
        J->SetNumberField(TEXT("pet_test_x"),TestPoint.X);J->SetNumberField(TEXT("pet_test_y"),TestPoint.Y);
        J->SetStringField(TEXT("scene_bounds"),FString::Printf(TEXT("%d,%d,%d,%d"),MinX,MinY,MaxX,MaxY));
        J->SetBoolField(TEXT("hovered"),Hovered);J->SetBoolField(TEXT("menu_open"),MenuOpen);
        J->SetBoolField(TEXT("click_through"),WasInputTransparent);J->SetNumberField(TEXT("pet_clicks"),PetClicks);J->SetNumberField(TEXT("ui_actions"),UIClicks);J->SetStringField(TEXT("last_action"),LastAction);

        const FDesktopPetGameWindowState Guard=FDesktopPetWindowGuard::GetState();
        J->SetNumberField(TEXT("ue_window_count"),Guard.WindowCount);
        J->SetNumberField(TEXT("ue_visible_windows"),Guard.VisibleCount);
        J->SetNumberField(TEXT("ue_enabled_windows"),Guard.EnabledCount);
        J->SetNumberField(TEXT("ue_taskbar_eligible_windows"),Guard.TaskbarEligibleCount);
        J->SetNumberField(TEXT("ue_show_attempts_blocked"),Guard.PreventedShowCount);
        J->SetNumberField(TEXT("display_scale"),PresentedScale);
        J->SetNumberField(TEXT("zoom_target_scale"),Zooming?ZoomTargetScale:PresentedScale);
        J->SetBoolField(TEXT("zooming"),Zooming);
        J->SetNumberField(TEXT("presentations"),Presentations);
        // 记录有效模式，方便区分桌宠的可发现性与底层 UE 窗口的隐藏状态。
        J->SetStringField(TEXT("window_mode"),Config.WindowMode==EDesktopPetWindowMode::ToolWindow?TEXT("ToolWindow"):TEXT("Application"));
        J->SetNumberField(TEXT("render_width"),RenderWidth);J->SetNumberField(TEXT("render_height"),RenderHeight);
        J->SetBoolField(TEXT("capture_translucency"),Config.bCaptureTranslucency);
        J->SetBoolField(TEXT("engine_post_processing"),true);
        J->SetNumberField(TEXT("coverage_samples"),CoverageSamples);
        J->SetNumberField(TEXT("readback_payload_bytes"),ReadbackPayloadBytes);
        J->SetNumberField(TEXT("readback_allocations"),ReadbackAllocations);
        J->SetNumberField(TEXT("composite_cpu_ms"),CompositeMilliseconds);
        J->SetBoolField(TEXT("gpu_composite"),LastFrameUsedGPU);
        J->SetBoolField(TEXT("coverage_samples_available"),!LastFrameUsedGPU);
        J->SetNumberField(TEXT("gpu_composite_working_bytes"),LastFrameUsedGPU?uint64(RenderWidth)*RenderHeight*12:0);
        J->SetNumberField(TEXT("owned_render_target_bytes"),uint64(RenderWidth)*RenderHeight*(18*Scale*Scale+4));
        J->SetBoolField(TEXT("hidden_world_rendering_disabled"),!GIsEditor&&GEngine&&GEngine->GameViewport&&GEngine->GameViewport->bDisableWorldRendering);
        J->SetBoolField(TEXT("compact_memory_requested"),Config.bCompactMemory);
        J->SetBoolField(TEXT("compact_memory_active"),Owner->PetIsCompactMemoryActive());
        // 记录最终有效的全局值，方便发现命令行/控制台覆盖，不把配置请求值当成实测值。
        auto Memory=MakeShared<FJsonObject>();
        for(const TCHAR* Name:{TEXT("r.Shadow.Virtual.MaxPhysicalPages"),TEXT("r.LumenScene.SurfaceCache.AtlasSize"),TEXT("r.Lumen.ScreenProbeGather.RadianceCache.ProbeAtlasResolutionInProbes"),TEXT("r.RenderTargetPoolMin"),TEXT("RHI.TransientAllocator.MinimumHeapSize"),TEXT("d3d12.PoolAllocator.ReadOnlyTextureVRAMPoolSize"),TEXT("fx.Cascade.GpuSpriteDynamicAllocations")})
            if(auto* C=IConsoleManager::Get().FindConsoleVariable(Name))Memory->SetNumberField(Name,C->GetInt());
        J->SetObjectField(TEXT("memory_cvars"),Memory);
        const auto Policy=FDesktopPetRenderPolicy::Resolve(Owner->Capture);
        J->SetNumberField(TEXT("gi_method"),Policy.GI);J->SetNumberField(TEXT("reflection_method"),Policy.Reflections);
        J->SetNumberField(TEXT("lumen_surface_resolution"),Policy.SurfaceResolution);
        if(Owner->ViewExtension)
        {J->SetNumberField(TEXT("view_gi_method"),Owner->ViewExtension->LastViewGI);J->SetNumberField(TEXT("view_reflection_method"),Owner->ViewExtension->LastViewReflections);}
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable")))J->SetBoolField(TEXT("vsm_enabled"),C->GetInt()!=0);
        J->SetBoolField(TEXT("transparent_window"),Config.bTransparentWindowEnabled);
        J->SetBoolField(TEXT("config_widget_open"),Owner->bConfigWidgetOpen);
        J->SetBoolField(TEXT("orbit_dragging"),OrbitDragging);
        J->SetBoolField(TEXT("preserve_additive"),Config.bPreserveAdditiveEffects);
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(J,Writer);
        FFileHelper::SaveStringToFile(Text,*(Folder/(TEXT("Runtime-")+Backend+TEXT(".json"))));
        if(Image&&Frames)
        {
            TArray<FColor> Straight=Pixels;
            for(auto& P:Straight)if(P.A){P.R=FMath::Min(255,int32(P.R)*255/P.A);P.G=FMath::Min(255,int32(P.G)*255/P.A);P.B=FMath::Min(255,int32(P.B)*255/P.A);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(ImageWidth,ImageHeight,Straight,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/(TEXT("Frame-")+Backend+FString::Printf(TEXT("-AA%d.png"),Scale))));
        }
    }
};
