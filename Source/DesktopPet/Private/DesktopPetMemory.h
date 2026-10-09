#pragma once
#include "CoreMinimal.h"
class ADesktopPetActor;
struct FDesktopPetConfig;

/** 进程级缓存预算协调器：只依赖公开 CVar，不调用 Renderer 私有结构，也不更改项目 ini。 */
class FDesktopPetMemory
{
public:
    /** 在游戏线程登记或更新宿主配置；多个宿主取较大容量，有宿主要求原始预算则全部恢复。 */
    static void Update(const ADesktopPetActor* Owner,const FDesktopPetConfig& Config);
    /** 销毁、停止或切图时撤销登记；最后一个宿主离开后恢复原来的 CVar 层。 */
    static void Release(const ADesktopPetActor* Owner);
    /** 查询预算是否处于协商启用状态；更高优先级控制台设置仍会被尊重。 */
    static bool IsActive(const ADesktopPetActor* Owner);
    /** 模块退出的兜底清理，不遗留全局配置。 */
    static void Shutdown();
};
