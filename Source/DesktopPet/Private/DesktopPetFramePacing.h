#pragma once
#include "CoreMinimal.h"
#include "DesktopPetTypes.h"
class ADesktopPetActor;

/** 独立桌宠的主循环预算；只协调帧率，不改材质、阴影、抗锯齿或捕获分辨率。 */
class FDesktopPetFramePacing
{
public:
    /** 每个运行实例登记自己的需求；多个实例采用最高需求，普通游戏窗口/PIE 不限速。 */
    static void Update(const ADesktopPetActor* Owner,const FDesktopPetConfig& Config,bool bInTray);
    /** 最后一个实例释放时撤销本插件的 CVar 层，恢复项目原设置。 */
    static void Release(const ADesktopPetActor* Owner);
    /** 模块卸载也必须撤销预算，不能给后续 PIE 留下全局副作用。 */
    static void Shutdown();
    /** 返回引擎实际有效的上限；0 表示未限制，控制台更高优先级可以覆盖插件。 */
    static float GetEffectiveLimit();
};
