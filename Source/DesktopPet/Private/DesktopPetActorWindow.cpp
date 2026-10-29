#include "DesktopPetActor.h"
#include "DesktopPetRuntime.h"

/** 尺寸、位置和质量的快捷节点最终仍调用同一个 PetApplyRuntimeConfig。 */
void ADesktopPetActor::PetSetDisplaySize(FIntPoint Size)
{
    auto C = PetGetRuntimeConfig();
    C.Normalize();
    // 先限定物理请求，避免极端输入在反算逻辑画布时溢出。
    Size.X = FMath::Clamp(Size.X, C.MinWindowSize.X, C.MaxWindowSize.X);
    Size.Y = FMath::Clamp(Size.Y, C.MinWindowSize.Y, C.MaxWindowSize.Y);
    C.WindowSize = FIntPoint(FMath::RoundToInt(Size.X / C.DisplayScale), FMath::RoundToInt(Size.Y / C.DisplayScale));
    PetApplyRuntimeConfig(C);
}

FIntPoint ADesktopPetActor::PetGetDisplaySize() const
{
    return Runtime && !bConfigPending ? Runtime->DisplaySize : PetGetRuntimeConfig().GetDisplaySize();
}

/** 读真实 HWND 坐标，使拖拽之后再改质量不会把窗口跳回初始位置。 */
FIntPoint ADesktopPetActor::PetGetWindowPosition() const
{
    if (!Runtime)
    {
        return PetGetRuntimeConfig().WindowPosition;
    }
    FPetDpiScope Scope;
    RECT R{};
    GetWindowRect(Runtime->Window, &R);
    return FIntPoint(R.left, R.top);
}

/** 手动拖拽接口可直接连接 Enhanced Input 的 Started / Completed。 */
bool ADesktopPetActor::PetBeginWindowDrag()
{
    return Runtime && Runtime->BeginDrag();
}

void ADesktopPetActor::PetEndWindowDrag()
{
    if (Runtime)
    {
        Runtime->FinishDrag();
    }
}

bool ADesktopPetActor::PetIsInputKeyDown(FKey Key) const
{
    if (!IsInGameThread())
    {
        return false;
    }
    const auto Current = Runtime;
    if (!Current || !Current->IsActive() || !Current->Config.bTransparentWindowEnabled || Current->Shell.IsInTray() ||
        !IsWindowVisible(Current->Window) || !IsWindowEnabled(Current->Window) || IsIconic(Current->Window))
    {
        return false;
    }
    const uint32 VirtualKey = FDesktopPetInputState::GetVirtualKey(Key);
    if (VirtualKey == 0 || VirtualKey > 255)
    {
        return false;
    }
    const bool bPhysicallyDown = (GetAsyncKeyState(FDesktopPetInputState::GetPhysicalKey(VirtualKey)) & 0x8000) != 0;
    return Current->InputState.IsKeyDown(VirtualKey, Key.IsMouseButton() ? false : PetHasFocus(), bPhysicallyDown);
}

bool ADesktopPetActor::PetIsWindowDragging() const
{
    return Runtime && Runtime->Dragging;
}

void* ADesktopPetActor::GetNativeWindowHandle() const
{
    return Runtime ? Runtime->Window : nullptr;
}

/** 显式请求一次原生焦点；不更改启动、托盘、置顶、穿透或项目输入策略。 */
bool ADesktopPetActor::PetRequestFocus()
{
    if (!IsInGameThread())
    {
        return false;
    }
    // 激活消息可能同步进入窗口回调，保留运行时并在消息之后检查归属。
    const auto Current = Runtime;
    if (!Current || !Current->IsActive() || !Current->Config.bTransparentWindowEnabled || Current->Shell.IsInTray())
    {
        return false;
    }
    const HWND PetWindow = Current->Window;
    if (!IsWindow(PetWindow) || !IsWindowVisible(PetWindow) || !IsWindowEnabled(PetWindow) ||
        GetWindowThreadProcessId(PetWindow, nullptr) != GetCurrentThreadId())
    {
        return false;
    }
    if (IsIconic(PetWindow))
    {
        ShowWindow(PetWindow, SW_RESTORE);
    }
    if (!Current->IsActive())
    {
        return false;
    }
    if (GetForegroundWindow() != PetWindow)
    {
        SetForegroundWindow(PetWindow);
    }
    if (!Current->IsActive() || GetForegroundWindow() != PetWindow)
    {
        return false;
    }
    ::SetFocus(PetWindow);
    // SetFocus 返回的是旧焦点；以实际状态判断成功，避免把旧焦点为空当成失败。
    return Current->IsActive() && PetHasFocus();
}

