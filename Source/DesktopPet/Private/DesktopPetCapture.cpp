#include "DesktopPetActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/IConsoleManager.h"

// 使用引擎实际的色调映射/调色链；不用 CPU 近似公式替代定制引擎的后处理。
void ADesktopPetActor::ConfigureCapturePipeline()
{
    const FDesktopPetConfig Config=PetGetRuntimeConfig();
    Capture->CaptureSource=SCS_FinalToneCurveHDR;
    Capture->TextureTarget=FinalColorTarget;
    auto& PP=Capture->PostProcessSettings;
    if(!PP.bOverride_DynamicGlobalIlluminationMethod)
    {
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod")))
        {PP.bOverride_DynamicGlobalIlluminationMethod=true;PP.DynamicGlobalIlluminationMethod=static_cast<EDynamicGlobalIlluminationMethod::Type>(C->GetInt());}
    }
    if(!PP.bOverride_ReflectionMethod)
    {
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod")))
        {PP.bOverride_ReflectionMethod=true;PP.ReflectionMethod=static_cast<EReflectionMethod::Type>(C->GetInt());}
    }
    // SceneCapture 默认使用半分辨率 Lumen Surface Cache，这里与常规视图对齐。
    if(!PP.bOverride_LumenSurfaceCacheResolution){PP.bOverride_LumenSurfaceCacheResolution=true;PP.LumenSurfaceCacheResolution=1.f;}
    Capture->bUseRayTracingIfEnabled=true;
    Capture->CompositeMode=SCCM_Overwrite;
    Capture->bAlwaysPersistRenderingState=true;
    // 大气和雾会填满透明背景；时间性 AA 与独立覆盖率不一致，轮廓继续使用 SSAA。
    Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetVolumetricFog(false);Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetAntiAliasing(false);
    Capture->ShowFlags.SetPostProcessing(true);
    Capture->ShowFlags.SetTonemapper(true);
    Capture->ShowFlags.SetEyeAdaptation(true);
    Capture->ShowFlags.SetBloom(true);
    Capture->ShowFlags.SetScreenSpaceReflections(true);
    Capture->ShowFlags.SetParticles(true);Capture->ShowFlags.SetNiagara(true);
    Capture->ShowFlags.SetTranslucency(Config.bCaptureTranslucency);
    Capture->ShowFlags.SetSeparateTranslucency(Config.bCaptureTranslucency);
}

// 每帧跟随主相机，可支持项目动画、相机移动、FOV/正交宽度和显示白名单动态变化。
void ADesktopPetActor::SyncOpacityCapture()
{
    OpacityCapture->SetWorldTransform(Capture->GetComponentTransform());
    OpacityCapture->ProjectionType=Capture->ProjectionType;
    OpacityCapture->FOVAngle=Capture->FOVAngle;
    OpacityCapture->OrthoWidth=Capture->OrthoWidth;
    OpacityCapture->bAutoCalculateOrthoPlanes=Capture->bAutoCalculateOrthoPlanes;
    OpacityCapture->AutoPlaneShift=Capture->AutoPlaneShift;
    OpacityCapture->bUpdateOrthoPlanes=Capture->bUpdateOrthoPlanes;
    OpacityCapture->bUseCameraHeightAsViewTarget=Capture->bUseCameraHeightAsViewTarget;
    OpacityCapture->bUseCustomProjectionMatrix=Capture->bUseCustomProjectionMatrix;
    OpacityCapture->CustomProjectionMatrix=Capture->CustomProjectionMatrix;
    OpacityCapture->bOverride_CustomNearClippingPlane=Capture->bOverride_CustomNearClippingPlane;
    OpacityCapture->CustomNearClippingPlane=Capture->CustomNearClippingPlane;
    OpacityCapture->bEnableClipPlane=Capture->bEnableClipPlane;
    OpacityCapture->ClipPlaneBase=Capture->ClipPlaneBase;
    OpacityCapture->ClipPlaneNormal=Capture->ClipPlaneNormal;
    OpacityCapture->PrimitiveRenderMode=Capture->PrimitiveRenderMode;
    OpacityCapture->ShowOnlyActors=Capture->ShowOnlyActors;
    OpacityCapture->ShowOnlyComponents=Capture->ShowOnlyComponents;
    OpacityCapture->HiddenActors=Capture->HiddenActors;
    OpacityCapture->HiddenComponents=Capture->HiddenComponents;
    OpacityCapture->LODDistanceFactor=Capture->LODDistanceFactor;
    OpacityCapture->MaxViewDistanceOverride=Capture->MaxViewDistanceOverride;
    OpacityCapture->ShowFlags=Capture->ShowFlags;
    OpacityCapture->ShowFlags.SetEyeAdaptation(false);
    OpacityCapture->ShowFlags.SetBloom(false);
    OpacityCapture->ShowFlags.SetScreenSpaceReflections(false);
    OpacityCapture->TextureTarget=SceneTarget;
    OpacityCapture->CaptureSource=SCS_SceneColorHDR;
    OpacityCapture->CompositeMode=SCCM_Overwrite;
    OpacityCapture->bAlwaysPersistRenderingState=true;
    // 覆盖率不需要色调映射；同一份后处理参数仍供投影和材质相关路径使用。
    OpacityCapture->PostProcessSettings=Capture->PostProcessSettings;
    OpacityCapture->PostProcessBlendWeight=1.f;
    OpacityCapture->PostProcessSettings.bOverride_DynamicGlobalIlluminationMethod=true;
    OpacityCapture->PostProcessSettings.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    OpacityCapture->PostProcessSettings.bOverride_ReflectionMethod=true;
    OpacityCapture->PostProcessSettings.ReflectionMethod=EReflectionMethod::None;
}
