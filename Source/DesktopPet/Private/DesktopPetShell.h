#pragma once
#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"
#include <shellapi.h>
#include <shobjidl.h>

/** Windows 系统集成只管理 HWND、任务栏和托盘，不依赖角色、Widget 或捕获算法。 */
class FDesktopPetShell
{
public:
    enum class EAction { None, Restore, Close };
    /** 初始化 COM 与窗口身份。所有调用都在创建 HWND 的游戏线程执行。 */
    void Attach(HWND InWindow);
    /** 任务栏按钮与捕获许可独立；不设置 ToolWindow、不隐藏或伪装系统进程。 */
    void ApplyFlags(bool bShowInTaskbar,bool bAllowCapture);
    /** 先成功创建托盘入口再隐藏窗口，失败时保留可见窗口，防止失去恢复入口。 */
    bool HideToTray();
    /** 恢复原 HWND 并移除托盘图标；保持其原有大小、位置与像素。 */
    bool RestoreFromTray(bool bShowWindow);
    /** 处理双击、键盘激活与 Explorer 重启；菜单动作交给宿主执行。 */
    EAction HandleMessage(UINT Message,WPARAM WParam,LPARAM LParam);
    bool IsInTray() const {return bInTray;}
    bool IsCapturePolicyApplied() const {return bCapturePolicyApplied;}
    bool IsTaskbarPolicyApplied() const {return bTaskbarPolicyApplied;}
    /** 必须早于 HWND 销毁调用；析构再次调用也安全。 */
    void Detach();
    ~FDesktopPetShell(){Detach();}
private:
    HWND Window=nullptr;
    HICON Icon=nullptr;
    ITaskbarList* Taskbar=nullptr;
    bool bOwnIcon=false,bUninitializeCOM=false,bInTray=false,bIconAdded=false;
    bool bShowTaskbar=true,bAllowCapture=true,bCapturePolicyApplied=false,bTaskbarPolicyApplied=false;
    UINT TaskbarCreated=0,TaskbarButtonCreated=0;
    static constexpr UINT CallbackMessage=WM_APP+0x470;
    /** 每个窗口固定 ID，不同实例由 HWND 区分。 */
    NOTIFYICONDATAW MakeIconData() const;
    bool AddTrayIcon();
    void DeleteTrayIcon();
    void ApplyTaskbar();
};
