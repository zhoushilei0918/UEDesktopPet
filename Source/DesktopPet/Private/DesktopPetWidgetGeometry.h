#pragma once

#include "Input/HittestGrid.h"
#include "Misc/App.h"
#include "Slate/WidgetRenderer.h"
#include "Types/PaintArgs.h"

/** 离屏纹理仍从原点绘制，UMG 的 Tick / Cached Geometry 则使用桌宠所在的桌面坐标。 */
namespace DesktopPetWidgetGeometry
{
    inline void DrawWindow(FWidgetRenderer& Renderer,
                           UTextureRenderTarget2D* Target,
                           const TSharedRef<SVirtualWindow>& Window,
                           FVector2D DesktopOrigin,
                           FVector2D DrawSize,
                           float Scale,
                           float DeltaTime)
    {
        FHittestGrid& Grid = Window->GetHittestGrid();
        const FGeometry Geometry = FGeometry::MakeRoot(DrawSize / Scale, FSlateLayoutTransform(Scale));
        // 通过 Slate 提供的窗口到桌面偏移传播给所有子控件，不移动隐藏的游戏窗口。
        const FPaintArgs PaintArgs(nullptr, Grid, DesktopOrigin, FApp::GetCurrentTime(), DeltaTime);
        Renderer.DrawWindow(PaintArgs, Target, Window, Geometry, Geometry.GetLayoutBoundingRect(), DeltaTime, false);
        // DrawWindow 内部先按纹理原点构建网格。绘制后只移动网格的桌面原点，
        // 不改变尺寸或重建内容，使命中路径与 Cached Geometry 使用同一坐标系。
        Grid.SetHittestArea(DesktopOrigin, Geometry.GetLayoutBoundingRect().GetSize());
    }
}
