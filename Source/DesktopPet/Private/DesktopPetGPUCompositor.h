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
    /** 颜色平面每像素 4 字节，随后是每像素 1 字节的独立场景 Alpha；仅尾部补齐到 4 字节。 */
    inline uint32 GetReadbackBytes(uint32 PixelCount)
    {
        return PixelCount * sizeof(uint32) + Align(PixelCount, uint32(4));
    }
    /**
     * 在渲染线程提交颜色合成和异步回读，不等待 GPU。
     * Cmd - 当前渲染命令列表；
     * Color - 引擎后处理后的线性颜色纹理；
     * Opacity - 与颜色同帧的反透明度纹理；
     * UI - 预乘 UI 纹理，可为空；
     * Size - 最终窗口的物理像素尺寸；
     * Config - 本帧使用的配置快照；
     * Readback - 接收紧密颜色/Alpha 数据的异步回读对象。
     */
    void Enqueue(FRHICommandListImmediate& Cmd,
                 FRHITexture* Color,
                 FRHITexture* Opacity,
                 FRHITexture* UI,
                 FIntPoint Size,
                 const FDesktopPetConfig& Config,
                 FRHIGPUBufferReadback& Readback);
} // namespace DesktopPetGPUCompositor
