#include "DesktopPetSettings.h"

namespace
{
    // 只负责纯尺寸计算，不访问角色、UI 或渲染器；所有外部入口共享这一套约束。
    struct FPetResolvedSizeLimits
    {
        FIntPoint Logical, Minimum, Maximum;
        float RequestedMinimum, RequestedMaximum;
        double EffectiveMinimum, EffectiveMaximum;
    };
    FPetResolvedSizeLimits ResolveSizeLimits(const FDesktopPetConfig& C)
    {
        FPetResolvedSizeLimits R;
        R.Logical = FIntPoint(FMath::Clamp(C.WindowSize.X, 64, 2048), FMath::Clamp(C.WindowSize.Y, 64, 2048));
        R.Minimum = FIntPoint(FMath::Clamp(C.MinWindowSize.X, 16, 2048), FMath::Clamp(C.MinWindowSize.Y, 16, 2048));
        R.Maximum = FIntPoint(FMath::Clamp(C.MaxWindowSize.X, 16, 2048), FMath::Clamp(C.MaxWindowSize.Y, 16, 2048));
        if (R.Minimum.X > R.Maximum.X)
        {
            Swap(R.Minimum.X, R.Maximum.X);
        }
        if (R.Minimum.Y > R.Maximum.Y)
        {
            Swap(R.Minimum.Y, R.Maximum.Y);
        }
        R.RequestedMinimum = FMath::Clamp(FMath::IsFinite(C.MinDisplayScale) ? C.MinDisplayScale : 0.25f, 0.01f, 32.f);
        R.RequestedMaximum = FMath::Clamp(FMath::IsFinite(C.MaxDisplayScale) ? C.MaxDisplayScale : 3.f, 0.01f, 32.f);
        if (R.RequestedMinimum > R.RequestedMaximum)
        {
            Swap(R.RequestedMinimum, R.RequestedMaximum);
        }
        const double SizeMinimum = FMath::Max(double(R.Minimum.X) / R.Logical.X, double(R.Minimum.Y) / R.Logical.Y);
        const double SizeMaximum = FMath::Min(double(R.Maximum.X) / R.Logical.X, double(R.Maximum.Y) / R.Logical.Y);
        if (SizeMinimum <= SizeMaximum)
        {
            // 求两个范围的交集。没有交集时将请求范围投影到最近的尺寸边界，优先满足窗体约束。
            R.EffectiveMinimum = FMath::Clamp(double(R.RequestedMinimum), SizeMinimum, SizeMaximum);
            R.EffectiveMaximum = FMath::Clamp(double(R.RequestedMaximum), SizeMinimum, SizeMaximum);
        }
        else
        {
            // 极端长宽比无法等比满足所有尺寸时锁定到最大可容纳倍率，最终尺寸再逐轴保护。
            // 不允许为了满足另一轴的最小值而突破最大纹理/窗口尺寸。
            R.EffectiveMinimum = R.EffectiveMaximum = SizeMaximum;
        }
        return R;
    }
} // namespace

/** 统一参数校验，蓝图 Setter、整组配置和 ini 都使用同一规则。 */
void FDesktopPetConfig::Normalize()
{
    // 在启动/应用配置时解析继承值；非法 ini 或蓝图枚举回退到可发现的标准应用窗口。
    if (WindowMode == EDesktopPetWindowMode::ProjectDefault)
    {
        WindowMode = GetDefault<UDesktopPetSettings>()->WindowMode;
    }
    if (WindowMode != EDesktopPetWindowMode::Application && WindowMode != EDesktopPetWindowMode::ToolWindow)
    {
        WindowMode = EDesktopPetWindowMode::Application;
    }
    // 图集采用二次幂，避免引擎隐式向上取整造成配置值与实际占用不一致。
    ShadowPageCapacity = FMath::RoundUpToPowerOfTwo(FMath::Clamp(ShadowPageCapacity, 512, 16384));
    SurfaceCacheCapacity = FMath::RoundUpToPowerOfTwo(FMath::Clamp(SurfaceCacheCapacity, 1024, 8192));
    RadianceProbeCapacity = FMath::RoundUpToPowerOfTwo(FMath::Clamp(RadianceProbeCapacity, 64, 256));
    IdleRenderTargetPoolMB = FMath::Clamp(IdleRenderTargetPoolMB, 0, 1000);
    const FPetResolvedSizeLimits Limits = ResolveSizeLimits(*this);
    WindowSize = Limits.Logical;
    MinWindowSize = Limits.Minimum;
    MaxWindowSize = Limits.Maximum;
    MinDisplayScale = Limits.RequestedMinimum;
    MaxDisplayScale = Limits.RequestedMaximum;
    DisplayScale = float(FMath::Clamp(
        FMath::IsFinite(DisplayScale) ? double(DisplayScale) : 1., Limits.EffectiveMinimum, Limits.EffectiveMaximum));
    SupersampleScale = FMath::Clamp(SupersampleScale, 1, 4);
    TargetFrameRate = FMath::Clamp(TargetFrameRate, 5, 60);
    TrayFrameRate = FMath::Clamp(TrayFrameRate, 1, 30);
    BackgroundVolumeMultiplier =
        FMath::Clamp(FMath::IsFinite(BackgroundVolumeMultiplier) ? BackgroundVolumeMultiplier : 1.f, 0.f, 1.f);
    Exposure = FMath::Clamp(FMath::IsFinite(Exposure) ? Exposure : 1.f, .01f, 16.f);
    HitAlphaThreshold = FMath::Clamp(HitAlphaThreshold, 1, 255);
    MenuCloseDelay = FMath::Clamp(MenuCloseDelay, 0.f, 10.f);
    MenuAnimationSeconds = FMath::Clamp(MenuAnimationSeconds, .01f, 5.f);
    Sharpness = FMath::Clamp(FMath::IsFinite(Sharpness) ? Sharpness : 0.2f, 0.f, 1.f);
    WheelZoomStep = FMath::Clamp(FMath::IsFinite(WheelZoomStep) ? WheelZoomStep : 0.1f, 0.01f, 0.5f);
    ZoomAnimationSeconds = FMath::Clamp(FMath::IsFinite(ZoomAnimationSeconds) ? ZoomAnimationSeconds : 0.18f, 0.f, 1.f);
    AdditiveAlphaStrength = FMath::Clamp(AdditiveAlphaStrength, .01f, 4.f);
}

