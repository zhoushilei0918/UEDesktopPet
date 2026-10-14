#include "DesktopPetFramePacing.h"
#include "HAL/IConsoleManager.h"

namespace DesktopPetFramePacingPrivate
{
    /** 主循环属于整个进程，不能由单个 Actor 恢复其他实例仍需要的预算。 */
    struct FRequest
    {
        FDesktopPetConfig Config;
        bool bInTray = false;
    };
    TMap<const ADesktopPetActor*, FRequest> Requests;
    const FName PaceTag(TEXT("DesktopPet.FramePacing"));
    constexpr EConsoleVariableFlags PacePriority = ECVF_SetByPluginHighPriority;
    bool bActive = false;
    float OriginalLimit = 0.f, AppliedLimit = -1.f;

    /** 使用 UE 的带标签 CVar 层；用户在运行中输入的控制台值不会被还原逻辑覆盖。 */
    void Restore()
    {
        if (!bActive)
        {
            return;
        }
        if (auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        {
            C->Unset(PacePriority, PaceTag);
        }
        AppliedLimit = -1.f;
        bActive = false;
    }
    void Reconcile()
    {
        bool bAllowed = !GIsEditor && !Requests.IsEmpty();
        int32 Requested = 0;
        for (const auto& Pair : Requests)
        {
            const FRequest& R = Pair.Value;
            bAllowed &=
                R.Config.bLimitEngineFrameRate && R.Config.bTransparentWindowEnabled && R.Config.bHideGameWindow;
            // 回读由有界双缓冲并行推进，不因一次旋转、编译或慢帧永久提升整个世界的 Tick。
            // 两倍输出频率、最低 60 Hz 保留输入与缩放响应；GPU 繁忙时不突发补帧。
            Requested = FMath::Max(Requested,
                                   R.bInTray ? R.Config.TrayFrameRate : FMath::Max(60, 2 * R.Config.TargetFrameRate));
        }
        if (!bAllowed)
        {
            Restore();
            return;
        }
        if (auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
        {
            if (!bActive)
            {
                OriginalLimit = C->GetFloat();
            }
            const float Limit = OriginalLimit > 0.f ? FMath::Min(OriginalLimit, float(Requested)) : float(Requested);
            if (!bActive || !FMath::IsNearlyEqual(Limit, AppliedLimit))
            {
                C->Set(Limit, PacePriority, PaceTag);
            }
            AppliedLimit = Limit;
            bActive = true;
        }
    }
} // namespace DesktopPetFramePacingPrivate
void FDesktopPetFramePacing::Update(const ADesktopPetActor* Owner, const FDesktopPetConfig& Config, bool bInTray)
{
    check(IsInGameThread());
    DesktopPetFramePacingPrivate::Requests.Add(Owner, {Config, bInTray});
    DesktopPetFramePacingPrivate::Reconcile();
}
void FDesktopPetFramePacing::Release(const ADesktopPetActor* Owner)
{
    check(IsInGameThread());
    DesktopPetFramePacingPrivate::Requests.Remove(Owner);
    DesktopPetFramePacingPrivate::Reconcile();
}
void FDesktopPetFramePacing::Shutdown()
{
    DesktopPetFramePacingPrivate::Requests.Reset();
    DesktopPetFramePacingPrivate::Restore();
}
float FDesktopPetFramePacing::GetEffectiveLimit()
{
    const auto* C = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
    return C ? C->GetFloat() : 0.f;
}
