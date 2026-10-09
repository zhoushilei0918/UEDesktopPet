#include "DesktopPetViewExtension.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "SceneView.h"
#include "TextureResource.h"
#include "DesktopPetActor.h"
#include "DesktopPetRenderPolicy.h"
#include "Components/SceneCaptureComponent2D.h"

/** 深度覆盖率保留黑色描边，不通过亮度猜测不透明轮廓。 */
class FDesktopPetCoverageCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FDesktopPetCoverageCS);
    SHADER_USE_PARAMETER_STRUCT(FDesktopPetCoverageCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,SceneDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,RawOpacity)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>,OutputCoverage)
        SHADER_PARAMETER(FIntPoint,OutputSize)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters,View)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {return IsFeatureLevelSupported(Parameters.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FDesktopPetCoverageCS,"/Plugin/DesktopPet/Coverage.usf","MainCS",SF_Compute);

FDesktopPetViewExtension::FDesktopPetViewExtension(const FAutoRegister& Register,FTextureRenderTargetResource* Color,FTextureRenderTargetResource* Geometry,FTextureRenderTargetResource* Opacity,ADesktopPetActor* Host)
    :FSceneViewExtensionBase(Register),Owner(Host),ColorResource(Color),GeometryResource(Geometry),OpacityResource(Opacity){}

// 只修正自己的离屏视图，不改主视口、项目 CVar、组件后处理设置或定制后处理链。
void FDesktopPetViewExtension::SetupView(FSceneViewFamily& Family,FSceneView& View)
{
    ADesktopPetActor* Host=Owner.Get();
    if(!Host||(Family.RenderTarget!=ColorResource&&Family.RenderTarget!=OpacityResource))return;
    if(Family.RenderTarget==ColorResource)
    {
        const auto Policy=FDesktopPetRenderPolicy::Resolve(Host->Capture);
        View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod=Policy.GI;
        View.FinalPostProcessSettings.ReflectionMethod=Policy.Reflections;
        View.FinalPostProcessSettings.LumenSurfaceCacheResolution=Policy.SurfaceResolution;
        LastViewGI=View.FinalPostProcessSettings.DynamicGlobalIlluminationMethod;LastViewReflections=View.FinalPostProcessSettings.ReflectionMethod;
    }
    if(!Host->Capture->bUseCustomProjectionMatrix)
    {
        // 始终使用逻辑画布宽高比：整数尺寸取整不能改变相机取景，再渲染时才不会回跳。
        const FIntPoint Canvas=Host->PetGetRuntimeConfig().WindowSize;
        const FIntPoint Size=Family.RenderTarget->GetSizeXY();
        FMatrix Projection=View.ProjectionMatrixUnadjustedForRHI;
        Projection.M[1][1]*=(double(Canvas.X)/Canvas.Y)/(double(Size.X)/Size.Y);
        View.UpdateProjectionMatrix(Projection);
    }
}

// 此时不透明及自定义描边已经写完深度；颜色仍继续走引擎完整后处理直到最终目标。
void FDesktopPetViewExtension::SubscribeToPostProcessingPass(EPostProcessingPass Pass,const FSceneView& View,FPostProcessingPassDelegateArray& Callbacks,bool Enabled)
{
    if(Pass==EPostProcessingPass::Tonemap&&View.Family&&View.Family->RenderTarget==ColorResource)
        Callbacks.Add(FPostProcessingPassDelegate::CreateRaw(this,&FDesktopPetViewExtension::ExportCoverage));
}
FScreenPassTexture FDesktopPetViewExtension::ExportCoverage(FRDGBuilder& GraphBuilder,const FSceneView& View,const FPostProcessMaterialInputs& Inputs)
{
    if(Inputs.SceneTextures.SceneTextures&&GeometryResource)
    {
        FRDGTextureRef Depth=Inputs.SceneTextures.SceneTextures->GetParameters()->SceneDepthTexture;
        FRHITexture* Geometry=GeometryResource->GetRenderTargetTexture();
        if(Depth&&Geometry&&OpacityResource)
        {
            auto Output=GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Geometry,TEXT("DesktopPet.Coverage")));
            auto* P=GraphBuilder.AllocParameters<FDesktopPetCoverageCS::FParameters>();
            P->RawOpacity=GraphBuilder.RegisterExternalTexture(CreateRenderTarget(OpacityResource->GetRenderTargetTexture(),TEXT("DesktopPet.RawOpacity")));
            P->SceneDepth=Depth;P->OutputCoverage=GraphBuilder.CreateUAV(Output);
            P->OutputSize=Output->Desc.Extent;P->View=View.ViewUniformBuffer;
            TShaderMapRef<FDesktopPetCoverageCS> Shader(GetGlobalShaderMap(View.GetFeatureLevel()));
            FComputeShaderUtils::AddPass(GraphBuilder,RDG_EVENT_NAME("DesktopPet Coverage"),Shader,P,FComputeShaderUtils::GetGroupCount(P->OutputSize,FIntPoint(8,8)));
        }
    }
    // 尊重其他扩展的 OverrideOutput，不截断或替换引擎颜色处理。
    return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
}
