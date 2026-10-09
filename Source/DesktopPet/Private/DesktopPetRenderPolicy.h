#pragma once
#include "CoreMinimal.h"
#include "Engine/Scene.h"
class USceneCaptureComponent2D;

/** SceneCapture 会重置这三个视图参数；在插件内部恢复项目实际值，不绑定具体渲染器。 */
struct FDesktopPetRenderPolicy
{
    EDynamicGlobalIlluminationMethod::Type GI=EDynamicGlobalIlluminationMethod::None;
    EReflectionMethod::Type Reflections=EReflectionMethod::None;
    float SurfaceResolution=1.f;
    /** 按引擎顺序读取项目默认、后处理体积、捕获组件；不会重复执行 Blendable。 */
    static FDesktopPetRenderPolicy Resolve(const USceneCaptureComponent2D* Capture);
    bool UsesLumen() const {return GI==EDynamicGlobalIlluminationMethod::Lumen||Reflections==EReflectionMethod::Lumen;}
};
