#include "DesktopPetRuntime.h"

/* 原生消息、人物拾取与指针事件；执行顺序保持在原有成员函数中。 */

void FDesktopPetRuntime::EmitPointer(EDesktopPetPointerEvent Type, FKey Key, float Wheel)
{
    if (!Owner->PetPointerInput.IsBound())
    {
        return;
    }
    FDesktopPetPointerEvent E;
    E.Type = Type;
    E.Key = Key;
    E.PixelPosition = Cursor;
    E.CanvasPosition =
        FVector2D(Cursor.X * Config.WindowSize.X / DisplaySize.X, Cursor.Y * Config.WindowSize.Y / DisplaySize.Y);
    E.Delta = Cursor - LastCursor;
    E.WheelDelta = Wheel;
    E.bOverUI = HitsUI(UIPath());
    E.bOverScene = HitsScene();
    E.HitActor = PickActor(Cursor);
    Owner->PetPointerInput.Broadcast(E);
}

bool FDesktopPetRuntime::BeginDrag()
{
    if (Shell.IsInTray() || Owner->bConfigWidgetOpen || !Config.bTransparentWindowEnabled)
    {
        return false;
    }
    if (!Draggable || Dragging || OrbitDragging)
    {
        return Dragging;
    }
    FinishZoom();
    Dragging = true;
    DragOffset = {static_cast<LONG>(Cursor.X), static_cast<LONG>(Cursor.Y)};
    if (GetCapture() != Window)
    {
        SetCapture(Window);
    }
    Owner->PetDragStateChanged.Broadcast(true);
    return true;
}

void FDesktopPetRuntime::FinishDrag()
{
    if (!Dragging)
    {
        return;
    }
    Dragging = false;
    if (NativePressed.IsEmpty() && GetCapture() == Window)
    {
        ReleaseCapture();
    }
    Owner->PetDragStateChanged.Broadcast(false);
}

