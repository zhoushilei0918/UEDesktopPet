#include "DesktopPetRuntime.h"

/* 窗口呈现、光标叠加与平滑缩放；执行顺序保持在原有成员函数中。 */

bool FDesktopPetRuntime::GetCursorFrame(FIntPoint Position, FDesktopPetCursorFrame& Frame, FIntPoint& Origin) const
{
    POINT Point{};
    if (!Config.bTransparentWindowEnabled || Shell.IsInTray() || !NativeCursor.GetCompositeFrame(Frame) ||
        !GetCursorPos(&Point))
    {
        return false;
    }
    Origin = FIntPoint(Point.x - Position.X, Point.y - Position.Y) - Frame.HotSpot;
    return true;
}

bool FDesktopPetRuntime::CursorChanged(bool Visible, const FDesktopPetCursorFrame& Frame, FIntPoint Origin) const
{
    return Visible != CursorPresented ||
           (Visible && (Origin != CursorOrigin || Frame.Size != CursorSize || Frame.Revision != CursorRevision));
}

bool FDesktopPetRuntime::Present(FIntPoint Size, FIntPoint Position)
{
    if (CapturedPixels.IsEmpty())
    {
        return false;
    }
    FPetDpiScope DpiScope;
    FDesktopPetCursorFrame CursorFrame;
    FIntPoint NewOrigin = FIntPoint::ZeroValue;
    const bool Visible = GetCursorFrame(Position, CursorFrame, NewOrigin);
    const bool ChangedCursor = CursorChanged(Visible, CursorFrame, NewOrigin);
    const FIntRect OldCursorRect = CursorSurface.GetRect();
    // 比较与缩放前先精确擦除旧光标，避免光标进入下一帧背景或被重复混合。
    CursorSurface.Restore(static_cast<FColor*>(Bits), BitmapSize);
    const bool Resized = BitmapSize != Size;
    const bool ChangedBase =
        Resized || (bFrameNeedsPresentation &&
                    (CapturedSize != Size ||
                     FMemory::Memcmp(Bits, CapturedPixels.GetData(), CapturedPixels.Num() * sizeof(FColor)) != 0));
    RECT Current{};
    GetWindowRect(Window, &Current);
    const bool Moved = Current.left != Position.X || Current.top != Position.Y;
    if (Resized)
    {
        BITMAPINFO Info{};
        Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Info.bmiHeader.biWidth = Size.X;
        Info.bmiHeader.biHeight = -Size.Y;
        Info.bmiHeader.biPlanes = 1;
        Info.bmiHeader.biBitCount = 32;
        Info.bmiHeader.biCompression = BI_RGB;
        void* NewBits = nullptr;
        HBITMAP NewBitmap = CreateDIBSection(MemoryDC, &Info, DIB_RGB_COLORS, &NewBits, nullptr, 0);
        if (!NewBitmap || !NewBits)
        {
            if (NewBitmap)
            {
                DeleteObject(NewBitmap);
            }
            return false;
        }
        SelectObject(MemoryDC, NewBitmap);
        DeleteObject(Bitmap);
        Bitmap = NewBitmap;
        Bits = NewBits;
        BitmapSize = Size;
    }
    if (ChangedBase)
    {
        DesktopPetPresentation::Resample(
            CapturedPixels, CapturedAlpha, CapturedSize, Size, static_cast<FColor*>(Bits), SceneAlpha);
    }
    else if (bFrameNeedsPresentation)
    {
        SceneAlpha = CapturedAlpha;
    }
    // 最后叠加，不乘人物覆盖率，也不经过场景曝光、后处理或第二次 Gamma。
    if (Visible)
    {
        CursorSurface.Draw(static_cast<FColor*>(Bits), Size, CursorFrame.Pixels, CursorFrame.Size, NewOrigin);
    }
    if (!ChangedBase && !Moved && !ChangedCursor)
    {
        bFrameNeedsPresentation = false;
        return true;
    }
    SIZE NativeSize{Size.X, Size.Y};
    POINT Destination{Position.X, Position.Y}, Origin{0, 0};
    BLENDFUNCTION Blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    const bool CursorOnly = !ChangedBase && !Moved && !Resized;
    const FIntRect NewCursorRect = CursorSurface.GetRect();
    FIntRect Dirty = OldCursorRect.Area() > 0 ? OldCursorRect : NewCursorRect;
    if (NewCursorRect.Area() > 0)
    {
        Dirty.Union(NewCursorRect);
    }
    RECT DirtyRect{Dirty.Min.X, Dirty.Min.Y, Dirty.Max.X, Dirty.Max.Y};
    UPDATELAYEREDWINDOWINFO Info{};
    Info.cbSize = sizeof(Info);
    Info.pptDst = &Destination;
    Info.psize = &NativeSize;
    Info.hdcSrc = MemoryDC;
    Info.pptSrc = &Origin;
    Info.pblend = &Blend;
    Info.dwFlags = ULW_ALPHA;
    // 仅移动光标时提交新旧位置的并集，不增加场景捕获、整帧缩放或第二张窗口位图。
    if (CursorOnly && Dirty.Area() > 0)
    {
        Info.prcDirty = &DirtyRect;
    }
    if (!UpdateLayeredWindowIndirect(Window, &Info))
    {
        UE_LOG(LogDesktopPet, Error, TEXT("UpdateLayeredWindowIndirect failed: %u"), GetLastError());
        CloseRequested = true;
        return false;
    }
    CursorPresented = Visible;
    CursorOrigin = NewOrigin;
    CursorSize = CursorFrame.Size;
    CursorRevision = CursorFrame.Revision;
    LastCursorPresentation = FPlatformTime::Seconds();
    DisplaySize = Size;
    bFrameNeedsPresentation = false;
    return true;
}

