#pragma once
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"
class FTextureRenderTargetResource;

/** 只为本实例的捕获导出深度覆盖率；不引用 Renderer/Private 或定制引擎字段。 */
class FDesktopPetViewExtension : public FSceneViewExtensionBase
{
public:
    FDesktopPetViewExtension(const FAutoRegister& Register,FTextureRenderTargetResource* Color,FTextureRenderTargetResource* Geometry,FTextureRenderTargetResource* Opacity);
    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily&,FSceneView&) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass,const FSceneView& View,FPostProcessingPassDelegateArray& Callbacks,bool Enabled) override;
private:
    /** 资源由宿主持有，宿主必须在更换/销毁资源前等待渲染线程。 */
    FTextureRenderTargetResource* ColorResource;
    FTextureRenderTargetResource* GeometryResource;
    FTextureRenderTargetResource* OpacityResource;
    FScreenPassTexture ExportCoverage(FRDGBuilder& GraphBuilder,const FSceneView& View,const FPostProcessMaterialInputs& Inputs);
};
