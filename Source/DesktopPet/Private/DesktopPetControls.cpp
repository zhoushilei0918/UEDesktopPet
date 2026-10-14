#include "DesktopPetActor.h"
#include "DesktopPetMenuInterface.h"
#include "DesktopPetRuntime.h"

/** UI 更换在下一 Tick 执行，避免正在分发 OnClicked 时释放其 Slate 路径。 */
void ADesktopPetActor::PetSetOverlayWidget(UUserWidget* Widget)
{
    OverlayWidget = Widget;
    bWidgetPending = true;
}

void ADesktopPetActor::PetSetInteractionTiming(float Delay, float Seconds)
{
    auto C = PetGetRuntimeConfig();
    C.MenuCloseDelay = Delay;
    C.MenuAnimationSeconds = Seconds;
    PetApplyRuntimeConfig(C);
}

/** 动作总线不硬编码退出、聊天、换动作等项目行为。 */
void ADesktopPetActor::PetDispatchAction(FName Action)
{
    PetAction.Broadcast(Action);
}

float ADesktopPetActor::PetGetInteractionProgress() const
{
    return Runtime ? Runtime->OpenAmount : 0.f;
}

/** 菜单与常驻 Overlay 分开，关闭菜单不会丢失项目已有 UI。 */
void ADesktopPetActor::PetSetConfigWidgetClass(TSubclassOf<UUserWidget> Class)
{
    PetCompleteConfigWidgetClose();
    ConfigWidget = nullptr;
    ConfigWidgetClass = Class;
}

bool ADesktopPetActor::PetOpenConfigWidget()
{
    if (!Runtime || bStopping || Runtime->Shell.IsInTray() || !ConfigWidgetClass)
    {
        return false;
    }
    if (bConfigWidgetOpen)
    {
        return true;
    }
    if (!ConfigWidget)
    {
        ConfigWidget = CreateWidget<UUserWidget>(GetWorld(), ConfigWidgetClass);
    }
    if (!ConfigWidget)
    {
        return false;
    }
    Runtime->ReleaseHeldInput();
    UpdateHoveredActor(nullptr);
    if (!Runtime || bStopping)
    {
        return false;
    }
    bConfigWidgetOpen = true;
    bWidgetPending = true;
    PetConfigWidgetVisibilityChanged.Broadcast(true);
    return bConfigWidgetOpen;
}

void ADesktopPetActor::PetCloseConfigWidget()
{
    if (!bConfigWidgetOpen)
    {
        return;
    }
    // 可选协议只负责把关闭请求交给 UMG；动画内容和结束时机仍完全由项目蓝图控制。
    if (!bStopping && IsValid(ConfigWidget) && ConfigWidget->Implements<UDesktopPetMenuInterface>() &&
        IDesktopPetMenuInterface::Execute_PetRequestMenuClose(ConfigWidget))
    {
        return;
    }
    PetCompleteConfigWidgetClose();
}

void ADesktopPetActor::PetCompleteConfigWidgetClose()
{
    if (!bConfigWidgetOpen)
    {
        return;
    }
    bConfigWidgetOpen = false;
    bWidgetPending = true;
    if (Runtime)
    {
        Runtime->ReleaseHeldInput();
    }
    PetConfigWidgetVisibilityChanged.Broadcast(false);
}