LRESULT CALLBACK FDesktopPetRuntime::WindowProc(HWND InWindow, UINT InMessage, WPARAM InWParam, LPARAM InLParam)
{
    FDesktopPetRuntime* Runtime = reinterpret_cast<FDesktopPetRuntime*>(GetWindowLongPtr(InWindow, GWLP_USERDATA));
    if (InMessage == WM_NCCREATE)
    {
        Runtime = static_cast<FDesktopPetRuntime*>(reinterpret_cast<CREATESTRUCT*>(InLParam)->lpCreateParams);
        SetWindowLongPtr(InWindow, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(Runtime));
    }
    if (!Runtime)
    {
        return DefWindowProc(InWindow, InMessage, InWParam, InLParam);
    }
    // 原生消息内触发蓝图 Stop 时，运行时至少保留到本条消息返回。
    const TSharedPtr<FDesktopPetRuntime> MessageRuntime = Runtime->Owner ? Runtime->Owner->Runtime : nullptr;
    if (!Runtime->IsActive())
    {
        return DefWindowProc(InWindow, InMessage, InWParam, InLParam);
    }
    const auto ShellAction = Runtime->Shell.HandleMessage(InMessage, InWParam, InLParam);
    if (ShellAction == FDesktopPetShell::EAction::Restore)
    {
        Runtime->RestoreRequested = true;
        return 0;
    }
    if (ShellAction == FDesktopPetShell::EAction::Close)
    {
        Runtime->CloseRequested = true;
        return 0;
    }
    // 收起期间只保留系统/托盘消息，既不拾取人物也不响应残留鼠标按键。
    if (Runtime->Shell.IsInTray() && InMessage >= WM_MOUSEFIRST && InMessage <= WM_MOUSELAST)
    {
        return 0;
    }
    // 先更新查询快照，再执行现有交互；UI 右键、中键和侧键也能查询，但不增加捕获或事件路由。
    if (Runtime->Config.bTransparentWindowEnabled)
    {
        Runtime->InputState.HandleMessage(InMessage, InWParam);
    }
    // 移动统一由 PollInput 采样；提前覆盖移动坐标会丢失增强输入的位移增量。
    if ((InMessage == WM_LBUTTONDOWN || InMessage == WM_LBUTTONDBLCLK || InMessage == WM_LBUTTONUP ||
         InMessage == WM_RBUTTONDOWN || InMessage == WM_RBUTTONUP || InMessage == WM_MOUSEWHEEL))
    {
        Runtime->LastCursor = Runtime->Cursor;
        POINT Point{GET_X_LPARAM(InLParam), GET_Y_LPARAM(InLParam)};
        if (InMessage == WM_MOUSEWHEEL)
        {
            ScreenToClient(InWindow, &Point);
        }
        Runtime->Cursor = FVector2D(Point.x, Point.y);
    }
    switch (InMessage)
    {
        case WM_NCHITTEST:
        {
            FPetDpiScope DpiScope;
            POINT Point{GET_X_LPARAM(InLParam), GET_Y_LPARAM(InLParam)};
            ScreenToClient(InWindow, &Point);
            if (Runtime->PassThroughBasePixel(FIntPoint(Point.x, Point.y)))
            {
                return HTTRANSPARENT;
            }
            break;
        }
        case WM_SETCURSOR:
        {
            if (LOWORD(InLParam) == HTCLIENT && Runtime->NativeCursor.Apply(InWindow))
            {
                return 1;
            }
            break;
        }
        case WM_ERASEBKGND:
        {
            return 1;
        }
        case WM_CLOSE:
        {
            Runtime->CloseRequested = true;
            return 0;
        }
        case WM_LBUTTONDOWN:
        {
            Runtime->PointerDown();
            return 0;
        }
        case WM_LBUTTONDBLCLK:
        {
            if (Runtime->PickActor(Runtime->Cursor) && Runtime->Owner->ConfigWidgetClass)
            {
                Runtime->Owner->PetOpenConfigWidget();
            }
            else
            {
                Runtime->PointerDown();
            }
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            if (Runtime->OrbitDragging)
            {
                const FVector2D Position(GET_X_LPARAM(InLParam), GET_Y_LPARAM(InLParam));
                Runtime->MoveOrbit(Position - Runtime->Cursor);
                Runtime->LastCursor = Runtime->Cursor;
                Runtime->Cursor = Position;
            }
            return 0;
        }
        case WM_LBUTTONUP:
        {
            Runtime->PointerUp();
            return 0;
        }
        case WM_RBUTTONDOWN:
        {
            Runtime->PointerDown(EKeys::RightMouseButton);
            return 0;
        }
        case WM_RBUTTONUP:
        {
            Runtime->OrbitDragging = false;
            Runtime->NativePressed.Remove(EKeys::RightMouseButton);
            Runtime->EmitPointer(EDesktopPetPointerEvent::Release, EKeys::RightMouseButton);
            if (Runtime->NativePressed.IsEmpty() && GetCapture() == InWindow)
            {
                ReleaseCapture();
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            Runtime->ZoomAtScreenPosition(GET_WHEEL_DELTA_WPARAM(InWParam) / float(WHEEL_DELTA),
                                          FVector2D(GET_X_LPARAM(InLParam), GET_Y_LPARAM(InLParam)));
            Runtime->EmitPointer(
                EDesktopPetPointerEvent::Wheel, FKey(), GET_WHEEL_DELTA_WPARAM(InWParam) / float(WHEEL_DELTA));
            if (Runtime->VirtualUser.IsValid())
            {
                FPointerEvent Event(Runtime->VirtualUser->GetUserIndex(),
                                    0,
                                    Runtime->ToUI(Runtime->Cursor),
                                    Runtime->ToUI(Runtime->LastCursor),
                                    Runtime->Pressed,
                                    FKey(),
                                    GET_WHEEL_DELTA_WPARAM(InWParam) / float(WHEEL_DELTA),
                                    FModifierKeysState());
                FSlateApplication::Get().RouteMouseWheelOrGestureEvent(Runtime->UIPath(), Event);
            }
            return 0;
            // 键盘退出等业务交给项目，不在透明核心中硬编码 Escape。
            // 捕获被系统夺走时也清理保持输入，避免增强输入动作一直停留在按下状态。
        }
        case WM_CAPTURECHANGED:
        {
            // 重复申请当前 HWND 的捕获不代表捕获被其他窗口夺走。
            if (reinterpret_cast<HWND>(InLParam) != InWindow)
            {
                Runtime->ReleaseHeldInput();
            }
            break;
        }
    }
    return DefWindowProc(InWindow, InMessage, InWParam, InLParam);
}

FVector2D FDesktopPetRuntime::ToUI(FVector2D P) const
{
    return UIOrigin + FVector2D(P.X * UIHitSize.X / DisplaySize.X, P.Y * UIHitSize.Y / DisplaySize.Y);
}

FWidgetPath FDesktopPetRuntime::UIPathAt(FVector2D Position) const
{
    if (!VirtualWindow.IsValid())
    {
        return FWidgetPath();
    }
    auto Hits = VirtualWindow->GetHittestGrid().GetBubblePath(ToUI(Position), 0, false, VirtualUser->GetUserIndex());
    return FWidgetPath(Hits);
}

FWidgetPath FDesktopPetRuntime::UIPath() const
{
    return UIPathAt(Cursor);
}

bool FDesktopPetRuntime::HitsUI(const FWidgetPath& Path) const
{
    for (const FArrangedWidget& W : Path.Widgets.GetInternalArray())
    {
        if (W.Widget->IsInteractable())
        {
            return true;
        }
    }
    return false;
}

FPointerEvent FDesktopPetRuntime::Pointer(FKey Key) const
{
    return FPointerEvent(
        VirtualUser->GetUserIndex(), 0, ToUI(Cursor), ToUI(LastCursor), Pressed, Key, 0, FModifierKeysState());
}

bool FDesktopPetRuntime::HitsScene() const
{
    const int32 X = FMath::FloorToInt(Cursor.X), Y = FMath::FloorToInt(Cursor.Y);
    return X >= 0 && Y >= 0 && X < DisplaySize.X && Y < DisplaySize.Y &&
           SceneAlpha.IsValidIndex(Y * DisplaySize.X + X) && SceneAlpha[Y * DisplaySize.X + X] >= Threshold;
}

AActor* FDesktopPetRuntime::PickActor(FVector2D Position) const
{
    if (Shell.IsInTray() || Owner->bConfigWidgetOpen || !Config.bTransparentWindowEnabled)
    {
        return nullptr;
    }
    if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y))
    {
        return nullptr;
    }
    const int32 X = FMath::FloorToInt(Position.X), Y = FMath::FloorToInt(Position.Y);
    if (X < 0 || Y < 0 || X >= DisplaySize.X || Y >= DisplaySize.Y || !SceneAlpha.IsValidIndex(Y * DisplaySize.X + X) ||
        SceneAlpha[Y * DisplaySize.X + X] < Threshold)
    {
        return nullptr;
    }
    if (HitsUI(UIPathAt(Position)))
    {
        return nullptr;
    }
    return Owner->TraceInteractionActor(
        FVector2D((Position.X + .5) / DisplaySize.X, (Position.Y + .5) / DisplaySize.Y));
}

