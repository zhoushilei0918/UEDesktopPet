#include "DesktopPetGPUCompositor.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarPetGPUComposite(TEXT("DesktopPet.GPUComposite"),1,
    TEXT("Use the plugin GPU compositor (0 keeps the CPU compatibility path)."),ECVF_Default);

/** 第一遍保持原采样顺序，完成 SSAA、sRGB、覆盖率重建和预乘。 */
class FDesktopPetResolveCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FDesktopPetResolveCS);
    SHADER_USE_PARAMETER_STRUCT(FDesktopPetResolveCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,FinalColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,InverseOpacity)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint>,PetOutput)
        SHADER_PARAMETER(FIntPoint,OutputSize)
        SHADER_PARAMETER(int32,SampleScale)
        SHADER_PARAMETER(float,InverseSamples)
        SHADER_PARAMETER(float,ExposureScale)
        SHADER_PARAMETER(float,AdditiveStrength)
        SHADER_PARAMETER(uint32,PreserveAdditive)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    {return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FDesktopPetResolveCS,"/Plugin/DesktopPet/Composite.usf","ResolveCS",SF_Compute);

/** 第二遍只锐化不透明内部，再按原来的预乘规则叠加 UI。 */
class FDesktopPetFinishCS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FDesktopPetFinishCS);
    SHADER_USE_PARAMETER_STRUCT(FDesktopPetFinishCS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>,PetInput)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D,UIBytes)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<uint2>,PackedOutput)
        SHADER_PARAMETER(FIntPoint,OutputSize)
        SHADER_PARAMETER(float,Sharpness)
        SHADER_PARAMETER(uint32,HasUI)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    {return IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5);}
};
IMPLEMENT_GLOBAL_SHADER(FDesktopPetFinishCS,"/Plugin/DesktopPet/Composite.usf","FinishCS",SF_Compute);

bool DesktopPetGPUCompositor::IsEnabled(const FDesktopPetConfig& Config)
{return Config.bUseGPUCompositing&&CVarPetGPUComposite.GetValueOnGameThread()!=0&&GMaxRHIFeatureLevel>=ERHIFeatureLevel::SM5;}

void DesktopPetGPUCompositor::Enqueue(FRHICommandListImmediate& Cmd,FRHITexture* Color,FRHITexture* Opacity,
    FRHITexture* UI,FIntPoint Size,const FDesktopPetConfig& Config,FRHIGPUBufferReadback& Readback)
{
    check(IsInRenderingThread());
    FRDGBuilder Graph(Cmd);
    const uint32 Count=uint32(Size.X)*Size.Y;
    const auto ColorRDG=Graph.RegisterExternalTexture(CreateRenderTarget(Color,TEXT("DesktopPet.FinalColor")));
    const auto AlphaRDG=Graph.RegisterExternalTexture(CreateRenderTarget(Opacity,TEXT("DesktopPet.InverseOpacity")));
    const auto Pet=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(uint32),Count),TEXT("DesktopPet.ResolvedPet"));
    const auto Packed=Graph.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(FPackedPixel),Count),TEXT("DesktopPet.PackedPixels"));
    auto* Resolve=Graph.AllocParameters<FDesktopPetResolveCS::FParameters>();
    Resolve->FinalColor=ColorRDG;Resolve->InverseOpacity=AlphaRDG;Resolve->PetOutput=Graph.CreateUAV(Pet);
    Resolve->OutputSize=Size;Resolve->SampleScale=Config.SupersampleScale;
    Resolve->InverseSamples=1.f/(Config.SupersampleScale*Config.SupersampleScale);
    Resolve->ExposureScale=Resolve->InverseSamples*Config.Exposure;
    Resolve->AdditiveStrength=Config.AdditiveAlphaStrength;Resolve->PreserveAdditive=Config.bPreserveAdditiveEffects;
    TShaderMapRef<FDesktopPetResolveCS> ResolveShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FComputeShaderUtils::AddPass(Graph,RDG_EVENT_NAME("DesktopPet Resolve"),ResolveShader,Resolve,FComputeShaderUtils::GetGroupCount(Size,FIntPoint(8,8)));
    auto* Finish=Graph.AllocParameters<FDesktopPetFinishCS::FParameters>();
    Finish->PetInput=Graph.CreateSRV(Pet);Finish->PackedOutput=Graph.CreateUAV(Packed);
    Finish->OutputSize=Size;Finish->Sharpness=Config.Sharpness;Finish->HasUI=UI!=nullptr;
    // UI 原路径读取的是 BGRA 字节，禁用 SRV 的 sRGB 解码，防止二次转换导致 UI 变暗。
    const auto UIRDG=UI?Graph.RegisterExternalTexture(CreateRenderTarget(UI,TEXT("DesktopPet.UI"))):ColorRDG;
    FRDGTextureSRVDesc UIDesc(UIRDG);UIDesc.SRGBOverride=SRGBO_ForceDisable;
    Finish->UIBytes=Graph.CreateSRV(UIDesc);
    TShaderMapRef<FDesktopPetFinishCS> FinishShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FComputeShaderUtils::AddPass(Graph,RDG_EVENT_NAME("DesktopPet Finish"),FinishShader,Finish,FComputeShaderUtils::GetGroupCount(Size,FIntPoint(8,8)));
    AddEnqueueCopyPass(Graph,&Readback,Packed,Count*sizeof(FPackedPixel));
    Graph.Execute();
}
