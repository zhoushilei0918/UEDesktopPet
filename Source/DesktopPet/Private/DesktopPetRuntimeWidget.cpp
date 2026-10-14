#include "DesktopPetRuntime.h"

/* UMG 图层与动画时钟；执行顺序保持在原有成员函数中。 */

void FDesktopPetRuntime::MarkPetWidgetAnimationTicks(float)
{
    NativeCursor.TickAnimations();
    if (!HasUI || Shell.IsInTray() || !Config.bTransparentWindowEnabled || !IsValid(Owner))
    {
        return;
    }
    const auto MarkWidget = [](UUserWidget* Widget)
    {
        if (IsValid(Widget) && Widget->IsConstructed() && Widget->IsVisible() && Widget->IsAnyAnimationPlaying())
        {
            UUMGSequenceTickManager::Get(Widget)->OnWidgetTicked(Widget);
        }
    };
    const auto MarkTree = [&MarkWidget](UUserWidget* Root)
    {
        if (!IsValid(Root) || !Root->IsConstructed() || !Root->IsVisible())
        {
            return;
        }
        MarkWidget(Root);
        if (Root->WidgetTree)
        {
            Root->WidgetTree->ForEachWidgetAndDescendants(
                [&MarkWidget](UWidget* Child)
                {
                    if (UUserWidget* Nested = Cast<UUserWidget>(Child))
                    {
                        // 隐藏容器中的子 UMG 保持暂停；普通静态控件不注册动画时钟。
                        for (UWidget* Parent = Nested->GetParent(); Parent; Parent = Parent->GetParent())
                        {
                            if (!Parent->IsVisible())
                            {
                                return;
                            }
                        }
                        MarkWidget(Nested);
                    }
                });
        }
    };
    MarkTree(Owner->OverlayWidget);
    if (Owner->bConfigWidgetOpen)
    {
        MarkTree(Owner->ConfigWidget);
    }
    // 这里只申报动画仍可更新，不手动调用 NativeTick，也不额外推进时间或增加场景捕获。
}

void FDesktopPetRuntime::SetWidget()
{
    HasUI = Owner->OverlayWidget || (Owner->bConfigWidgetOpen && Owner->ConfigWidget);
    if (!HasUI)
    {
        VirtualWindow->GetHittestGrid().Clear();
    }
    TSharedRef<SOverlay> Layers = SNew(SOverlay);
    if (Owner->OverlayWidget)
    {
        Layers->AddSlot()[Owner->OverlayWidget->TakeWidget()];
    }
    if (Owner->bConfigWidgetOpen && Owner->ConfigWidget)
    {
        Layers->AddSlot()[Owner->ConfigWidget->TakeWidget()];
    }
    VirtualWindow->SetContent(Layers);
}
