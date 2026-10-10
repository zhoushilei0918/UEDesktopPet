#include "DesktopPetFramePacing.h"
#include "HAL/IConsoleManager.h"

namespace DesktopPetFramePacingPrivate
{
    /** 主循环属于整个进程，不能由单个 Actor 恢复其他实例仍需要的预算。 */
    struct FRequest
    {
        FDesktopPetConfig Config;
        bool bInTray=false;
        int32 TickMultiplier=2;
        double ObservationStart=0.;
        uint64 ObservationFrames=0;
    };
    TMap<const ADesktopPetActor*,FRequest> Requests;
    const FName PaceTag(TEXT("DesktopPet.FramePacing"));
    constexpr EConsoleVariableFlags PacePriority=ECVF_SetByPluginHighPriority;
    bool bActive=false;
    float OriginalLimit=0.f,AppliedLimit=-1.f;

    // 使用 UE 的带标签 CVar 层；用户在运行中输入的控制台值不会被还原逻辑覆盖。
    void Restore()
    {
        if(!bActive)return;
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))C->Unset(PacePriority,PaceTag);
        AppliedLimit=-1.f;bActive=false;
    }
    void Reconcile()
    {
        bool bAllowed=!GIsEditor&&!Requests.IsEmpty();
        int32 Requested=0;
        for(const auto& Pair:Requests)
        {
            const FRequest& R=Pair.Value;
            bAllowed&=R.Config.bLimitEngineFrameRate&&R.Config.bTransparentWindowEnabled&&R.Config.bHideGameWindow;
            // 从两倍输出频率开始，避免 30 FPS 桌宠始终让整个世界以 120 Hz 空转。
            // 最少 60 Hz 保留缩放/拖拽响应；回读较慢时 ObserveFrames 自动增加调度余量。
            Requested=FMath::Max(Requested,R.bInTray?R.Config.TrayFrameRate:FMath::Max(60,R.TickMultiplier*R.Config.TargetFrameRate));
        }
        if(!bAllowed){Restore();return;}
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        {
            if(!bActive)OriginalLimit=C->GetFloat();
            const float Limit=OriginalLimit>0.f?FMath::Min(OriginalLimit,float(Requested)):float(Requested);
            if(!bActive||!FMath::IsNearlyEqual(Limit,AppliedLimit))C->Set(Limit,PacePriority,PaceTag);
            AppliedLimit=Limit;bActive=true;
        }
    }
}
void FDesktopPetFramePacing::Update(const ADesktopPetActor* Owner,const FDesktopPetConfig& Config,bool bInTray)
{check(IsInGameThread());DesktopPetFramePacingPrivate::Requests.Add(Owner,{Config,bInTray});DesktopPetFramePacingPrivate::Reconcile();}
void FDesktopPetFramePacing::ObserveFrames(const ADesktopPetActor* Owner,uint64 PresentedFrames)
{
    using namespace DesktopPetFramePacingPrivate;
    check(IsInGameThread());
    FRequest* R=Requests.Find(Owner);
    if(!R||GIsEditor||R->bInTray||!R->Config.bLimitEngineFrameRate)return;
    const double Now=FPlatformTime::Seconds();
    if(R->ObservationStart<=0.){R->ObservationStart=Now;R->ObservationFrames=PresentedFrames;return;}
    const double Seconds=Now-R->ObservationStart;
    if(Seconds<2.)return;
    const double Rate=double(PresentedFrames-R->ObservationFrames)/Seconds;
    // 只向上补足余量，不反复升降造成动画节奏抖动；配置更改/托盘恢复后重新评估。
    if(Rate<R->Config.TargetFrameRate*.99&&R->TickMultiplier<4){++R->TickMultiplier;Reconcile();}
    R->ObservationStart=Now;R->ObservationFrames=PresentedFrames;
}
void FDesktopPetFramePacing::Release(const ADesktopPetActor* Owner)
{check(IsInGameThread());DesktopPetFramePacingPrivate::Requests.Remove(Owner);DesktopPetFramePacingPrivate::Reconcile();}
void FDesktopPetFramePacing::Shutdown(){DesktopPetFramePacingPrivate::Requests.Reset();DesktopPetFramePacingPrivate::Restore();}
float FDesktopPetFramePacing::GetEffectiveLimit()
{const auto* C=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));return C?C->GetFloat():0.f;}