void FDesktopPetRuntime::PointerDown(FKey Key)
{
    if (!Owner || !VirtualUser.IsValid() || Shell.IsInTray())
    {
        return;
    }
    Owner->UpdateHoveredActor(PickActor(Cursor));
    if (!IsActive())
    {
        return;
    }
    EmitPointer(EDesktopPetPointerEvent::Press, Key);
    if (!IsActive())
    {
        return;
    }
    FWidgetPath Path = UIPath();
    if (HitsUI(Path))
    {
        if (Key != EKeys::LeftMouseButton)
        {
            return;
        }
        NativePressed.Add(Key);
        UIPressed = true;
        Pressed.Add(Key);
        FSlateApplication::Get().RoutePointerDownEvent(Path, Pointer(Key));
        if (!IsActive())
        {
            return;
        }
        if (!Shell.IsInTray() && GetCapture() != Window)
        {
            SetCapture(Window);
        }
    }
    else if (AActor* Actor = PickActor(Cursor))
    {
        // 保持普通按键也需要捕获；是否进入自动窗口拖拽由 Draggable 配置决定。
        NativePressed.Add(Key);
        if (GetCapture() != Window)
        {
            SetCapture(Window);
        }
        if (Key == EKeys::LeftMouseButton)
        {
            Owner->PetActorLeftClicked.Broadcast(Actor);
            Owner->PetEventActorLeftClicked(Actor);
            if (!IsActive())
            {
                return;
            }
            if (IsValid(Actor) && Owner->PetGetInteractionActors().Contains(Actor))
            {
                Owner->PetClicked.Broadcast();
            }
            if (IsActive() && IsValid(Actor) && Owner->PetGetInteractionActors().Contains(Actor) && Draggable &&
                !Owner->bConfigWidgetOpen)
            {
                BeginDrag();
            }
        }
        else if (Key == EKeys::RightMouseButton)
        {
            Owner->PetActorRightClicked.Broadcast(Actor);
            Owner->PetEventActorRightClicked(Actor);
            if (IsActive() && !Shell.IsInTray() && !Owner->bConfigWidgetOpen && !Dragging &&
                IsValid(Owner->OrbitFocusActor))
            {
                OrbitDragging = true;
            }
        }
    }
}

