#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Windows/WindowsHWrapper.h"

/** 独立的按键查询状态；不参与拖拽、旋转、Slate 路由或焦点申请。 */
class FDesktopPetInputState
{
public:
    /** 在现有输入回调之前记录鼠标消息，保证 Press/Release 回调查询到更新后的状态。 */
    void HandleMessage(UINT Message, WPARAM WParam);
    /** 清理没有收到窗口外抬起消息的鼠标键；仅检查本窗口曾收到的按键。 */
    void Refresh();
    /** 取消交互、收起、停止或失焦后，不保留鼠标按住状态。 */
    void Reset();
    /** 鼠标需要本实例收到过按下；键盘需要本实例当前拥有焦点。两者均校验物理状态。 */
    bool IsKeyDown(uint32 VirtualKey, bool bKeyboardFocus, bool bPhysicallyDown) const;
    /** 无 Windows 按键映射的轴、滚轮、手柄键等返回 0。 */
    static uint32 GetVirtualKey(FKey Key);
    /** Windows 的异步查询使用物理左右键；窗口消息使用主/次按钮，需兼容系统交换按钮设置。 */
    static uint32 GetPhysicalKey(uint32 VirtualKey);

private:
    static bool IsMouseKey(uint32 VirtualKey);
    void SetMouseKey(uint32 VirtualKey, bool bDown);
    TSet<uint32> MouseKeys;
};
