#pragma once
#include "CoreMinimal.h"

/** 在已有窗口位图最上层混合光标；只缓存光标覆盖的小矩形，不分配第二张窗口图像。 */
class FDesktopPetCursorSurface
{
public:
    /** 上一次光标的有效矩形；恢复前保留它可用于计算系统提交的脏区域。 */
    FIntRect GetRect() const
    {
        return SavedRect;
    }

    /** 读取未包含光标的窗口 Alpha，防止光标自身让透明像素变成可点击区域。 */
    uint8 GetBaseAlpha(const FColor* Pixels, FIntPoint Size, FIntPoint Point) const
    {
        if (!Pixels || Point.X < 0 || Point.Y < 0 || Point.X >= Size.X || Point.Y >= Size.Y)
        {
            return 0;
        }
        if (Size == SavedSize && SavedRect.Contains(Point))
        {
            return Background[(Point.Y - SavedRect.Min.Y) * SavedRect.Width() + Point.X - SavedRect.Min.X].A;
        }
        return Pixels[Point.Y * Size.X + Point.X].A;
    }

    /** 擦除上次光标，精确恢复原始人物/UI 像素；移动、离开、切换模式时不会留下拖影。 */
    void Restore(FColor* Pixels, FIntPoint Size)
    {
        if (Pixels && Size == SavedSize)
        {
            for (int32 Y = SavedRect.Min.Y; Y < SavedRect.Max.Y; ++Y)
            {
                FMemory::Memcpy(Pixels + Y * Size.X + SavedRect.Min.X,
                                Background.GetData() + (Y - SavedRect.Min.Y) * SavedRect.Width(),
                                SavedRect.Width() * sizeof(FColor));
            }
        }
        SavedRect = FIntRect(0, 0, 0, 0);
    }

    /**
     * 在窗口最上层混合预乘光标，保留原 Alpha，不重复处理 Gamma。
     * Pixels - 读写的窗口 BGRA 缓冲区；
     * Size - 窗口物理像素尺寸；
     * Cursor - 已预乘的光标图像；
     * CursorSize - 光标图像尺寸；
     * Origin - 光标图像左上角相对窗口的位置。
     */
    void Draw(FColor* Pixels, FIntPoint Size, const TArray<FColor>* Cursor, FIntPoint CursorSize, FIntPoint Origin)
    {
        if (!Pixels || !Cursor || CursorSize.X < 1 || CursorSize.Y < 1 || Cursor->Num() != CursorSize.X * CursorSize.Y)
        {
            return;
        }
        const FIntRect Rect(FMath::Clamp(Origin.X, 0, Size.X),
                            FMath::Clamp(Origin.Y, 0, Size.Y),
                            FMath::Clamp(Origin.X + CursorSize.X, 0, Size.X),
                            FMath::Clamp(Origin.Y + CursorSize.Y, 0, Size.Y));
        if (Rect.Width() <= 0 || Rect.Height() <= 0)
        {
            return;
        }
        SavedRect = Rect;
        SavedSize = Size;
        Background.SetNumUninitialized(Rect.Width() * Rect.Height());
        for (int32 Y = Rect.Min.Y; Y < Rect.Max.Y; ++Y)
        {
            for (int32 X = Rect.Min.X; X < Rect.Max.X; ++X)
            {
                FColor& D = Pixels[Y * Size.X + X];
                Background[(Y - Rect.Min.Y) * Rect.Width() + X - Rect.Min.X] = D;
                const FColor S = (*Cursor)[(Y - Origin.Y) * CursorSize.X + X - Origin.X];
                const uint32 Remain = 255 - S.A;
                const auto Blend = [Remain](uint8 Source, uint8 Dest)
                {
                    return uint8(FMath::Min(255u, uint32(Source) + (uint32(Dest) * Remain + 127) / 255));
                };
                D = FColor(Blend(S.R, D.R), Blend(S.G, D.G), Blend(S.B, D.B), Blend(S.A, D.A));
            }
        }
    }

private:
    /** 背景保存为预乘字节，恢复时不经过任何颜色或透明度运算。 */
    TArray<FColor> Background;
    FIntRect SavedRect = FIntRect(0, 0, 0, 0);
    FIntPoint SavedSize = FIntPoint::ZeroValue;
};
