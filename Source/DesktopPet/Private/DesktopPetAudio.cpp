#include "DesktopPetAudio.h"
#include "Misc/App.h"

namespace DesktopPetAudioPrivate
{
    /** 只保存实例请求的倍率，不持有 Actor 或音频资源的强引用。 */
    TMap<const ADesktopPetActor*, float> Requests;
    bool bActive = false, bAppliedVolume = false;
    float OriginalUnfocused = 0.f, OriginalVolume = 1.f, AppliedUnfocused = 0.f;

    /** 不逐帧写 ini；FApp 的 Setter 只更新配置缓存，磁盘存储仍由 PetSaveRuntimeConfig 负责。 */
    void Reconcile()
    {
        if (Requests.IsEmpty())
        {
            if (!bActive)
            {
                return;
            }
            // 其他系统已显式接管后台倍率时不覆盖它；正常退出恢复接管前的配置缓存值。
            if (FApp::GetUnfocusedVolumeMultiplier() == AppliedUnfocused)
            {
                FApp::SetUnfocusedVolumeMultiplier(OriginalUnfocused);
            }
            if (bAppliedVolume && FApp::GetVolumeMultiplier() == AppliedUnfocused)
            {
                FApp::SetVolumeMultiplier(
                    FApp::HasFocus() ? 1.f : (GIsEditor ? OriginalVolume : FApp::GetUnfocusedVolumeMultiplier()));
            }
            bActive = false;
            bAppliedVolume = false;
            return;
        }
        if (!bActive)
        {
            OriginalUnfocused = FApp::GetUnfocusedVolumeMultiplier();
            OriginalVolume = FApp::GetVolumeMultiplier();
            bActive = true;
        }
        AppliedUnfocused = 0.f;
        for (const auto& Entry : Requests)
        {
            AppliedUnfocused = FMath::Max(AppliedUnfocused, Entry.Value);
        }
        if (FApp::GetUnfocusedVolumeMultiplier() != AppliedUnfocused)
        {
            FApp::SetUnfocusedVolumeMultiplier(AppliedUnfocused);
        }
    }
} // namespace DesktopPetAudioPrivate

void FDesktopPetAudio::Update(const ADesktopPetActor* Owner, const FDesktopPetConfig& Config)
{
    check(IsInGameThread());
    const float Volume = FMath::Clamp(
        FMath::IsFinite(Config.BackgroundVolumeMultiplier) ? Config.BackgroundVolumeMultiplier : 1.f, 0.f, 1.f);
    DesktopPetAudioPrivate::Requests.Add(Owner, Config.bAllowBackgroundAudio ? Volume : 0.f);
    DesktopPetAudioPrivate::Reconcile();
    Tick();
}

void FDesktopPetAudio::Tick()
{
    check(IsInGameThread());
    using namespace DesktopPetAudioPrivate;
    // 前台音量由引擎和项目原有音频系统处理；后台不暂停声音，仅应用全局倍率。
    // PIE 的 EditorEngine 会每帧按编辑器偏好写入 0，因此须在世界音频更新前补齐倍率。
    if (bActive && !FApp::HasFocus())
    {
        FApp::SetVolumeMultiplier(AppliedUnfocused);
        bAppliedVolume = true;
    }
}

void FDesktopPetAudio::Release(const ADesktopPetActor* Owner)
{
    check(IsInGameThread());
    DesktopPetAudioPrivate::Requests.Remove(Owner);
    DesktopPetAudioPrivate::Reconcile();
    Tick();
}

void FDesktopPetAudio::Shutdown()
{
    DesktopPetAudioPrivate::Requests.Reset();
    DesktopPetAudioPrivate::Reconcile();
}
