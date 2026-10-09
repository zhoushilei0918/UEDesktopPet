#include "DesktopPetStartupMemory.h"
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogDesktopPetMemoryBootstrap,Log,All);
namespace
{
    const FName StartupTag(TEXT("DesktopPet.StartupAllocation"));
    constexpr EConsoleVariableFlags Priority=ECVF_SetByPluginHighPriority;
    FDelegateHandle ModulesHandle;
    struct FStartupValue
    {
        const TCHAR* Name;
        int32 Value;
        bool bCapacity;
        bool bApplied=false;
    };
    FStartupValue Values[]={
        {TEXT("RHI.TransientAllocator.MinimumHeapSize"),16,true},
        {TEXT("d3d12.PoolAllocator.ReadOnlyTextureVRAMPoolSize"),16*1024*1024,true},
        {TEXT("fx.Cascade.GpuSpriteDynamicAllocations"),1,false}
    };
    // 后端模块可能尚未加载；等实际模块注册 CVar 后再设置，不注册同名假变量、不强制加载 D3D12。
    void ApplyAvailable()
    {
        for(auto& V:Values)
        {
            if(V.bApplied)continue;
            auto* C=IConsoleManager::Get().FindConsoleVariable(V.Name);
            if(!C)continue;
            const int32 Requested=V.bCapacity?FMath::Min(C->GetInt(),V.Value):V.Value;
            C->Set(Requested,Priority,StartupTag);V.bApplied=true;
            UE_LOG(LogDesktopPetMemoryBootstrap,Log,TEXT("Startup allocation: %s=%d (requested %d)"),V.Name,C->GetInt(),Requested);
        }
    }
}

// 只读取 ini 的原始字段；此阶段 UObject 尚未就绪，不调用 GetDefault 或创建设置对象。
void FDesktopPetStartupMemory::Startup()
{
#if !IS_PROGRAM
#if WITH_EDITOR
    if(!FParse::Param(FCommandLine::Get(),TEXT("game")))return;
#endif
    if(IsRunningCommandlet()||IsRunningDedicatedServer()||!GConfig)return;
    if(FParse::Param(FCommandLine::Get(),TEXT("PetOpaque"))||FParse::Param(FCommandLine::Get(),TEXT("PetFullMemory")))return;
    const TCHAR* Section=TEXT("/Script/DesktopPet.DesktopPetSettings");
    bool bEnabled=true,bHidden=true,bTransparent=true,bCompact=true;
    GConfig->GetBool(Section,TEXT("bCompactStartupAllocations"),bEnabled,GGameIni);
    GConfig->GetBool(Section,TEXT("bHideGameWindow"),bHidden,GGameIni);
    GConfig->GetBool(Section,TEXT("bTransparentWindowEnabled"),bTransparent,GGameIni);
    GConfig->GetBool(Section,TEXT("bCompactMemory"),bCompact,GGameIni);
    if(!bEnabled||!bHidden||!bTransparent||!bCompact)return;
    int32 HeapMB=16,TextureMB=16;
    GConfig->GetInt(Section,TEXT("TransientHeapChunkMB"),HeapMB,GGameIni);
    GConfig->GetInt(Section,TEXT("TexturePoolChunkMB"),TextureMB,GGameIni);
    Values[0].Value=FMath::RoundUpToPowerOfTwo(FMath::Clamp(HeapMB,16,128));
    Values[1].Value=FMath::RoundUpToPowerOfTwo(FMath::Clamp(TextureMB,16,64))*1024*1024;
    ModulesHandle=FModuleManager::Get().OnModulesChanged().AddLambda([](FName,EModuleChangeReason Reason)
    {
        if(Reason==EModuleChangeReason::ModuleLoaded)ApplyAvailable();
    });
    ApplyAvailable();
#endif
}

// 分配器实例终生保留初始化时的粒度；这里只撤销我们自己的 CVar 层，不改写用户配置文件。
void FDesktopPetStartupMemory::Shutdown()
{
    if(ModulesHandle.IsValid())FModuleManager::Get().OnModulesChanged().Remove(ModulesHandle);
    ModulesHandle.Reset();
    for(auto& V:Values)if(V.bApplied)
    {
        if(auto* C=IConsoleManager::Get().FindConsoleVariable(V.Name))C->Unset(Priority,StartupTag);
        V.bApplied=false;
    }
}
