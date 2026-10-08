#include "DesktopPetActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"

// 使用引擎实际的色调映射/调色链；不用 CPU 近似公式替代定制引擎的后处理。
void ADesktopPetActor::ConfigureCapturePipeline()
{
    const FDesktopPetConfig Config=GetRuntimeConfig();
    Capture->CaptureSource=Config.bUseEnginePostProcessing?SCS_FinalToneCurveHDR:SCS_SceneColorHDR;
    Capture->TextureTarget=Config.bUseEnginePostProcessing?FinalColorTarget:SceneTarget;
    Capture->CompositeMode=SCCM_Overwrite;
    Capture->bAlwaysPersistRenderingState=true;
    // 大气和雾会填满透明背景；时间性 AA 与独立覆盖率不一致，轮廓继续使用 SSAA。
    Capture->ShowFlags.SetAtmosphere(false);Capture->ShowFlags.SetFog(false);
    Capture->ShowFlags.SetVolumetricFog(false);Capture->ShowFlags.SetMotionBlur(false);
    Capture->ShowFlags.SetTemporalAA(false);Capture->ShowFlags.SetAntiAliasing(false);
    Capture->ShowFlags.SetPostProcessing(true);
    Capture->ShowFlags.SetTonemapper(true);
    Capture->ShowFlags.SetEyeAdaptation(Config.bUseEnginePostProcessing);
    Capture->ShowFlags.SetBloom(Config.bUseEnginePostProcessing);
    Capture->ShowFlags.SetScreenSpaceReflections(Config.bUseEnginePostProcessing);
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
    OpacityCapture->PostProcessBlendWeight=Capture->PostProcessBlendWeight;
}

void ADesktopPetActor::SetUseEnginePostProcessing(bool Value)
{
    auto Config=GetRuntimeConfig();Config.bUseEnginePostProcessing=Value;ApplyRuntimeConfig(Config);
}
