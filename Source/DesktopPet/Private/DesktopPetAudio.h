#pragma once

#include "DesktopPetTypes.h"

class ADesktopPetActor;

/** 后台音量属于整个进程；统一协调多个桌宠，不关联任何音乐蓝图或音频资产。 */
class FDesktopPetAudio
{
public:
    /** 注册或更新运行中的实例请求，取所有有效后台倍率的最大值。 */
    static void Update(const ADesktopPetActor* Owner, const FDesktopPetConfig& Config);
    /** 世界音频更新前临时应用后台音量，同时覆盖 PIE 的后台静音策略。 */
    static void Tick();
    /** 停止单个桌宠；只有最后一个实例退出时才恢复原始引擎配置。 */
    static void Release(const ADesktopPetActor* Owner);
    /** 模块卸载时解除全部请求并恢复接管前的状态。 */
    static void Shutdown();
};
