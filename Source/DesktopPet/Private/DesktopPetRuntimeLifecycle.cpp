#include "DesktopPetRuntime.h"

/* 窗口创建、配置应用与资源释放；执行顺序保持在原有成员函数中。 */

bool FDesktopPetRuntime::IsActive() const
{
    return Owner && !Owner->bStopping && Owner->Runtime.Get() == this && !Owner->IsActorBeingDestroyed();
}

bool FDesktopPetRuntime::Create(const FDesktopPetConfig& InConfig)
{
    FPetDpiScope DpiScope;
    Config = InConfig;
    ReadConfig();
    DisplaySize = BitmapSize = UIHitSize = FIntPoint(Width, Height);
    PresentedScale = ZoomTargetScale = Config.DisplayScale;
    WNDCLASSEXW WC{};
    WC.cbSize = sizeof(WC);
    WC.lpfnWndProc = WindowProc;
    WC.hInstance = GetModuleHandle(nullptr);
    WC.style = CS_DBLCLKS;
    WC.lpszClassName = L"UE58DesktopPetWindow";
    WC.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&WC);
    const FString Title = DesktopPetApplicationName::Get();
    Window = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW | (Topmost ? WS_EX_TOPMOST : 0),
                             WC.lpszClassName,
                             *Title,
                             WS_POPUP,
                             Config.WindowPosition.X,
                             Config.WindowPosition.Y,
                             Width,
                             Height,
                             nullptr,
                             nullptr,
                             WC.hInstance,
                             this);
    if (!Window)
    {
        UE_LOG(LogDesktopPet, Error, TEXT("CreateWindow failed: %u"), GetLastError());
        return false;
    }
    Shell.Attach(Window);
    MemoryDC = CreateCompatibleDC(nullptr);
    BITMAPINFO Info{};
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(MemoryDC, &Info, DIB_RGB_COLORS, &Bits, nullptr, 0);
    if (!Bitmap || !MemoryDC)
    {
        return false;
    }
    OldBitmap = SelectObject(MemoryDC, Bitmap);
    SceneAlpha.SetNumZeroed(Width * Height);
    FMemory::Memzero(Bits, Width * Height * 4);
    VirtualUser = FSlateApplication::Get().FindOrCreateVirtualUser(Owner->GetUniqueID());
    VirtualWindow = SNew(SVirtualWindow).Size(FVector2D(Config.WindowSize));
    VirtualWindow->SetIsFocusable(true);
    SetWidget();
    FSlateApplication::Get().RegisterVirtualWindow(VirtualWindow.ToSharedRef());
    PetAnimationPreTickHandle =
        FSlateApplication::Get().OnPreTick().AddRaw(this, &FDesktopPetRuntime::MarkPetWidgetAnimationTicks);
    // Slate 完成本帧全局光标查询后，仅为鼠标所在的桌宠窗口重申已选光标。
    PetCursorPostTickHandle = FSlateApplication::Get().OnPostTick().AddLambda(
        [this](float)
        {
            if (IsActive() && Config.bTransparentWindowEnabled && !Shell.IsInTray())
            {
                if (!NativeCursor.Apply(Window))
                {
                    NativeCursor.Release();
                }
            }
            else
            {
                NativeCursor.Release();
            }
        });
    Renderer = MakeUnique<FWidgetRenderer>(true, true);
    ApplyWindowMode();
    ShowWindow(Window, Config.bTransparentWindowEnabled ? SW_SHOWNOACTIVATE : SW_HIDE);
    ApplyWindowMode();
    FDesktopPetWindowGuard::SetHidden(Config.bHideGameWindow && Config.bTransparentWindowEnabled);
    ApplyPetGameViewportPolicy(Config.bHideGameWindow && Config.bTransparentWindowEnabled);
    if (auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("t.IdleWhenNotForeground")))
    {
        C->Set(0, ECVF_SetByCode);
    }

    return true;
}

FDesktopPetRuntime::~FDesktopPetRuntime()
{
    NativeCursor.Release();
    if (FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().OnPreTick().Remove(PetAnimationPreTickHandle);
        FSlateApplication::Get().OnPostTick().Remove(PetCursorPostTickHandle);
    }
    FlushRenderingCommands();
    PendingReadbacks.Reset();
    AvailableReadbacks.Reset();
    if (FSlateApplication::IsInitialized() && VirtualWindow.IsValid())
    {
        FSlateApplication::Get().UnregisterVirtualWindow(VirtualWindow.ToSharedRef());
    }
    VirtualWindow.Reset();
    VirtualUser.Reset();
    Renderer.Reset();
    Shell.Detach();
    if (Window)
    {
        SetWindowLongPtr(Window, GWLP_USERDATA, 0);
        DestroyWindow(Window);
    }
    if (OldBitmap && MemoryDC)
    {
        SelectObject(MemoryDC, OldBitmap);
    }
    if (Bitmap)
    {
        DeleteObject(Bitmap);
    }
    if (MemoryDC)
    {
        DeleteDC(MemoryDC);
    }
    // 停止或重建桌宠也不恢复 UE 主窗口；只有显式 PetSetHideGameWindow(false) 才恢复。
}

