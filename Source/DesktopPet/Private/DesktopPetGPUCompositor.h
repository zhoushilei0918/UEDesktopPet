#pragma once
#include "CoreMinimal.h"
#include "DesktopPetTypes.h"
class FRHICommandListImmediate;
class FRHITexture;
class FRHIGPUBufferReadback;

/** 通用 GPU 像素管线：输入最终颜色、反透明度和 UI，不引用任何项目资源或场景对象。 */
namespace DesktopPetGPUCompositor
{
    /** CPU 路径作为兼容回退；控制台开关只用于本插件，不改变项目渲染设置。 */
    bool IsEnabled(const FDesktopPetConfig& Config);
    /** 每个输出像素 8 字节：Windows BGRA 和不含 UI 的场景 Alpha。 */
    struct FPackedPixel { uint32 BGRA;uint32 SceneAlpha; };
    /** 渲染线程异步提交两遍计算及回读，不等待 GPU，不刷新渲染线程。 */
    void Enqueue(FRHICommandListImmediate& Cmd,FRHITexture* Color,FRHITexture* Opacity,FRHITexture* UI,
                 FIntPoint Size,const FDesktopPetConfig& Config,FRHIGPUBufferReadback& Readback);
}