bool ADesktopPetActor::PetHasFocus() const
{
    if (!IsInGameThread())
    {
        return false;
    }
    const auto Current = Runtime;
    if (!Current || !Current->IsActive() || !Current->Config.bTransparentWindowEnabled || Current->Shell.IsInTray())
    {
        return false;
    }
    const HWND PetWindow = Current->Window;
    return IsWindow(PetWindow) && IsWindowVisible(PetWindow) && IsWindowEnabled(PetWindow) && !IsIconic(PetWindow) &&
           GetForegroundWindow() == PetWindow && ::GetFocus() == PetWindow;
}

/** 运行时更新 WindowSize；保留其他实例参数。 */
void ADesktopPetActor::PetSetWindowSize(FIntPoint Value)
{
    auto C = PetGetRuntimeConfig();
    C.WindowSize = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 WindowPosition；保留其他实例参数。 */
void ADesktopPetActor::PetSetWindowPosition(FIntPoint Value)
{
    auto C = PetGetRuntimeConfig();
    C.WindowPosition = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 DisplayScale；保留其他实例参数。 */
void ADesktopPetActor::PetSetDisplayScale(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.DisplayScale = Value;
    PetApplyRuntimeConfig(C);
}

/** 单项接口同样经过统一校验，不绕过尺寸上限。 */
void ADesktopPetActor::PetSetMinDisplayScale(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.MinDisplayScale = Value;
    PetApplyRuntimeConfig(C);
}

/** 单项接口同样经过统一校验，不绕过尺寸上限。 */
void ADesktopPetActor::PetSetMaxDisplayScale(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.MaxDisplayScale = Value;
    PetApplyRuntimeConfig(C);
}

/** 单项接口同样经过统一校验，不绕过尺寸上限。 */
void ADesktopPetActor::PetSetMinWindowSize(FIntPoint Value)
{
    auto C = PetGetRuntimeConfig();
    C.MinWindowSize = Value;
    PetApplyRuntimeConfig(C);
}

/** 单项接口同样经过统一校验，不绕过尺寸上限。 */
void ADesktopPetActor::PetSetMaxWindowSize(FIntPoint Value)
{
    auto C = PetGetRuntimeConfig();
    C.MaxWindowSize = Value;
    PetApplyRuntimeConfig(C);
}

/** 成组设置可原子地更换两个边界；正在运行的滚轮动画随下一 Tick 的配置更新结束。 */
void ADesktopPetActor::PetSetDisplayScaleLimits(float Minimum, float Maximum)
{
    auto C = PetGetRuntimeConfig();
    C.MinDisplayScale = Minimum;
    C.MaxDisplayScale = Maximum;
    PetApplyRuntimeConfig(C);
}

/** 最终窗口尺寸约束独立于 UI、角色与具体渲染方式。 */
void ADesktopPetActor::PetSetWindowSizeLimits(FIntPoint Minimum, FIntPoint Maximum)
{
    auto C = PetGetRuntimeConfig();
    C.MinWindowSize = Minimum;
    C.MaxWindowSize = Maximum;
    PetApplyRuntimeConfig(C);
}

/** 提供有效范围，蓝图滑条可以直接使用这两个输出，避免显示不可达到的倍率。 */
void ADesktopPetActor::PetGetEffectiveDisplayScaleLimits(float& Minimum, float& Maximum) const
{
    const FVector2D Range = PetGetRuntimeConfig().GetEffectiveDisplayScaleRange();
    Minimum = float(Range.X);
    Maximum = float(Range.Y);
}

/** 运行时只替换系统窗口身份，其他实例参数及配置应用时序保持一致。 */
void ADesktopPetActor::PetSetWindowMode(EDesktopPetWindowMode Mode)
{
    auto C = PetGetRuntimeConfig();
    C.WindowMode = Mode;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 bAlwaysOnTop；保留其他实例参数。 */
void ADesktopPetActor::PetSetAlwaysOnTop(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bAlwaysOnTop = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 bHideGameWindow；保留其他实例参数。 */
void ADesktopPetActor::PetSetHideGameWindow(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bHideGameWindow = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 bDraggable；保留其他实例参数。 */
void ADesktopPetActor::PetSetDraggable(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bDraggable = Value;
    PetApplyRuntimeConfig(C);
}

/** 原生滚轮与增强输入共用同一入口、命中规则和物理屏幕坐标，不需要再连接缩放 Setter。 */
bool ADesktopPetActor::PetZoomAtScreenPosition(float WheelDelta, FVector2D ScreenPosition)
{
    return Runtime && Runtime->ZoomAtScreenPosition(WheelDelta, ScreenPosition);
}

bool ADesktopPetActor::PetIsZooming() const
{
    return Runtime && Runtime->Zooming;
}

float ADesktopPetActor::PetGetZoomTargetScale() const
{
    return Runtime && Runtime->Zooming ? Runtime->ZoomTargetScale : PetGetDisplayScale();
}

void ADesktopPetActor::PetSetZoomAnimationSeconds(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.ZoomAnimationSeconds = Value;
    PetApplyRuntimeConfig(C);
}

/** 新接口仍共用整组配置路径，不在 UMG 回调内重建渲染资源。 */
void ADesktopPetActor::PetSetShowInTaskbar(bool bShow)
{
    PetSetWindowMode(bShow ? EDesktopPetWindowMode::Application : EDesktopPetWindowMode::ToolWindow);
}

void ADesktopPetActor::PetSetAllowScreenCapture(bool bAllow)
{
    auto C = PetGetRuntimeConfig();
    C.bAllowScreenCapture = bAllow;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetTrayFrameRate(int32 FPS)
{
    auto C = PetGetRuntimeConfig();
    C.TrayFrameRate = FPS;
    PetApplyRuntimeConfig(C);
}

bool ADesktopPetActor::PetIsHiddenToTray() const
{
    return Runtime && Runtime->Shell.IsInTray();
}

/** 托盘隐藏不销毁 Actor、角色、UI 或相机；先获得可用恢复入口，再释放输入。 */
bool ADesktopPetActor::PetHideToTray()
{
    const auto Current = Runtime;
    if (!Current || bStopping || !Current->Config.bTransparentWindowEnabled)
    {
        return false;
    }
    if (Current->Shell.IsInTray())
    {
        return true;
    }
    if (!Current->Shell.HideToTray())
    {
        return false;
    }
    Current->NativeCursor.Release();
    Current->FinishZoom();
    Current->ReleaseHeldInput();
    if (Runtime != Current || !Current->Shell.IsInTray())
    {
        return false;
    }
    PetCompleteConfigWidgetClose();
    if (Runtime != Current || !Current->Shell.IsInTray())
    {
        return false;
    }
    UpdateHoveredActor(nullptr);
    if (Runtime != Current || !Current->Shell.IsInTray())
    {
        return false;
    }
    FDesktopPetFramePacing::Update(this, Current->Config, true);
    PetTrayStateChanged.Broadcast(true);
    if (Runtime == Current && Current->Shell.IsInTray())
    {
        PetEventTrayStateChanged(true);
    }
    return Runtime == Current && Current->Shell.IsInTray();
}

/** 首次恢复立刻安排新捕获，上一张有效图像保留到新帧完成，不闪回 UE 游戏窗口。 */
bool ADesktopPetActor::PetRestoreFromTray()
{
    const auto Current = Runtime;
    if (!Current || bStopping)
    {
        return false;
    }
    if (!Current->Shell.RestoreFromTray(Current->Config.bTransparentWindowEnabled))
    {
        return false;
    }
    Current->NextCaptureTime = 0.;
    FDesktopPetFramePacing::Update(this, Current->Config, false);
    PetTrayStateChanged.Broadcast(false);
    if (Runtime == Current && !Current->Shell.IsInTray())
    {
        PetEventTrayStateChanged(false);
    }
    return Runtime == Current && !Current->Shell.IsInTray();
}

/** 所有快捷节点汇总到同一配置应用入口，便于蓝图做设置面板和 SaveGame。 */
void ADesktopPetActor::PetSetTransparentWindowEnabled(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bTransparentWindowEnabled = Value;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetWheelZoomEnabled(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bEnableWheelZoom = Value;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetCenterAnchoredScaling(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bCenterAnchoredScaling = Value;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetWheelZoomStep(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.WheelZoomStep = Value;
    PetApplyRuntimeConfig(C);
}
