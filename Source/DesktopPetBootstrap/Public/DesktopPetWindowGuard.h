#pragma once
#include "CoreMinimal.h"

/** 在游戏 HWND 创建前安装防护，不隐藏或禁用编辑器主窗口。 */
class DESKTOPPETBOOTSTRAP_API FDesktopPetWindowGuard
{
public:
    /** 初始化本线程 CBT 钩子，早于引擎创建游戏窗口。 */
    static void Startup();
    /** 卸载钩子并恢复仍存在的窗口过程，避免悬空函数指针。 */
    static void Shutdown();
    /** 动态控制隐藏；会同步禁用输入、移除任务栏/Alt+Tab 资格。 */
    static void SetHidden(bool bHidden);
};
