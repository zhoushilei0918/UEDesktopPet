#pragma once
#include "CoreMinimal.h"
#include "Math/Float16Color.h"
#include "DesktopPetTypes.h"
/** 纯像素合成器：不依赖角色、UMG 类、Windows HWND 或 Niagara 业务。 */
namespace DesktopPetCompositor
{
    /** 输入 HDR 反透明度图和预乘 UI 图；输出 Windows 所需的 BGRA 预乘像素。 */
    void Composite(const TArray<FFloat16Color>& Scene,const TArray<FColor>& UI,
                   int32 Width,int32 Height,const FDesktopPetConfig& Config,
                   TArray<FColor>& Out,TArray<uint8>& SceneAlpha,
                   const TArray<FFloat16Color>* FinalColor=nullptr);
}
