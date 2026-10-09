#include "DesktopPetSettings.h"

// 统一参数校验，蓝图 Setter、整组配置和 ini 都使用同一规则。
void FDesktopPetConfig::Normalize()
{
    WindowSize.X=FMath::Clamp(WindowSize.X,64,2048);
    WindowSize.Y=FMath::Clamp(WindowSize.Y,64,2048);
    DisplayScale=FMath::Clamp(FMath::IsFinite(DisplayScale)?DisplayScale:1.f,.25f,3.f);
    DisplayScale=FMath::Min(DisplayScale,2048.f/FMath::Max(WindowSize.X,WindowSize.Y));
    SupersampleScale=FMath::Clamp(SupersampleScale,1,4);
    TargetFrameRate=FMath::Clamp(TargetFrameRate,5,60);
    Exposure=FMath::Clamp(FMath::IsFinite(Exposure)?Exposure:1.f,.01f,16.f);
    HitAlphaThreshold=FMath::Clamp(HitAlphaThreshold,1,255);
    MenuCloseDelay=FMath::Clamp(MenuCloseDelay,0.f,10.f);
    MenuAnimationSeconds=FMath::Clamp(MenuAnimationSeconds,.01f,5.f);
    Sharpness=FMath::Clamp(FMath::IsFinite(Sharpness)?Sharpness:0.2f,0.f,1.f);
    WheelZoomStep=FMath::Clamp(FMath::IsFinite(WheelZoomStep)?WheelZoomStep:0.1f,0.01f,0.5f);
    AdditiveAlphaStrength=FMath::Clamp(AdditiveAlphaStrength,.01f,4.f);
}

// Windows layered window 和渲染目标始终使用物理像素；UI 使用未缩放的逻辑尺寸。
FIntPoint FDesktopPetConfig::GetDisplaySize() const
{
    auto Axis=[this](int32 Logical)
    {
        if(!bCenterAnchoredScaling)return FMath::Max(16,FMath::RoundToInt(Logical*DisplayScale));
        const int32 Parity=Logical&1;
        return FMath::Clamp(Logical+2*int32(FMath::RoundToInt(Logical*(double(DisplayScale)-1.0)*0.5)),16+Parity,2048-Parity);
    };
    return FIntPoint(Axis(WindowSize.X),Axis(WindowSize.Y));
}

// 保持旧版本 ini 字段兼容，同时提供蓝图结构体接口。
FDesktopPetConfig UDesktopPetSettings::MakeRuntimeConfig() const
{
    FDesktopPetConfig Result;
    Result.WindowSize = WindowSize;
    Result.WindowPosition = WindowPosition;
    Result.DisplayScale = DisplayScale;
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
    Result.bWriteDiagnostics = bWriteDiagnostics;
    Result.bCaptureTranslucency = bCaptureTranslucency;
    Result.bPreserveAdditiveEffects = bPreserveAdditiveEffects;
    Result.AdditiveAlphaStrength = AdditiveAlphaStrength;
    Result.bTransparentWindowEnabled = bTransparentWindowEnabled;
    Result.bCenterAnchoredScaling = bCenterAnchoredScaling;
    Result.bEnableWheelZoom = bEnableWheelZoom;
    Result.WheelZoomStep = WheelZoomStep;
    Result.Sharpness = Sharpness;
    Result.Normalize();
    return Result;
}
