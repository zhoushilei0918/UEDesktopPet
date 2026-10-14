#include "DesktopPetActor.h"
#include "DesktopPetSettings.h"
#include "DesktopPetMemory.h"
#include "DesktopPetAudio.h"
#include "DesktopPetFramePacing.h"
#include "DesktopPetRuntime.h"

/** 未启动的实例也可以先读取并修改默认配置。 */
FDesktopPetConfig ADesktopPetActor::PetGetDefaultRuntimeConfig()
{
    return GetDefault<UDesktopPetSettings>()->MakeRuntimeConfig();
}

FDesktopPetConfig ADesktopPetActor::PetGetRuntimeConfig() const
{
    FDesktopPetConfig Result =
        bHasConfig ? DesiredConfig : (bOverrideDefaultConfig ? InitialConfig : PetGetDefaultRuntimeConfig());
    if (Runtime && !bConfigPending)
    {
        Result.WindowPosition = PetGetWindowPosition();
        Result.DisplayScale = Runtime->PresentedScale;
    }
    return Result;
}

/** 任何字段都可通过整组配置运行时修改；普通参数属于实例，Memory 字段由全局协调器协商。 */
void ADesktopPetActor::PetApplyRuntimeConfig(const FDesktopPetConfig& Config)
{
    if (Runtime)
    {
        Runtime->ZoomCommitPending = false;
    }
    const FDesktopPetConfig Before = PetGetRuntimeConfig();
    DesiredConfig = Config;
    DesiredConfig.Normalize();
    if (DesiredConfig.bCenterAnchoredScaling && DesiredConfig.WindowPosition == Before.WindowPosition)
    {
        // 拖拽或外部移动后重新校准；未应用的多次滚轮请求共享同一个精确中心。
        if (!bCenterAnchorValid || Before.WindowPosition != CenterAnchorPosition)
        {
            ExactWindowCenter = FVector2D(Before.WindowPosition) + FVector2D(Before.GetDisplaySize()) * 0.5;
        }
        const FVector2D Position = ExactWindowCenter - FVector2D(DesiredConfig.GetDisplaySize()) * 0.5;
        DesiredConfig.WindowPosition = FIntPoint(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
        bCenterAnchorValid = true;
    }
    else
    {
        ExactWindowCenter = FVector2D(DesiredConfig.WindowPosition) + FVector2D(DesiredConfig.GetDisplaySize()) * 0.5;
        bCenterAnchorValid = true;
    }
    CenterAnchorPosition = DesiredConfig.WindowPosition;
    bHasConfig = true;
    bConfigPending = true;
}

/** 运行时更新 SupersampleScale；保留其他实例参数。 */
void ADesktopPetActor::PetSetSupersampleScale(int32 Value)
{
    auto C = PetGetRuntimeConfig();
    C.SupersampleScale = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 TargetFrameRate；保留其他实例参数。 */
void ADesktopPetActor::PetSetTargetFrameRate(int32 Value)
{
    auto C = PetGetRuntimeConfig();
    C.TargetFrameRate = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 Exposure；保留其他实例参数。 */
void ADesktopPetActor::PetSetExposure(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.Exposure = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 bClickThroughTransparentPixels；保留其他实例参数。 */
void ADesktopPetActor::PetSetClickThroughTransparentPixels(bool Value)
{
    auto C = PetGetRuntimeConfig();
    C.bClickThroughTransparentPixels = Value;
    PetApplyRuntimeConfig(C);
}

/** 运行时更新 HitAlphaThreshold；保留其他实例参数。 */
void ADesktopPetActor::PetSetHitAlphaThreshold(int32 Value)
{
    auto C = PetGetRuntimeConfig();
    C.HitAlphaThreshold = Value;
    PetApplyRuntimeConfig(C);
}

/** 所有快捷节点最终走同一配置应用路径，运行时切换不重建窗口。 */
void ADesktopPetActor::PetSetCompactMemory(bool bEnabled)
{
    auto C = PetGetRuntimeConfig();
    C.bCompactMemory = bEnabled;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetMemoryCapacities(int32 ShadowPages,
                                              int32 SurfaceAtlasSize,
                                              int32 RadianceProbes,
                                              int32 IdlePoolMB)
{
    auto C = PetGetRuntimeConfig();
    C.ShadowPageCapacity = ShadowPages;
    C.SurfaceCacheCapacity = SurfaceAtlasSize;
    C.RadianceProbeCapacity = RadianceProbes;
    C.IdleRenderTargetPoolMB = IdlePoolMB;
    PetApplyRuntimeConfig(C);
}

bool ADesktopPetActor::PetIsCompactMemoryActive() const
{
    return FDesktopPetMemory::IsActive(this);
}

void ADesktopPetActor::PetSetLimitEngineFrameRate(bool bEnabled)
{
    auto C = PetGetRuntimeConfig();
    C.bLimitEngineFrameRate = bEnabled;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetGPUCompositingEnabled(bool bEnabled)
{
    auto C = PetGetRuntimeConfig();
    C.bUseGPUCompositing = bEnabled;
    PetApplyRuntimeConfig(C);
}

/** 声音配置与其他 Setter 共用运行时配置与保存入口，不依赖项目音频蓝图。 光标显示方式只影响插件窗口合成，不修改 PlayerController 或项目 Widget 的属性。 */
void ADesktopPetActor::PetSetCompositeSoftwareCursorEnabled(bool bEnabled)
{
    auto C = PetGetRuntimeConfig();
    C.bCompositeSoftwareCursor = bEnabled;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetBackgroundAudioEnabled(bool bEnabled)
{
    auto C = PetGetRuntimeConfig();
    C.bAllowBackgroundAudio = bEnabled;
    PetApplyRuntimeConfig(C);
}

void ADesktopPetActor::PetSetBackgroundVolumeMultiplier(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.BackgroundVolumeMultiplier = Value;
    PetApplyRuntimeConfig(C);
}

float ADesktopPetActor::PetGetEffectiveEngineFrameRateLimit() const
{
    return FDesktopPetFramePacing::GetEffectiveLimit();
}

void ADesktopPetActor::PetSetSharpness(float Value)
{
    auto C = PetGetRuntimeConfig();
    C.Sharpness = Value;
    PetApplyRuntimeConfig(C);
}
