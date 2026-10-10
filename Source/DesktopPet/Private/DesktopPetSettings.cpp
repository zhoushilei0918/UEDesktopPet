#include "DesktopPetSettings.h"

// 统一参数校验，蓝图 Setter、整组配置和 ini 都使用同一规则。
void FDesktopPetConfig::Normalize()
{
    // 在启动/应用配置时解析继承值；非法 ini 或蓝图枚举回退到可发现的标准应用窗口。
    if(WindowMode==EDesktopPetWindowMode::ProjectDefault)WindowMode=GetDefault<UDesktopPetSettings>()->WindowMode;
    if(WindowMode!=EDesktopPetWindowMode::Application&&WindowMode!=EDesktopPetWindowMode::ToolWindow)
        WindowMode=EDesktopPetWindowMode::Application;
    // 图集采用二次幂，避免引擎隐式向上取整造成配置值与实际占用不一致。
    ShadowPageCapacity=FMath::RoundUpToPowerOfTwo(FMath::Clamp(ShadowPageCapacity,512,16384));
    SurfaceCacheCapacity=FMath::RoundUpToPowerOfTwo(FMath::Clamp(SurfaceCacheCapacity,1024,8192));
    RadianceProbeCapacity=FMath::RoundUpToPowerOfTwo(FMath::Clamp(RadianceProbeCapacity,64,256));
    IdleRenderTargetPoolMB=FMath::Clamp(IdleRenderTargetPoolMB,0,1000);
    WindowSize.X=FMath::Clamp(WindowSize.X,64,2048);
    WindowSize.Y=FMath::Clamp(WindowSize.Y,64,2048);
    DisplayScale=FMath::Clamp(FMath::IsFinite(DisplayScale)?DisplayScale:1.f,.25f,3.f);
    DisplayScale=FMath::Min(DisplayScale,2048.f/FMath::Max(WindowSize.X,WindowSize.Y));
    SupersampleScale=FMath::Clamp(SupersampleScale,1,4);
    TargetFrameRate=FMath::Clamp(TargetFrameRate,5,60);
    TrayFrameRate=FMath::Clamp(TrayFrameRate,1,30);
    Exposure=FMath::Clamp(FMath::IsFinite(Exposure)?Exposure:1.f,.01f,16.f);
    HitAlphaThreshold=FMath::Clamp(HitAlphaThreshold,1,255);
    MenuCloseDelay=FMath::Clamp(MenuCloseDelay,0.f,10.f);
    MenuAnimationSeconds=FMath::Clamp(MenuAnimationSeconds,.01f,5.f);
    Sharpness=FMath::Clamp(FMath::IsFinite(Sharpness)?Sharpness:0.2f,0.f,1.f);
    WheelZoomStep=FMath::Clamp(FMath::IsFinite(WheelZoomStep)?WheelZoomStep:0.1f,0.01f,0.5f);
    ZoomAnimationSeconds=FMath::Clamp(FMath::IsFinite(ZoomAnimationSeconds)?ZoomAnimationSeconds:0.18f,0.f,1.f);
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
    Result.WindowMode = WindowMode;
    Result.bAllowScreenCapture = bAllowScreenCapture;
    Result.bLimitEngineFrameRate = bLimitEngineFrameRate;
    Result.TrayFrameRate = TrayFrameRate;
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
