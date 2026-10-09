#pragma once
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"
class FTextureRenderTargetResource;
class ADesktopPetActor;

/** 只为本实例的捕获导出深度覆盖率；不引用 Renderer/Private 或定制引擎字段。 */
class FDesktopPetViewExtension : public FSceneViewExtensionBase
{
public:
    FDesktopPetViewExtension(const FAutoRegister& Register,FTextureRenderTargetResource* Color,FTextureRenderTargetResource* Geometry,FTextureRenderTargetResource* Opacity,ADesktopPetActor* Host);
    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily&,FSceneView&) override;
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass,const FSceneView& View,FPostProcessingPassDelegateArray& Callbacks,bool Enabled) override;
    /** 游戏线程诊断：记录真正提交给场景渲染器的视图参数。 */
    int32 LastViewGI=INDEX_NONE,LastViewReflections=INDEX_NONE;
private:
    /** 资源由宿主持有，宿主必须在更换/销毁资源前等待渲染线程。 */
    TWeakObjectPtr<ADesktopPetActor> Owner;
    FTextureRenderTargetResource* ColorResource;
    FTextureRenderTargetResource* GeometryResource;
    FTextureRenderTargetResource* OpacityResource;
    FScreenPassTexture ExportCoverage(FRDGBuilder& GraphBuilder,const FSceneView& View,const FPostProcessMaterialInputs& Inputs);
};
