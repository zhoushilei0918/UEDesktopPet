#pragma once

#include "CoreMinimal.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SVirtualWindow.h"

/** Preserve the cursor's original layout and anchor while allocating space for transformed descendants. */
namespace DesktopPetCursorLayout
{
    struct FLayout
    {
        FIntPoint Size = FIntPoint::ZeroValue;
        FIntPoint HotSpot = FIntPoint::ZeroValue;
        FGeometry Geometry;
        FSlateRect ClipRect;
        float Scale = 1.f;
    };

    inline bool IsFinite(const FSlateRect& Rect)
    {
        return FMath::IsFinite(Rect.Left) && FMath::IsFinite(Rect.Top) &&
               FMath::IsFinite(Rect.Right) && FMath::IsFinite(Rect.Bottom);
    }

    inline void IncludeChildren(const SWidget& Widget, const FGeometry& Geometry, FSlateRect& Bounds)
    {
        const FSlateRect Rect = Geometry.GetRenderBoundingRect(Widget.GetCullingBoundsExtension());
        if (IsFinite(Rect))
        {
            Bounds = Bounds.Expand(Rect);
        }
        FArrangedChildren Children(EVisibility::Visible);
        Widget.ArrangeChildren(Geometry, Children);
        for (const FArrangedWidget& Child : Children.GetInternalArray())
        {
            IncludeChildren(*Child.Widget, Child.Geometry, Bounds);
        }
    }

    inline FLayout Measure(const TSharedRef<SVirtualWindow>& Window, FVector2D Desired,
                           float Padding, FSlateRect& RetainedBounds)
    {
        Window->Resize(Desired);
        Window->SlatePrepass(1.f);
        FSlateRect Bounds(0, 0, float(Desired.X), float(Desired.Y));
        IncludeChildren(*Window, FGeometry::MakeRoot(Desired, FSlateLayoutTransform()), Bounds);
        Padding = FMath::IsFinite(Padding) ? FMath::Clamp(Padding, 0.f, 1024.f) : 64.f;
        Bounds = Bounds.ExtendBy(FMargin(Padding));
        if (Padding > 0.f)
        {
            // Grow in small blocks instead of reallocating the render target for every animation pixel.
            Bounds.Left = FMath::FloorToFloat(Bounds.Left / 32.f) * 32.f;
            Bounds.Top = FMath::FloorToFloat(Bounds.Top / 32.f) * 32.f;
            Bounds.Right = FMath::CeilToFloat(Bounds.Right / 32.f) * 32.f;
            Bounds.Bottom = FMath::CeilToFloat(Bounds.Bottom / 32.f) * 32.f;
        }
        // Animations may expand the painted area. Keep the allocation stable when they contract again.
        if (RetainedBounds.IsValid())
        {
            Bounds = Bounds.Expand(RetainedBounds);
        }
        RetainedBounds = Bounds;
        FLayout Result;
        const FVector2D Extent = Bounds.GetSize();
        // Resource bound scales the complete image instead of cutting off an edge.
        constexpr int32 MaxTextureSize = 4096;
        Result.Scale = float(FMath::Min(1., (MaxTextureSize - 2.) / FMath::Max(Extent.X, Extent.Y)));
        const FIntPoint Min(FMath::FloorToInt(Bounds.Left * Result.Scale),
                            FMath::FloorToInt(Bounds.Top * Result.Scale));
        const FIntPoint Max(FMath::CeilToInt(Bounds.Right * Result.Scale),
                            FMath::CeilToInt(Bounds.Bottom * Result.Scale));
        Result.Size = Max - Min;
        const FIntPoint OriginalSize(FMath::Max(1, FMath::CeilToInt(Desired.X * Result.Scale)),
                                     FMath::Max(1, FMath::CeilToInt(Desired.Y * Result.Scale)));
        Result.HotSpot = FIntPoint(FMath::Clamp(FMath::RoundToInt(OriginalSize.X * .5f), 0, OriginalSize.X - 1),
                                   FMath::Clamp(FMath::RoundToInt(OriginalSize.Y * .5f), 0, OriginalSize.Y - 1)) - Min;
        Result.Geometry = FGeometry::MakeRoot(Desired, FSlateLayoutTransform(Result.Scale, FVector2D(-Min.X, -Min.Y)));
        Result.ClipRect = FSlateRect(0, 0, float(Result.Size.X), float(Result.Size.Y));
        return Result;
    }
}
