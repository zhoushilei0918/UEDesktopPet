#pragma once
#include "CoreMinimal.h"
#include "Math/Float16Color.h"
#include "DesktopPetTypes.h"
/** 纯像素合成器：不依赖角色、UMG 类、Windows HWND 或 Niagara 业务。 */
namespace DesktopPetCompositor
{
    /**
     * 在 CPU 合成预乘 BGRA 颜色和独立场景命中 Alpha。
     * Opacity - 超采样反透明度；
     * UI - 窗口分辨率的预乘 UI 像素；
     * Width、Height - 输出窗口的物理像素宽高；
     * Config - 本帧配置快照；
     * Out - 输出给 Windows 的颜色数组；
     * SceneAlpha - 输出给场景拾取使用的 Alpha，不包含 UI；
     * FinalColor - 引擎后处理后的超采样颜色；
     * Pet - 可复用的场景颜色暂存数组。
     */
    void Composite(const TArray<FFloat16>& Opacity,
                   const TArray<FColor>& UI,
                   int32 Width,
                   int32 Height,
                   const FDesktopPetConfig& Config,
                   TArray<FColor>& Out,
                   TArray<uint8>& SceneAlpha,
                   const TArray<FFloat16Color>& FinalColor,
                   TArray<FColor>& Pet);
} // namespace DesktopPetCompositor
