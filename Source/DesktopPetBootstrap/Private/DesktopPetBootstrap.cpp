#include "DesktopPetWindowGuard.h"
#include "DesktopPetStartupMemory.h"
#include "Modules/ModuleManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Windows/WindowsHWrapper.h"

namespace
{
    // 只记录本游戏线程中 UnrealWindow 类的窗口；桌宠自己的 HWND 不在此名单内。
    struct FGuardedWindow
    {
        WNDPROC Original=nullptr;
        LONG_PTR Style=0,ExStyle=0;
    };
    TMap<HWND,FGuardedWindow> GuardedWindows;
    HHOOK CreateHook=nullptr;
    bool bHide=true;
    int32 PreventedShows=0;

    // 用窗口过程拦截后续的 ShowWindow 和样式恢复，解决 UE 首帧再次显示窗口的问题。
    LRESULT CALLBACK GuardProc(HWND H,UINT M,WPARAM W,LPARAM L)
    {
        const FGuardedWindow* Record=GuardedWindows.Find(H);
        if(!Record)return DefWindowProcW(H,M,W,L);
        WNDPROC Original=Record->Original;
        if(bHide)
        {
            if(M==WM_WINDOWPOSCHANGING)
            {
                LRESULT Result=CallWindowProcW(Original,H,M,W,L);
                auto* P=reinterpret_cast<WINDOWPOS*>(L);
                if(P->flags&SWP_SHOWWINDOW)++PreventedShows;
                P->flags=(P->flags&~SWP_SHOWWINDOW)|SWP_HIDEWINDOW|SWP_NOACTIVATE;
                return Result;
            }
            if(M==WM_STYLECHANGING)
            {
                LRESULT Result=CallWindowProcW(Original,H,M,W,L);
                auto* S=reinterpret_cast<STYLESTRUCT*>(L);
                if(W==GWL_STYLE)S->styleNew=(S->styleNew&~WS_VISIBLE)|WS_DISABLED;
                if(W==GWL_EXSTYLE)S->styleNew=(S->styleNew&~WS_EX_APPWINDOW)|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE;
                return Result;
            }
            if(M==WM_MOUSEACTIVATE)return MA_NOACTIVATEANDEAT;
            if(M==WM_NCHITTEST)return HTTRANSPARENT;
            if((M>=WM_KEYFIRST&&M<=WM_KEYLAST)||(M>=WM_MOUSEFIRST&&M<=WM_MOUSELAST)||M==WM_INPUT)return 0;
            if(M==WM_ENABLE&&W){EnableWindow(H,false);return 0;}
        }
        LRESULT Result=CallWindowProcW(Original,H,M,W,L);
        if(M==WM_NCDESTROY)GuardedWindows.Remove(H);
        return Result;
    }

    // CBT 在 WM_NCCREATE / 首次显示之前执行，因此不会出现游戏窗口先闪现再隐藏的间隙。
    LRESULT CALLBACK CreateProc(int Code,WPARAM W,LPARAM L)
    {
        if(Code==HCBT_CREATEWND)
        {
            HWND H=reinterpret_cast<HWND>(W);
            wchar_t ClassName[128]{};
            GetClassNameW(H,ClassName,128);
            if(FCString::Strcmp(ClassName,TEXT("UnrealWindow"))==0)
            {
                auto* Info=reinterpret_cast<CBT_CREATEWNDW*>(L);
                FGuardedWindow Record;
                Record.Style=Info->lpcs->style;
                Record.ExStyle=Info->lpcs->dwExStyle;
                Record.Original=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(H,GWLP_WNDPROC));
                GuardedWindows.Add(H,Record);
                SetWindowLongPtrW(H,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(GuardProc));
                if(bHide)
                {
                    Info->lpcs->style=(Info->lpcs->style&~WS_VISIBLE)|WS_DISABLED;
                    Info->lpcs->dwExStyle=(Info->lpcs->dwExStyle&~WS_EX_APPWINDOW)|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE;
                    SetWindowLongPtrW(H,GWL_STYLE,(GetWindowLongPtrW(H,GWL_STYLE)&~WS_VISIBLE)|WS_DISABLED);
                    SetWindowLongPtrW(H,GWL_EXSTYLE,(GetWindowLongPtrW(H,GWL_EXSTYLE)&~WS_EX_APPWINDOW)|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE);
                }
            }
        }
        return CallNextHookEx(CreateHook,Code,W,L);
    }
}

