#include "DesktopPetRenderPolicy.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "HAL/IConsoleManager.h"

FDesktopPetRenderPolicy FDesktopPetRenderPolicy::Resolve(const USceneCaptureComponent2D* Capture)
{
    check(IsInGameThread());
    FDesktopPetRenderPolicy Result;
    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.DynamicGlobalIlluminationMethod")))
        Result.GI=static_cast<EDynamicGlobalIlluminationMethod::Type>(C->GetInt());
    if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ReflectionMethod")))
        Result.Reflections=static_cast<EReflectionMethod::Type>(C->GetInt());
    if(!Capture)return Result;
    // 这三个字段在 FSceneView 中使用 SET_PP：权重大于零即覆盖，不进行枚举插值。
    auto Apply=[&](const FPostProcessSettings& PP,float Weight)
    {
        if(Weight<=0.f||!FMath::IsFinite(Weight))return;
        if(PP.bOverride_DynamicGlobalIlluminationMethod)Result.GI=PP.DynamicGlobalIlluminationMethod;
        if(PP.bOverride_ReflectionMethod)Result.Reflections=PP.ReflectionMethod;
        if(PP.bOverride_LumenSurfaceCacheResolution)Result.SurfaceResolution=PP.LumenSurfaceCacheResolution;
    };
    if(UWorld* World=Capture->GetWorld())
    {
        // 引擎提供的迭代器已按优先级排列，也包括后处理组件，不仅仅是体积 Actor。
        for(IInterface_PostProcessVolume& Volume:World->GetPostProcessVolumeIterator())
        {
            const auto V=Volume.GetProperties();
            if(!V.bIsEnabled||!V.Settings)continue;
            float Weight=FMath::Clamp(V.BlendWeight,0.f,1.f);
            if(!V.bIsUnbound)
            {
                float Distance=0.f;Volume.EncompassesPoint(Capture->GetComponentLocation(),0.f,&Distance);
                if(Distance<0.f||Distance>V.BlendRadius)Weight=0.f;
                else if(V.BlendRadius>=1.f)Weight*=1.f-Distance/V.BlendRadius;
            }
            Apply(*V.Settings,Weight);
        }
    }
    Apply(Capture->PostProcessSettings,FMath::Clamp(Capture->PostProcessBlendWeight,0.f,1.f));
    return Result;
}