void FDesktopPetRuntime::PointerUp()
{
    // 在回调前清理保持状态，避免项目在 Release/OnClicked 内停止窗口时递归发送抬起。
    const bool WasUIPressed = UIPressed;
    UIPressed = false;
    NativePressed.Remove(EKeys::LeftMouseButton);
    Pressed.Remove(EKeys::LeftMouseButton);
    EmitPointer(EDesktopPetPointerEvent::Release, EKeys::LeftMouseButton);
    if (!IsActive() && !Owner->bStopping)
    {
        return;
    }
    if (WasUIPressed)
    {
        FSlateApplication::Get().RoutePointerUpEvent(UIPath(), Pointer(EKeys::LeftMouseButton));
    }
    FinishDrag();
    if (NativePressed.IsEmpty() && GetCapture() == Window)
    {
        ReleaseCapture();
    }
}

void FDesktopPetRuntime::ReleaseHeldInput()
{
    InputState.Reset();
    const bool Left = NativePressed.Contains(EKeys::LeftMouseButton) || UIPressed || Dragging;
    const bool Right = NativePressed.Contains(EKeys::RightMouseButton);
    NativePressed.Empty();
    OrbitDragging = false;
    if (Left)
    {
        PointerUp();
    }
    if (Right)
    {
        EmitPointer(EDesktopPetPointerEvent::Release, EKeys::RightMouseButton);
    }
    if (GetCapture() == Window)
    {
        ReleaseCapture();
    }
}

void FDesktopPetRuntime::MoveOrbit(FVector2D Delta)
{
    if (!OrbitDragging || Owner->bConfigWidgetOpen || !IsValid(Owner->OrbitFocusActor))
    {
        return;
    }
    FRotator R = Owner->OrbitRotation;
    R.Yaw += Delta.X * Owner->OrbitSensitivity / PresentedScale;
    R.Pitch -= Delta.Y * Owner->OrbitSensitivity / PresentedScale;
    Owner->PetSetOrbitRotation(R);
}