// 打包游戏和 -game 独立游戏启用；编辑器工作台、烘焙和专用服务器不安装防护。
void FDesktopPetWindowGuard::Startup()
{
#if !IS_PROGRAM
#if WITH_EDITOR
    if(!FParse::Param(FCommandLine::Get(),TEXT("game")))return;
#endif
    if(IsRunningCommandlet()||IsRunningDedicatedServer())return;
    if(GConfig)GConfig->GetBool(TEXT("/Script/DesktopPet.DesktopPetSettings"),TEXT("bHideGameWindow"),bHide,GGameIni);
    bool Transparent=true;
    if(GConfig)GConfig->GetBool(TEXT("/Script/DesktopPet.DesktopPetSettings"),TEXT("bTransparentWindowEnabled"),Transparent,GGameIni);
    bHide=bHide&&Transparent&&!FParse::Param(FCommandLine::Get(),TEXT("PetOpaque"));
    // 游戏窗口防护启用时也关闭启动画面，避免桌宠启动前显示 UE Splash。
    if(bHide&&!FParse::Param(FCommandLine::Get(),TEXT("nosplash")))FCommandLine::Append(TEXT(" -nosplash"));
    CreateHook=SetWindowsHookExW(WH_CBT,CreateProc,nullptr,GetCurrentThreadId());
#endif
}

// 模块卸载前还原窗口过程。正常关闭时 HWND 通常已经被引擎销毁。
void FDesktopPetWindowGuard::Shutdown()
{
    if(CreateHook){UnhookWindowsHookEx(CreateHook);CreateHook=nullptr;}
    for(auto& Pair:GuardedWindows)if(IsWindow(Pair.Key))SetWindowLongPtrW(Pair.Key,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Pair.Value.Original));
    GuardedWindows.Empty();
}

// 蓝图运行时切换策略时，统一修改可见性、可激活性、输入和任务栏状态。
void FDesktopPetWindowGuard::SetHidden(bool Hidden)
{
    bHide=Hidden;
    for(const auto& Pair:GuardedWindows)
    {
        HWND H=Pair.Key;
        if(!IsWindow(H))continue;
        if(bHide)
        {
            ShowWindow(H,SW_HIDE);
            EnableWindow(H,false);
            SetWindowLongPtrW(H,GWL_EXSTYLE,(GetWindowLongPtrW(H,GWL_EXSTYLE)&~WS_EX_APPWINDOW)|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE);
        }
        else
        {
            SetWindowLongPtrW(H,GWL_STYLE,Pair.Value.Style&~WS_DISABLED);
            SetWindowLongPtrW(H,GWL_EXSTYLE,Pair.Value.ExStyle);
            EnableWindow(H,true);
            ShowWindow(H,SW_SHOWNOACTIVATE);
        }
        SetWindowPos(H,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
}
bool FDesktopPetWindowGuard::IsHidden(){return bHide;}

// 验收直接读取真实 HWND 状态，不把“发出了隐藏命令”当成隐藏成功。
FDesktopPetGameWindowState FDesktopPetWindowGuard::GetState()
{
    FDesktopPetGameWindowState S;
    S.PreventedShowCount=PreventedShows;
    for(const auto& Pair:GuardedWindows)if(IsWindow(Pair.Key))
    {
        ++S.WindowCount;
        S.VisibleCount+=IsWindowVisible(Pair.Key)?1:0;
        S.EnabledCount+=IsWindowEnabled(Pair.Key)?1:0;
        const LONG_PTR E=GetWindowLongPtrW(Pair.Key,GWL_EXSTYLE);
        S.TaskbarEligibleCount+=(E&WS_EX_TOOLWINDOW)?0:1;
    }
    return S;
}

/** PostConfigInit 加载，保证防护先于任何游戏窗口创建。 */
class FDesktopPetBootstrapModule : public IModuleInterface
{
public:
    virtual void StartupModule() override { FDesktopPetWindowGuard::Startup(); FDesktopPetStartupMemory::Startup(); }
    virtual void ShutdownModule() override { FDesktopPetStartupMemory::Shutdown(); FDesktopPetWindowGuard::Shutdown(); }
};
IMPLEMENT_MODULE(FDesktopPetBootstrapModule,DesktopPetBootstrap)
