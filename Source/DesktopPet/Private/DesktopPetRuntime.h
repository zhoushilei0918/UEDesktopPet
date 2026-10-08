#pragma once
#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
#include "Blueprint/UserWidget.h"
#include "DesktopPetCompositor.h"
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
    FRHIGPUTextureReadback Scene{TEXT("DesktopPetScene")};
    FRHIGPUTextureReadback UI{TEXT("DesktopPetUI")};
    TAtomic<bool> Submitted{false}, Copying{false}, Complete{false}, Failed{false};
    TArray<FFloat16Color> ScenePixels;
    TArray<FColor> UIPixels;
    int32 SceneWidth=0,SceneHeight=0,Width=0,Height=0;
};

// 输入隔离独立于桌宠 HWND 的生命周期；停止再启动显示时仍保留主视口原始策略。
// 仅影响打包游戏，编辑器中的关卡编辑视口保持原有操作方式。
inline void ApplyPetGameViewportPolicy(bool Hidden)
{
    if(GIsEditor||!GEngine||!GEngine->GameViewport)return;
    static TWeakObjectPtr<UGameViewportClient> SavedViewport;
    static bool SavedIgnore=false;
    static EMouseCaptureMode SavedCapture=EMouseCaptureMode::NoCapture;
    static EMouseLockMode SavedLock=EMouseLockMode::DoNotLock;
    UGameViewportClient* Viewport=GEngine->GameViewport;
    if(Hidden)
    {
        if(SavedViewport.Get()!=Viewport)
        {
            SavedViewport=Viewport;SavedIgnore=Viewport->IgnoreInput();
            SavedCapture=Viewport->GetMouseCaptureMode();SavedLock=Viewport->GetMouseLockMode();
        }
        Viewport->SetIgnoreInput(true);
        Viewport->SetMouseCaptureMode(EMouseCaptureMode::NoCapture);
        Viewport->SetMouseLockMode(EMouseLockMode::DoNotLock);
    }
    else if(SavedViewport.Get()==Viewport)
    {
        Viewport->SetIgnoreInput(SavedIgnore);Viewport->SetMouseCaptureMode(SavedCapture);
        Viewport->SetMouseLockMode(SavedLock);SavedViewport.Reset();
    }
}