bool FDesktopPetRuntime::PassThroughBasePixel(FIntPoint Point) const
{
    return ClickThrough && Bits && !Dragging && !OrbitDragging && !UIPressed && NativePressed.IsEmpty() &&
           CursorSurface.GetBaseAlpha(static_cast<const FColor*>(Bits), BitmapSize, Point) == 0;
}

void FDesktopPetRuntime::PollInput(float Delta)
{
    FPetDpiScope DpiScope;
    InputState.Refresh();
    if (!Config.bTransparentWindowEnabled)
    {
        NativeCursor.Release();
        Owner->UpdateHoveredActor(nullptr);
        return;
    }
    ApplyPetGameViewportPolicy(Config.bHideGameWindow && Config.bTransparentWindowEnabled);
    POINT P{};
    GetCursorPos(&P);
    if (Dragging)
    {
        SetWindowPos(Window,
                     Topmost ? HWND_TOPMOST : HWND_TOP,
                     P.x - DragOffset.x,
                     P.y - DragOffset.y,
                     0,
                     0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
    }
    ScreenToClient(Window, &P);
    LastCursor = Cursor;
    Cursor = FVector2D(P.x, P.y);
    MoveOrbit(Cursor - LastCursor);
    const bool Inside = Cursor.X >= 0 && Cursor.Y >= 0 && Cursor.X < DisplaySize.X && Cursor.Y < DisplaySize.Y;
    if (Owner->bConfigWidgetOpen && !Inside)
    {
        Owner->PetCloseConfigWidget();
    }
    if (!IsActive())
    {
        return;
    }
    FWidgetPath Path = UIPath();
    Owner->UpdateHoveredActor(PickActor(Cursor));
    if (!IsActive())
    {
        return;
    }
    const bool OnUI = HitsUI(Path), OnScene = Owner->PetGetHoveredActor() != nullptr;
    if (OnScene && !Owner->bConfigWidgetOpen)
    {
        AActor* HoverActor = Owner->PetGetHoveredActor();
        Owner->PetActorMouseHover.Broadcast(HoverActor);
        Owner->PetEventActorMouseHover(HoverActor);
    }
    if (!IsActive())
    {
        return;
    }
    const double Now = FPlatformTime::Seconds();
    if (OnUI || OnScene || UIPressed || Dragging)
    {
        LastHover = Now;
    }
    Hovered = OnScene;
    MenuOpen = Owner->bConfigWidgetOpen;
    OpenAmount = FMath::FInterpConstantTo(OpenAmount, MenuOpen ? 1.f : 0.f, Delta, 1.f / AnimationSeconds);
    Owner->PetInteractionProgress.Broadcast(FMath::SmoothStep(0.f, 1.f, OpenAmount), FVector2D(Config.WindowSize));
    if (!IsActive())
    {
        return;
    }
    // 即使指针静止，动画/粒子也可能改变其下方的有效 Alpha，需要刷新 Actor 命中状态。
    EmitPointer(EDesktopPetPointerEvent::Move);
    if (!IsActive())
    {
        return;
    }
    FSlateApplication::Get().RoutePointerMoveEvent(Path, Pointer(), false);
    const bool Transparent = PassThroughBasePixel(FIntPoint(P.x, P.y)) ||
                             (ClickThrough && !Owner->bConfigWidgetOpen && !OnUI && !OnScene && !Dragging &&
                              !OrbitDragging && !UIPressed && NativePressed.IsEmpty());
    if (Transparent != WasInputTransparent)
    {
        LONG_PTR Style = GetWindowLongPtr(Window, GWL_EXSTYLE);
        SetWindowLongPtr(Window, GWL_EXSTYLE, Transparent ? (Style | WS_EX_TRANSPARENT) : (Style & ~WS_EX_TRANSPARENT));
        WasInputTransparent = Transparent;
    }
    if (IsActive())
    {
        NativeCursor.Update(Window, Owner->GetWorld(), Path, Pointer(), FrameRate);
    }
}
