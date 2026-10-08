#pragma once
#include "CoreMinimal.h"

/** 本进程 UE 游戏窗口的防护状态；用于运行时检查和自动验收。 */
struct FDesktopPetGameWindowState
{
    int32 WindowCount=0;
    int32 VisibleCount=0;
    int32 EnabledCount=0;
    int32 TaskbarEligibleCount=0;
    int32 PreventedShowCount=0;
};

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
    /** 查询当前策略。 */
    static bool IsHidden();
    /** 查询防护的所有 UE HWND，供测试验证持续隐藏。 */
    static FDesktopPetGameWindowState GetState();
};