void FDesktopPetRuntime::ReadConfig()
{
    const FIntPoint Size = Config.GetDisplaySize();
    Width = Size.X;
    Height = Size.Y;
    Scale = Config.SupersampleScale;
    FrameRate = Config.TargetFrameRate;
    Exposure = Config.Exposure;
    Threshold = Config.HitAlphaThreshold;
    CloseDelay = Config.MenuCloseDelay;
    AnimationSeconds = Config.MenuAnimationSeconds;
    Topmost = Config.bAlwaysOnTop;
    ClickThrough = Config.bClickThroughTransparentPixels;
    Draggable = Config.bDraggable;
    ExitOnClose = Config.bExitApplicationOnClose;
    NativeCursor.SetCompositeSoftwareCursor(Config.bCompositeSoftwareCursor);
}

void FDesktopPetRuntime::ApplyWindowMode()
{
    Shell.ApplyFlags(Config.WindowMode != EDesktopPetWindowMode::ToolWindow, Config.bAllowScreenCapture);
}

void FDesktopPetRuntime::ApplyConfig(const FDesktopPetConfig& NewConfig)
{
    FPetDpiScope DpiScope;
    const bool Resize =
        NewConfig.GetDisplaySize() != Config.GetDisplaySize() || NewConfig.SupersampleScale != Config.SupersampleScale;
    RECT ActualRect{};
    GetWindowRect(Window, &ActualRect);
    const bool Move = NewConfig.WindowPosition != FIntPoint(ActualRect.left, ActualRect.top);
    const bool WindowModeChanged =
        NewConfig.WindowMode != Config.WindowMode || NewConfig.bAllowScreenCapture != Config.bAllowScreenCapture;
    const bool ModeChanged = NewConfig.bTransparentWindowEnabled != Config.bTransparentWindowEnabled;
    const bool PreserveGesture = Zooming && ZoomCommitPending;
    if (!ZoomCommitPending)
    {
        ZoomAnchorValid = false;
    }
    ZoomCommitPending = false;
    if (!PreserveGesture)
    {
        Zooming = false;
    }
    if (NewConfig.TargetFrameRate != Config.TargetFrameRate || Resize)
    {
        NextCaptureTime = 0.;
    }
    // 切换兼容路径时回收旧 staging 资源，避免保留两套回读缓存。
    if (NewConfig.bUseGPUCompositing != Config.bUseGPUCompositing)
    {
        FlushRenderingCommands();
        PendingReadbacks.Reset();
        AvailableReadbacks.Reset();
    }
    Config = NewConfig;
    ReadConfig();
    if (WindowModeChanged)
    {
        ReleaseHeldInput();
        ApplyWindowMode();
    }
    if (!PreserveGesture)
    {
        PresentedScale = ZoomTargetScale = Config.DisplayScale;
        if (CapturedPixels.IsEmpty())
        {
            DisplaySize = Config.GetDisplaySize();
        }
        Present(Config.GetDisplaySize(), Config.WindowPosition);
    }
    if (!Draggable)
    {
        FinishDrag();
    }
    if (ModeChanged)
    {
        NativeCursor.Release();
        ReleaseHeldInput();
        Owner->PetCompleteConfigWidgetClose();
        // 调试窗口模式不能留在托盘里，否则普通游戏窗口和收起状态会相互矛盾。
        if (Shell.IsInTray())
        {
            RestoreRequested = true;
        }
        ShowWindow(Window, Config.bTransparentWindowEnabled && !Shell.IsInTray() ? SW_SHOWNOACTIVATE : SW_HIDE);
        ApplyWindowMode();
    }
    if (Resize)
    {
        FlushRenderingCommands();
        PendingReadbacks.Reset();
        AvailableReadbacks.Reset();
        Owner->ViewExtension.Reset();
        Owner->SceneTarget->InitCustomFormat(Width * Scale, Height * Scale, PF_FloatRGBA, true);
        Owner->SceneTarget->UpdateResourceImmediate(true);
        Owner->FinalColorTarget->InitCustomFormat(Width * Scale, Height * Scale, PF_FloatRGBA, true);
        Owner->FinalColorTarget->UpdateResourceImmediate(true);
        Owner->GeometryTarget->InitCustomFormat(Width * Scale, Height * Scale, PF_R16F, true);
        Owner->GeometryTarget->UpdateResourceImmediate(true);
        Owner->ViewExtension = FSceneViewExtensions::NewExtension<FDesktopPetViewExtension>(
            Owner->FinalColorTarget->GameThread_GetRenderTargetResource(),
            Owner->GeometryTarget->GameThread_GetRenderTargetResource(),
            Owner->SceneTarget->GameThread_GetRenderTargetResource(),
            Owner);
        // 与启动时保持一致：Slate 负责 Gamma，RT 禁止再次 sRGB 编码；缩放后颜色不能改变。
        Owner->UITarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, true);
        Owner->UITarget->UpdateResourceImmediate(true);
    }
    VirtualWindow->Resize(FVector2D(Config.WindowSize));
    Owner->ConfigureCapturePipeline();
    SetWindowPos(Window,
                 Topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 Config.WindowPosition.X,
                 Config.WindowPosition.Y,
                 0,
                 0,
                 SWP_NOACTIVATE | SWP_NOSIZE | ((Move && !CapturedPixels.Num()) ? 0 : SWP_NOMOVE));
    if (Dragging)
    {
        POINT P{};
        GetCursorPos(&P);
        ScreenToClient(Window, &P);
        DragOffset = P;
    }
    FDesktopPetWindowGuard::SetHidden(Config.bHideGameWindow && Config.bTransparentWindowEnabled);
    ApplyPetGameViewportPolicy(Config.bHideGameWindow && Config.bTransparentWindowEnabled);
}
