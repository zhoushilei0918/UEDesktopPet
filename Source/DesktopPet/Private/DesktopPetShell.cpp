#include "DesktopPetShell.h"

DEFINE_LOG_CATEGORY_STATIC(LogDesktopPetShell,Log,All);

void FDesktopPetShell::Attach(HWND InWindow)
{
    Window=InWindow;
    const HRESULT Init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bUninitializeCOM=SUCCEEDED(Init);
    // UE 已用其他 COM 模型初始化时仍可创建任务栏对象，不重复反初始化该线程。
    if(SUCCEEDED(CoCreateInstance(CLSID_TaskbarList,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&Taskbar))))
        if(FAILED(Taskbar->HrInit())){Taskbar->Release();Taskbar=nullptr;}
    TaskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    TaskbarButtonCreated=RegisterWindowMessageW(L"TaskbarButtonCreated");
    ChangeWindowMessageFilterEx(Window,TaskbarCreated,MSGFLT_ALLOW,nullptr);
    ChangeWindowMessageFilterEx(Window,TaskbarButtonCreated,MSGFLT_ALLOW,nullptr);
    // 继承打包 EXE 的应用图标；项目更换应用图标后无需在插件中绑定角色资产。
    wchar_t Executable[32768]{};GetModuleFileNameW(nullptr,Executable,UE_ARRAY_COUNT(Executable));
    bOwnIcon=ExtractIconExW(Executable,0,nullptr,&Icon,1)>0&&Icon;
    if(!Icon)Icon=LoadIconW(nullptr,IDI_APPLICATION);
    SendMessageW(Window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(Icon));
}
void FDesktopPetShell::ApplyTaskbar()
{
    // 保持普通可捕获窗口身份，通过 Shell 的正式接口单独删除/恢复任务栏按钮。
    // TaskbarButtonCreated 会再次校准，避免 Shell 异步创建按钮后覆盖这里的设置。
    bTaskbarPolicyApplied=Taskbar&&SUCCEEDED(bShowTaskbar&&!bInTray&&IsWindowVisible(Window)?Taskbar->AddTab(Window):Taskbar->DeleteTab(Window));
}
void FDesktopPetShell::ApplyFlags(bool bShowInTaskbar,bool bAllowCaptureIn)
{
    bShowTaskbar=bShowInTaskbar;bAllowCapture=bAllowCaptureIn;
    ApplyTaskbar();
    // 这是 Windows 捕获排除提示，不是进程隐藏，也不承诺阻止所有第三方捕获路径。
    bCapturePolicyApplied=SetWindowDisplayAffinity(Window,bAllowCapture?WDA_NONE:WDA_EXCLUDEFROMCAPTURE)!=0;
    if(!bCapturePolicyApplied)UE_LOG(LogDesktopPetShell,Warning,TEXT("Window capture policy failed: %u"),GetLastError());
}
NOTIFYICONDATAW FDesktopPetShell::MakeIconData() const
{
    NOTIFYICONDATAW Data{};Data.cbSize=sizeof(Data);Data.hWnd=Window;Data.uID=1;
    Data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP|NIF_SHOWTIP;Data.uCallbackMessage=CallbackMessage;Data.hIcon=Icon;
    FCString::Strncpy(Data.szTip,TEXT("Desktop Pet — 双击恢复桌宠"),UE_ARRAY_COUNT(Data.szTip));
    return Data;
}
bool FDesktopPetShell::AddTrayIcon()
{
    auto Data=MakeIconData();
    // 重复的 TaskbarCreated 通知不一定意味着图标已删除；允许更新仍存在的同一入口。
    if(!Shell_NotifyIconW(NIM_ADD,&Data)&&!Shell_NotifyIconW(NIM_MODIFY,&Data))return false;
    bIconAdded=true;Data.uVersion=NOTIFYICON_VERSION_4;
    if(!Shell_NotifyIconW(NIM_SETVERSION,&Data)){DeleteTrayIcon();return false;}
    return true;
}
void FDesktopPetShell::DeleteTrayIcon()
{
    if(!bIconAdded)return;
    auto Data=MakeIconData();Shell_NotifyIconW(NIM_DELETE,&Data);bIconAdded=false;
}
bool FDesktopPetShell::HideToTray()
{
    if(bInTray)return true;
    if(!Window||!AddTrayIcon())return false;
    bInTray=true;ShowWindow(Window,SW_HIDE);ApplyTaskbar();return true;
}
bool FDesktopPetShell::RestoreFromTray(bool bShowWindow)
{
    if(!bInTray)return false;
    bInTray=false;
    if(bShowWindow)ShowWindow(Window,SW_SHOWNOACTIVATE);
    ApplyTaskbar();DeleteTrayIcon();return true;
}
FDesktopPetShell::EAction FDesktopPetShell::HandleMessage(UINT Message,WPARAM WParam,LPARAM LParam)
{
    if(Message==TaskbarCreated)
    {
        bIconAdded=false;
        // Explorer 重启后重新登记；如果恢复入口不可建立，自动恢复桌宠，避免不可操作。
        if(bInTray&&!AddTrayIcon())return EAction::Restore;
        ApplyTaskbar();
    }
    if(Message==TaskbarButtonCreated)ApplyTaskbar();
    if(Message!=CallbackMessage||!bInTray||HIWORD(LParam)!=1)return EAction::None;
    const UINT Event=LOWORD(LParam);
    if(Event==WM_LBUTTONDBLCLK||Event==NIN_KEYSELECT)return EAction::Restore;
    if(Event==WM_CONTEXTMENU)
    {
        // 简单的恢复/退出菜单提供键盘及无双击设备的备用入口。
        HMENU Menu=CreatePopupMenu();if(!Menu)return EAction::None;
        AppendMenuW(Menu,MF_STRING,1,L"显示桌宠");AppendMenuW(Menu,MF_STRING,2,L"关闭桌宠");
        POINT Point{};GetCursorPos(&Point);SetForegroundWindow(Window);
        const UINT Choice=TrackPopupMenu(Menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,Point.x,Point.y,0,Window,nullptr);
        DestroyMenu(Menu);PostMessageW(Window,WM_NULL,0,0);
        if(Choice==1)return EAction::Restore;
        if(Choice==2)return EAction::Close;
    }
    return EAction::None;
}
void FDesktopPetShell::Detach()
{
    DeleteTrayIcon();bInTray=false;
    if(Taskbar){Taskbar->Release();Taskbar=nullptr;}
    if(bOwnIcon&&Icon)DestroyIcon(Icon);
    Icon=nullptr;bOwnIcon=false;Window=nullptr;
    if(bUninitializeCOM){CoUninitialize();bUninitializeCOM=false;}
}
