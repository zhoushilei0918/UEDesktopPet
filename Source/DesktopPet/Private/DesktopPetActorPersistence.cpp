#include "DesktopPetActor.h"
#include "DesktopPetConfigStorage.h"
#include "DesktopPetRuntime.h"

/** 只在第一次进入游戏/启动窗口时尝试读取；无存档时安静回退，不产生每帧磁盘访问。 */
void ADesktopPetActor::TryAutoLoadSavedRuntimeConfig()
{
    if (bSavedConfigLoadAttempted)
    {
        return;
    }
    bSavedConfigLoadAttempted = true;
    if (bHasConfig || !bAutoLoadSavedConfig || IsRunningCommandlet() || !PetHasSavedRuntimeConfig())
    {
        return;
    }
    FString Error;
    if (!PetLoadRuntimeConfig(Error))
    {
        UE_LOG(LogDesktopPet, Warning, TEXT("Saved configuration ignored: %s"), *Error);
    }
}

/** 快照读取会保留尚未 Tick 的 Setter 值；滚轮动画中则保存此刻实际显示的倍率和位置。 */
bool ADesktopPetActor::PetSaveRuntimeConfig(FString& Error)
{
    return DesktopPetConfigStorage::Save(SavedConfigProfile, PetGetRuntimeConfig(), Error);
}

bool ADesktopPetActor::PetLoadRuntimeConfig(FString& Error)
{
    FDesktopPetConfig Loaded = bOverrideDefaultConfig ? InitialConfig : PetGetDefaultRuntimeConfig();
    if (!DesktopPetConfigStorage::Load(SavedConfigProfile, Loaded, Error))
    {
        return false;
    }
    // 不走保持旧中心的 Setter 路径，避免把已保存的左上角位置再次平移。
    if (Runtime)
    {
        Runtime->ZoomCommitPending = false;
    }
    DesiredConfig = Loaded;
    bHasConfig = true;
    bConfigPending = true;
    bCenterAnchorValid = false;
    return true;
}

bool ADesktopPetActor::PetHasSavedRuntimeConfig() const
{
    return DesktopPetConfigStorage::Exists(SavedConfigProfile);
}

bool ADesktopPetActor::PetDeleteSavedRuntimeConfig(FString& Error)
{
    return DesktopPetConfigStorage::Delete(SavedConfigProfile, Error);
}

FString ADesktopPetActor::PetGetSavedConfigPath() const
{
    return DesktopPetConfigStorage::GetPath(SavedConfigProfile);
}
