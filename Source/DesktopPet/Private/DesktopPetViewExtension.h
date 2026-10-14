#pragma once
#include "SceneViewExtension.h"
#include "PostProcess/PostProcessMaterialInputs.h"
class FTextureRenderTargetResource;
class ADesktopPetActor;

/** 只为本实例的捕获导出深度覆盖率；不引用 Renderer/Private 或定制引擎字段。 */
class FDesktopPetViewExtension : public FSceneViewExtensionBase
{
public:
    /**
     * 把本实例的颜色、深度和反透明度资源接入引擎视图扩展。
     * Register - 引擎提供的视图扩展注册凭据；
     * Color - 最终颜色目标资源；
     * Geometry - 接收覆盖率的目标资源；
     * Opacity - 半透明捕获的反透明度资源；
     * Host - 持有资源的桌宠宿主。
     */
    FDesktopPetViewExtension(const FAutoRegister& Register,
                             FTextureRenderTargetResource* Color,
                             FTextureRenderTargetResource* Geometry,
                             FTextureRenderTargetResource* Opacity,
                             ADesktopPetActor* Host);
    virtual void SetupViewFamily(FSceneViewFamily&) override
    {
    }
    virtual void SetupView(FSceneViewFamily&, FSceneView&) override;
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override
    {
    }
    virtual void SubscribeToPostProcessingPass(EPostProcessingPass Pass,
                                               const FSceneView& View,
                                               FPostProcessingPassDelegateArray& Callbacks,
                                               bool Enabled) override;

private:
    /** 资源由宿主持有，宿主必须在更换/销毁资源前等待渲染线程。 */
    TWeakObjectPtr<ADesktopPetActor> Owner;
    FTextureRenderTargetResource* ColorResource;
    FTextureRenderTargetResource* GeometryResource;
    FTextureRenderTargetResource* OpacityResource;
    FScreenPassTexture ExportCoverage(FRDGBuilder& GraphBuilder,
                                      const FSceneView& View,
                                      const FPostProcessMaterialInputs& Inputs);
};