/** 蓝图可读取实际可用范围；即使尚未启动，非法外部值也不会产生 NaN 或倒置范围。 */
FVector2D FDesktopPetConfig::GetEffectiveDisplayScaleRange() const
{
    const FPetResolvedSizeLimits Limits = ResolveSizeLimits(*this);
    return FVector2D(Limits.EffectiveMinimum, Limits.EffectiveMaximum);
}

/** Windows layered window 和渲染目标始终使用物理像素；UI 使用未缩放的逻辑尺寸。 */
FIntPoint FDesktopPetConfig::GetDisplaySize() const
{
    const FPetResolvedSizeLimits Limits = ResolveSizeLimits(*this);
    // 与 Normalize 保存的 float 倍率保持一致，避免边界在转换前后跨越半像素而抖动。
    const double Scale = float(FMath::Clamp(
        FMath::IsFinite(DisplayScale) ? double(DisplayScale) : 1., Limits.EffectiveMinimum, Limits.EffectiveMaximum));
    auto Axis = [this, Scale](int32 Logical, int32 Minimum, int32 Maximum)
    {
        // 保留原来的中心锚点奇偶取整；在边界处尺寸约束优先，不能因取整多出 1 像素。
        const int32 Rounded = bCenterAnchoredScaling ? Logical + 2 * FMath::RoundToInt(Logical * (Scale - 1.) * .5) :
                                                       FMath::RoundToInt(Logical * Scale);
        return FMath::Clamp(Rounded, Minimum, Maximum);
    };
    return FIntPoint(Axis(Limits.Logical.X, Limits.Minimum.X, Limits.Maximum.X),
                     Axis(Limits.Logical.Y, Limits.Minimum.Y, Limits.Maximum.Y));
}

/** 保持旧版本 ini 字段兼容，同时提供蓝图结构体接口。 */
FDesktopPetConfig UDesktopPetSettings::MakeRuntimeConfig() const
{
    FDesktopPetConfig Result;
    Result.WindowMode = WindowMode;
    Result.bAllowScreenCapture = bAllowScreenCapture;
    Result.bLimitEngineFrameRate = bLimitEngineFrameRate;
    Result.bUseGPUCompositing = bUseGPUCompositing;
    Result.TrayFrameRate = TrayFrameRate;
    Result.bAllowBackgroundAudio = bAllowBackgroundAudio;
    Result.BackgroundVolumeMultiplier = BackgroundVolumeMultiplier;
    Result.bCompositeSoftwareCursor = bCompositeSoftwareCursor;
    Result.WindowSize = WindowSize;
    Result.WindowPosition = WindowPosition;
    Result.DisplayScale = DisplayScale;
    Result.MinDisplayScale = MinDisplayScale;
    Result.MaxDisplayScale = MaxDisplayScale;
    Result.MinWindowSize = MinWindowSize;
    Result.MaxWindowSize = MaxWindowSize;
    Result.bAlwaysOnTop = bAlwaysOnTop;
    Result.bHideGameWindow = bHideGameWindow;
    Result.bDraggable = bDraggable;
    Result.bExitApplicationOnClose = bExitApplicationOnClose;
    Result.SupersampleScale = SupersampleScale;
    Result.TargetFrameRate = TargetFrameRate;
    Result.Exposure = Exposure;
    Result.bClickThroughTransparentPixels = bClickThroughTransparentPixels;
    Result.HitAlphaThreshold = HitAlphaThreshold;
    Result.MenuCloseDelay = MenuCloseDelay;
    Result.MenuAnimationSeconds = MenuAnimationSeconds;
    Result.bCaptureTranslucency = bCaptureTranslucency;
    Result.bPreserveAdditiveEffects = bPreserveAdditiveEffects;
    Result.AdditiveAlphaStrength = AdditiveAlphaStrength;
    Result.bTransparentWindowEnabled = bTransparentWindowEnabled;
    Result.bCenterAnchoredScaling = bCenterAnchoredScaling;
    Result.bEnableWheelZoom = bEnableWheelZoom;
    Result.WheelZoomStep = WheelZoomStep;
    Result.ZoomAnimationSeconds = ZoomAnimationSeconds;
    Result.Sharpness = Sharpness;
    Result.bCompactMemory = bCompactMemory;
    Result.ShadowPageCapacity = ShadowPageCapacity;
    Result.SurfaceCacheCapacity = SurfaceCacheCapacity;
    Result.RadianceProbeCapacity = RadianceProbeCapacity;
    Result.IdleRenderTargetPoolMB = IdleRenderTargetPoolMB;
    Result.Normalize();
    return Result;
}