/** 一个显示实例的运行时资源；只关联通用宿主，不依赖任何项目角色、菜单或特效类。 */
struct FDesktopPetRuntime
{
    ADesktopPetActor* Owner=nullptr;
    HWND Window=nullptr;
    FDesktopPetConfig Config;
    HDC MemoryDC=nullptr;
    HBITMAP Bitmap=nullptr;
    HGDIOBJ OldBitmap=nullptr;
    void* Bits=nullptr;
    int32 Width=600,Height=760,Scale=2,FrameRate=30,Threshold=8;
    float Exposure=1,CloseDelay=.4f,AnimationSeconds=.18f;
    bool Topmost=true,ClickThrough=true,Draggable=true,ExitOnClose=true,Diagnostics=false;
    bool CloseRequested=false,Dragging=false,UIPressed=false,Hovered=false,MenuOpen=false;
    bool WasInputTransparent=false;
    POINT DragOffset{};
    FVector2D Cursor=FVector2D::ZeroVector,LastCursor=FVector2D::ZeroVector;
    double LastCapture=0,LastHover=0,LastReport=0,StartTime=0;
    float OpenAmount=0;
    uint64 Frames=0,PetClicks=0,UIClicks=0;
    FString Backend,LastAction;
    TArray<FColor> Pixels;
    TArray<uint8> SceneAlpha;
    TSharedPtr<FPetReadback,ESPMode::ThreadSafe> Pending;
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
        FDesktopPetPointerEvent E;
        E.Type=Type;E.Key=Key;E.PixelPosition=Cursor;E.CanvasPosition=Cursor/Config.DisplayScale;
        E.Delta=Cursor-LastCursor;E.WheelDelta=Wheel;E.bOverUI=HitsUI(UIPath());E.bOverScene=HitsScene();
        E.HitActor=PickActor(Cursor);
        Owner->OnPointerInput.Broadcast(E);
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
    // 接收项目 Widget，空指针对应完全透明的空 UI 层。
    void SetWidget()
    {
        if(Owner->OverlayWidget)VirtualWindow->SetContent(Owner->OverlayWidget->TakeWidget());
        else VirtualWindow->SetContent(SNullWidget::NullWidget);
    }
    // 绘制目标尺寸改变时等待旧 GPU 请求结束，避免新旧尺寸混用；HWND 与 Widget 都保持不变。
    void ApplyConfig(const FDesktopPetConfig& NewConfig)
    {
        FPetDpiScope DpiScope;
        const bool Resize=NewConfig.GetDisplaySize()!=Config.GetDisplaySize()||NewConfig.SupersampleScale!=Config.SupersampleScale;
        const bool Move=NewConfig.WindowPosition!=Config.WindowPosition;
        const bool HideChanged=NewConfig.bHideGameWindow!=Config.bHideGameWindow;
        Config=NewConfig;
        ReadConfig();
        if(!Draggable)FinishDrag();
        if(Resize)
        {
            FlushRenderingCommands();Pending.Reset();
            if(OldBitmap)SelectObject(MemoryDC,OldBitmap);
            if(Bitmap)DeleteObject(Bitmap);
            BITMAPINFO Info{};Info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);Info.bmiHeader.biWidth=Width;Info.bmiHeader.biHeight=-Height;
            Info.bmiHeader.biPlanes=1;Info.bmiHeader.biBitCount=32;Info.bmiHeader.biCompression=BI_RGB;
            Bitmap=CreateDIBSection(MemoryDC,&Info,DIB_RGB_COLORS,&Bits,nullptr,0);
            if(!Bitmap||!Bits)
            {
                UE_LOG(LogDesktopPet,Error,TEXT("Resize DIB allocation failed: %u"),GetLastError());
                CloseRequested=true;return;
            }
            OldBitmap=SelectObject(MemoryDC,Bitmap);
            Pixels.SetNumZeroed(Width*Height);SceneAlpha.SetNumZeroed(Width*Height);
            Owner->SceneTarget->InitCustomFormat(Width*Scale,Height*Scale,PF_FloatRGBA,true);
            Owner->SceneTarget->UpdateResourceImmediate(true);
            Owner->UITarget->InitCustomFormat(Width,Height,PF_B8G8R8A8,false);
            Owner->UITarget->UpdateResourceImmediate(true);
        }
        VirtualWindow->Resize(FVector2D(Config.WindowSize));
        Owner->Capture->ShowFlags.SetTranslucency(Config.bCaptureTranslucency);
        Owner->Capture->ShowFlags.SetSeparateTranslucency(Config.bCaptureTranslucency);
        SetWindowPos(Window,Topmost?HWND_TOPMOST:HWND_NOTOPMOST,Config.WindowPosition.X,Config.WindowPosition.Y,
                     Width,Height,SWP_NOACTIVATE|(Move?0:SWP_NOMOVE));
        if(HideChanged)FDesktopPetWindowGuard::SetHidden(Config.bHideGameWindow);
        ApplyPetGameViewportPolicy(Config.bHideGameWindow);
    }
    // 手动拖拽 API 不要求鼠标位于人物上，可用于项目自定义拖拽把手或增强输入。
    bool BeginDrag()
    {
        if(!Draggable||Dragging)return Dragging;
        Dragging=true;DragOffset={static_cast<LONG>(Cursor.X),static_cast<LONG>(Cursor.Y)};
        if(GetCapture()!=Window)SetCapture(Window);
        Owner->OnDragStateChanged.Broadcast(true);return true;
    }
    // 无论主动结束还是 Windows 捕获丢失，都发出一次结束通知。
    void FinishDrag()
    {
        if(!Dragging)return;
        Dragging=false;
        if(GetCapture()==Window)ReleaseCapture();
        Owner->OnDragStateChanged.Broadcast(false);
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
        // 移动统一由 PollInput 采样；提前覆盖移动坐标会丢失增强输入的位移增量。
        if((M==WM_LBUTTONDOWN||M==WM_LBUTTONUP||M==WM_RBUTTONDOWN||M==WM_RBUTTONUP||M==WM_MOUSEWHEEL))
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
        case WM_LBUTTONUP:R->PointerUp();return 0;
        case WM_RBUTTONDOWN:
            R->PointerDown(EKeys::RightMouseButton);return 0;
        case WM_RBUTTONUP:
            R->NativePressed.Remove(EKeys::RightMouseButton);
            R->EmitPointer(EDesktopPetPointerEvent::Release,EKeys::RightMouseButton);
            if(R->NativePressed.IsEmpty()&&GetCapture()==H)ReleaseCapture();
            return 0;
        case WM_MOUSEWHEEL:
            R->EmitPointer(EDesktopPetPointerEvent::Wheel,FKey(),GET_WHEEL_DELTA_WPARAM(W)/float(WHEEL_DELTA));
            if(R->VirtualUser.IsValid())
            {
                FPointerEvent Event(R->VirtualUser->GetUserIndex(),0,R->Cursor,R->LastCursor,R->Pressed,FKey(),GET_WHEEL_DELTA_WPARAM(W)/float(WHEEL_DELTA),FModifierKeysState());
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
    // HitTestGrid 使用最终物理像素坐标，因此显示缩放后仍能准确命中 UMG。
    FWidgetPath UIPathAt(FVector2D Position) const
    {
        if(!VirtualWindow.IsValid())return FWidgetPath();
        auto Hits=VirtualWindow->GetHittestGrid().GetBubblePath(Position,0,false,VirtualUser->GetUserIndex());
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
        return FPointerEvent(VirtualUser->GetUserIndex(),0,Cursor,LastCursor,Pressed,Key,0,FModifierKeysState());
    }
    // 命中基于最终合成前的场景 Alpha，也适用于半透明模型及粒子。
    bool HitsScene()const
    {
        const int32 X=FMath::FloorToInt(Cursor.X),Y=FMath::FloorToInt(Cursor.Y);
        return X>=0&&Y>=0&&X<Width&&Y<Height&&SceneAlpha.IsValidIndex(Y*Width+X)&&SceneAlpha[Y*Width+X]>=Threshold;
    }
    // 必须同时满足已渲染的 Alpha、非 UI 和捕获相机碰撞查询；显示白名单不自动授予交互权。
    AActor* PickActor(FVector2D Position) const
    {
        if(!FMath::IsFinite(Position.X)||!FMath::IsFinite(Position.Y))return nullptr;
        const int32 X=FMath::FloorToInt(Position.X),Y=FMath::FloorToInt(Position.Y);
        if(X<0||Y<0||X>=Width||Y>=Height||!SceneAlpha.IsValidIndex(Y*Width+X)||SceneAlpha[Y*Width+X]<Threshold)return nullptr;
        if(HitsUI(UIPathAt(Position)))return nullptr;
        return Owner->TraceInteractionActor(FVector2D((Position.X+.5)/Width,(Position.Y+.5)/Height));
    }
    // 两种鼠标键统一判定；Actor 点击定义为按下，左键才参与默认窗口拖拽。
    void PointerDown(FKey Key=EKeys::LeftMouseButton)
    {
        if(!Owner||!VirtualUser.IsValid())return;
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
            if(GetCapture()!=Window)SetCapture(Window);
        }
        else if(AActor* Actor=PickActor(Cursor))
        {
            // 保持普通按键也需要捕获；是否进入窗口拖拽由配置或项目增强输入决定。
            NativePressed.Add(Key);if(GetCapture()!=Window)SetCapture(Window);
            if(Key==EKeys::LeftMouseButton)
            {
                ++PetClicks;Owner->OnActorLeftClicked.Broadcast(Actor);
                if(!IsActive())return;
                if(IsValid(Actor)&&Owner->InteractionActors.Contains(Actor))Owner->OnPetClicked.Broadcast();
                if(IsActive()&&IsValid(Actor)&&Owner->InteractionActors.Contains(Actor)&&Draggable&&Config.bAutoDragOnPrimaryButton)BeginDrag();
            }
            else if(Key==EKeys::RightMouseButton)Owner->OnActorRightClicked.Broadcast(Actor);
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
        NativePressed.Empty();
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
        Backend=GDynamicRHI?GDynamicRHI->GetName():TEXT("Unknown");
        WNDCLASSEXW WC{};WC.cbSize=sizeof(WC);WC.lpfnWndProc=WindowProc;WC.hInstance=GetModuleHandle(nullptr);
        WC.lpszClassName=L"UE58DesktopPetWindow";WC.hCursor=LoadCursor(nullptr,IDC_ARROW);
        RegisterClassExW(&WC);
        const FString Title=FString::Printf(TEXT("DesktopPet - %s"),*Backend);
        Window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|(Topmost?WS_EX_TOPMOST:0),WC.lpszClassName,*Title,WS_POPUP,Config.WindowPosition.X,Config.WindowPosition.Y,Width,Height,nullptr,nullptr,WC.hInstance,this);
        if(!Window){UE_LOG(LogDesktopPet,Error,TEXT("CreateWindow failed: %u"),GetLastError());return false;}
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
        ShowWindow(Window,SW_SHOWNOACTIVATE);
        FDesktopPetWindowGuard::SetHidden(Config.bHideGameWindow);
        ApplyPetGameViewportPolicy(Config.bHideGameWindow);
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground")))C->Set(0,ECVF_SetByCode);
        StartTime=FPlatformTime::Seconds();
        UE_LOG(LogDesktopPet,Display,TEXT("Started %s %dx%d SSAA=%dx%d FPS=%d Native layered window; model and UMG are live"),*Backend,Width,Height,Scale,Scale,FrameRate);
        return true;
    }
    // 先等待渲染线程再释放 DIB、虚拟窗口和读回对象，防止退出时资源悬空。
    ~FDesktopPetRuntime()
    {
        FlushRenderingCommands();
        Pending.Reset();
        if(FSlateApplication::IsInitialized()&&VirtualWindow.IsValid())FSlateApplication::Get().UnregisterVirtualWindow(VirtualWindow.ToSharedRef());
        VirtualWindow.Reset();VirtualUser.Reset();Renderer.Reset();
        if(Window){SetWindowLongPtr(Window,GWLP_USERDATA,0);DestroyWindow(Window);}
        if(OldBitmap&&MemoryDC)SelectObject(MemoryDC,OldBitmap);
        if(Bitmap)DeleteObject(Bitmap);
        if(MemoryDC)DeleteDC(MemoryDC);
        // 停止或重建桌宠也不恢复 UE 主窗口；只有显式 SetHideGameWindow(false) 才恢复。
    }
    // 每帧检测指针；完全穿透时 HWND 收不到移动消息，仍需轮询系统光标。
    void PollInput(float Delta)
    {
        FPetDpiScope DpiScope;
        ApplyPetGameViewportPolicy(Config.bHideGameWindow);
        POINT P{};GetCursorPos(&P);
        if(Dragging)SetWindowPos(Window,Topmost?HWND_TOPMOST:HWND_TOP,P.x-DragOffset.x,P.y-DragOffset.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        ScreenToClient(Window,&P);LastCursor=Cursor;Cursor=FVector2D(P.x,P.y);
        FWidgetPath Path=UIPath();
        Owner->UpdateHoveredActor(PickActor(Cursor));
        if(!IsActive())return;
        const bool OnUI=HitsUI(Path),OnScene=Owner->GetHoveredActor()!=nullptr;
        const double Now=FPlatformTime::Seconds();
        if(OnUI||OnScene||UIPressed||Dragging)LastHover=Now;
        Hovered=OnScene;
        MenuOpen=OnUI||OnScene||UIPressed||Dragging||(Now-LastHover<CloseDelay);
        OpenAmount=FMath::FInterpConstantTo(OpenAmount,MenuOpen?1.f:0.f,Delta,1.f/AnimationSeconds);
        Owner->OnInteractionProgress.Broadcast(FMath::SmoothStep(0.f,1.f,OpenAmount),FVector2D(Config.WindowSize));
        if(!IsActive())return;
        // 即使指针静止，动画/粒子也可能改变其下方的有效 Alpha，需要刷新 Actor 命中状态。
        EmitPointer(EDesktopPetPointerEvent::Move);
        if(!IsActive())return;
        FSlateApplication::Get().RoutePointerMoveEvent(Path,Pointer(),false);
        const bool Transparent=ClickThrough&&!OnUI&&!OnScene&&!Dragging&&!UIPressed&&NativePressed.IsEmpty();
        if(Transparent!=WasInputTransparent)
        {
            LONG_PTR Style=GetWindowLongPtr(Window,GWL_EXSTYLE);
            SetWindowLongPtr(Window,GWL_EXSTYLE,Transparent?(Style|WS_EX_TRANSPARENT):(Style&~WS_EX_TRANSPARENT));
            WasInputTransparent=Transparent;
        }
    }
    // 把纯像素算法与原生窗口操作分离；合成器不认识项目中的角色或 Widget。
    void Composite(const FPetReadback& F)
    {
        FPetDpiScope DpiScope;
        DesktopPetCompositor::Composite(F.ScenePixels,F.UIPixels,Width,Height,Config,Pixels,SceneAlpha);
        FMemory::Memcpy(Bits,Pixels.GetData(),Pixels.Num()*sizeof(FColor));
        SIZE Size{Width,Height};POINT Origin{0,0};BLENDFUNCTION Blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        if(!UpdateLayeredWindow(Window,nullptr,nullptr,&Size,MemoryDC,&Origin,0,&Blend,ULW_ALPHA))
            UE_LOG(LogDesktopPet,Error,TEXT("UpdateLayeredWindow failed: %u"),GetLastError());
        ++Frames;
    }
    // 模型与 UI 分开绘制，RHI 接口同时兼容 DX11/DX12；只保留一个在途请求。
    void CaptureFrame(float Delta)
    {
        if(CloseRequested)return;
        if(Pending.IsValid())
        {
            if(Pending->Failed.Load())
            {
                // 丢弃失败帧并允许下次重新捕获，不能让一次 GPU 锁定失败永久卡住显示。
                UE_LOG(LogDesktopPet,Warning,TEXT("GPU readback lock failed; discarding frame"));
                Pending.Reset();
            }
            else if(Pending->Complete.Load())
            {Composite(*Pending);Pending.Reset();}
            else if(Pending->Submitted.Load()&&!Pending->Copying.Load()&&Pending->Scene.IsReady()&&Pending->UI.IsReady())
            {
                Pending->Copying.Store(true);auto Frame=Pending;
                ENQUEUE_RENDER_COMMAND(DesktopPetReadback)([Frame](FRHICommandListImmediate& Cmd)
                {
                    int32 Pitch=0;
                    const auto* S=static_cast<const FFloat16Color*>(Frame->Scene.Lock(Pitch));
                    if(!S){Frame->Failed.Store(true);return;}
                    Frame->ScenePixels.SetNumUninitialized(Frame->SceneWidth*Frame->SceneHeight);
                    for(int32 Y=0;Y<Frame->SceneHeight;++Y)FMemory::Memcpy(Frame->ScenePixels.GetData()+Y*Frame->SceneWidth,S+Y*Pitch,Frame->SceneWidth*sizeof(FFloat16Color));
                    Frame->Scene.Unlock();
                    const auto* U=static_cast<const FColor*>(Frame->UI.Lock(Pitch));
                    if(!U){Frame->Failed.Store(true);return;}
                    Frame->UIPixels.SetNumUninitialized(Frame->Width*Frame->Height);
                    for(int32 Y=0;Y<Frame->Height;++Y)FMemory::Memcpy(Frame->UIPixels.GetData()+Y*Frame->Width,U+Y*Pitch,Frame->Width*sizeof(FColor));
                    Frame->UI.Unlock();Frame->Complete.Store(true);
                });
            }
        }
        const double Now=FPlatformTime::Seconds();
        if(Pending.IsValid()||Now-LastCapture<1.0/FrameRate)return;
        LastCapture=Now;
        Owner->Capture->CaptureScene();
        Renderer->DrawWindow(Owner->UITarget,VirtualWindow->GetHittestGrid(),VirtualWindow.ToSharedRef(),Config.DisplayScale,FVector2D(Width,Height),Delta,false);
        auto Frame=MakeShared<FPetReadback,ESPMode::ThreadSafe>();Pending=Frame;
        Frame->Width=Width;Frame->Height=Height;Frame->SceneWidth=Width*Scale;Frame->SceneHeight=Height*Scale;
        FTextureRenderTargetResource* SR=Owner->SceneTarget->GameThread_GetRenderTargetResource();
        FTextureRenderTargetResource* UR=Owner->UITarget->GameThread_GetRenderTargetResource();
        ENQUEUE_RENDER_COMMAND(DesktopPetCopy)([Frame,SR,UR](FRHICommandListImmediate& Cmd)
        {
            FRHITexture* ST=SR->GetRenderTargetTexture();FRHITexture* UT=UR->GetRenderTargetTexture();
            Cmd.Transition(FRHITransitionInfo(ST,ERHIAccess::Unknown,ERHIAccess::CopySrc));
            Cmd.Transition(FRHITransitionInfo(UT,ERHIAccess::Unknown,ERHIAccess::CopySrc));
            Frame->Scene.EnqueueCopy(Cmd,ST);Frame->UI.EnqueueCopy(Cmd,UT);
            Cmd.Transition(FRHITransitionInfo(ST,ERHIAccess::CopySrc,ERHIAccess::SRVMask));
            Cmd.Transition(FRHITransitionInfo(UT,ERHIAccess::CopySrc,ERHIAccess::SRVMask));
            Frame->Submitted.Store(true);
        });
    }
    // 诊断只记录当前实例和本进程主窗口状态，不修改任何项目业务。
    void Report(bool Image)
    {
        FPetDpiScope DpiScope;
        const FString Folder=FPaths::ProjectSavedDir()/TEXT("DesktopPet");IFileManager::Get().MakeDirectory(*Folder,true);
        int32 Empty=0,Solid=0,Edge=0;FIntPoint TestPoint(-1,-1);int32 MinX=Width,MaxX=0,MinY=Height,MaxY=0;
        for(int32 I=0;I<SceneAlpha.Num();++I)
        {
            uint8 A=SceneAlpha[I];if(!A)++Empty;else if(A==255)++Solid;else ++Edge;
            if(A>=Threshold){const int32 X=I%Width,Y=I/Width;MinX=FMath::Min(MinX,X);MaxX=FMath::Max(MaxX,X);MinY=FMath::Min(MinY,Y);MaxY=FMath::Max(MaxY,Y);if(Y>Height/3&&Y<Height*2/3&&X>Width/3&&X<Width*2/3)TestPoint=FIntPoint(X,Y);}
        }
        RECT Rect{};GetWindowRect(Window,&Rect);
        auto J=MakeShared<FJsonObject>();J->SetStringField(TEXT("backend"),Backend);J->SetNumberField(TEXT("frames"),Frames);
        J->SetNumberField(TEXT("average_fps"),Frames/FMath::Max(.001,FPlatformTime::Seconds()-StartTime));
        J->SetNumberField(TEXT("width"),Width);J->SetNumberField(TEXT("height"),Height);J->SetNumberField(TEXT("ssaa_scale"),Scale);
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
        J->SetNumberField(TEXT("display_scale"),Config.DisplayScale);
        J->SetBoolField(TEXT("capture_translucency"),Config.bCaptureTranslucency);
        J->SetBoolField(TEXT("preserve_additive"),Config.bPreserveAdditiveEffects);
        FString Text;auto Writer=TJsonWriterFactory<>::Create(&Text);FJsonSerializer::Serialize(J,Writer);
        FFileHelper::SaveStringToFile(Text,*(Folder/(TEXT("Runtime-")+Backend+TEXT(".json"))));
        if(Image&&Frames)
        {
            TArray<FColor> Straight=Pixels;
            for(auto& P:Straight)if(P.A){P.R=FMath::Min(255,int32(P.R)*255/P.A);P.G=FMath::Min(255,int32(P.G)*255/P.A);P.B=FMath::Min(255,int32(P.B)*255/P.A);}
            TArray64<uint8> PNG;FImageUtils::PNGCompressImageArray(Width,Height,Straight,PNG);
            FFileHelper::SaveArrayToFile(PNG,*(Folder/(TEXT("Frame-")+Backend+FString::Printf(TEXT("-AA%d.png"),Scale))));
        }
    }
};