bool FDesktopPetRuntime::ZoomAtScreenPosition(float Delta, FVector2D Screen)
{
    if (!Config.bEnableWheelZoom || Dragging || OrbitDragging || UIPressed || !FMath::IsFinite(Delta) || Delta == 0.f ||
        !FMath::IsFinite(Screen.X) || !FMath::IsFinite(Screen.Y))
    {
        return false;
    }
    FPetDpiScope DpiScope;
    RECT R{};
    GetWindowRect(Window, &R);
    const FVector2D Local = Screen - FVector2D(R.left, R.top);
    if (!PickActor(Local) || CapturedPixels.IsEmpty())
    {
        return false;
    }
    FDesktopPetConfig Target = Config;
    Target.DisplayScale = (Zooming ? ZoomTargetScale : PresentedScale) *
                          FMath::Pow(1.f + Config.WheelZoomStep, FMath::Clamp(Delta, -64.f, 64.f));
    Target.Normalize();
    // 到达任一边界后不继续积累不可显示的倍率；反向滚动可立即响应。
    if (FMath::IsNearlyEqual(Target.DisplayScale, Zooming ? ZoomTargetScale : PresentedScale))
    {
        return false;
    }
    // 连续滚动且鼠标未移动时保留原始 UV，避免窗口整数取整误差逐次累积。
    const FVector2D Expected = ZoomScreen + FVector2D(.5, .5) - ZoomUV * FVector2D(DisplaySize);
    const bool Moved = FMath::Abs(Expected.X - R.left) > .501 || FMath::Abs(Expected.Y - R.top) > .501;
    if (!ZoomAnchorValid || !ZoomScreen.Equals(Screen, 0.001) || Moved)
    {
        ZoomScreen = Screen;
        ZoomUV = FVector2D((Local.X + .5) / DisplaySize.X, (Local.Y + .5) / DisplaySize.Y);
        ZoomAnchorValid = true;
    }
    ZoomStartScale = PresentedScale;
    ZoomTargetScale = Target.DisplayScale;
    ZoomStarted = FPlatformTime::Seconds();
    Zooming = true;
    return true;
}

void FDesktopPetRuntime::FinishZoom()
{
    if (!Zooming)
    {
        return;
    }
    Zooming = false;
    ZoomTargetScale = PresentedScale;
    Owner->DesiredConfig = Config;
    Owner->DesiredConfig.DisplayScale = PresentedScale;
    Owner->DesiredConfig.WindowPosition = Owner->PetGetWindowPosition();
    Owner->bHasConfig = true;
    Owner->bConfigPending = true;
    Owner->bCenterAnchorValid = false;
    ZoomCommitPending = true;
}

void FDesktopPetRuntime::UpdateZoom()
{
    if (!Zooming)
    {
        return;
    }
    if (Owner->bConfigWidgetOpen)
    {
        FinishZoom();
        return;
    }
    const double T = Config.ZoomAnimationSeconds > 0.f ?
                         FMath::Clamp((FPlatformTime::Seconds() - ZoomStarted) / Config.ZoomAnimationSeconds, 0., 1.) :
                         1.;
    // 显示动画最多 60 Hz；场景继续遵守 TargetFrameRate，避免空闲高 Tick 频率浪费 CPU。
    const double Now = FPlatformTime::Seconds();
    if (T < 1. && Now - LastZoomPresentation < 1. / 60.)
    {
        return;
    }
    LastZoomPresentation = Now;
    const double Ease = T * T * (3. - 2. * T);
    FDesktopPetConfig C = Config;
    C.DisplayScale =
        FMath::Exp(FMath::Lerp(FMath::Loge(double(ZoomStartScale)), FMath::Loge(double(ZoomTargetScale)), Ease));
    // 动画中的每一帧也遵守限制，不能只钳制结束时的 RT 尺寸。
    const FVector2D Limits = C.GetEffectiveDisplayScaleRange();
    C.DisplayScale = FMath::Clamp(C.DisplayScale, float(Limits.X), float(Limits.Y));
    const FIntPoint Size = C.GetDisplaySize();
    const FVector2D P = ZoomScreen + FVector2D(.5, .5) - ZoomUV * FVector2D(Size);
    const FIntPoint Position(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y));
    if (Size == DisplaySize || Present(Size, Position))
    {
        PresentedScale = C.DisplayScale;
    }
    if (T >= 1.)
    {
        FinishZoom();
    }
}

void FDesktopPetRuntime::PresentLatestFrame()
{
    if (Shell.IsInTray() || !Config.bTransparentWindowEnabled)
    {
        return;
    }
    FPetDpiScope DpiScope;
    RECT Rect{};
    GetWindowRect(Window, &Rect);
    const FIntPoint Position(Rect.left, Rect.top);
    if (!bFrameNeedsPresentation)
    {
        FDesktopPetCursorFrame Frame;
        FIntPoint Origin = FIntPoint::ZeroValue;
        const bool Visible = GetCursorFrame(Position, Frame, Origin);
        if (!CursorChanged(Visible, Frame, Origin))
        {
            return;
        }
        // 移出窗口立即擦除；移动最多 60 Hz，静止时完全不增加窗口提交。
        if (Visible && FPlatformTime::Seconds() - LastCursorPresentation < 1. / 60.)
        {
            return;
        }
    }
    Present(DisplaySize, Position);
}
