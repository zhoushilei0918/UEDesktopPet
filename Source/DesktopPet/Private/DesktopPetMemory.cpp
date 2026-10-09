#include "DesktopPetMemory.h"
#include "DesktopPetActor.h"
#include "HAL/IConsoleManager.h"
#include "DesktopPetRenderPolicy.h"

namespace
{
    // UE 的缓存 CVar 为全局，不能把它们伪装成彼此独立的 Actor 状态。
    TMap<const ADesktopPetActor*,FDesktopPetConfig> Requests;
    bool bBudgetActive=false;
    const FName BudgetTag(TEXT("DesktopPet.MemoryBudget"));
    constexpr EConsoleVariableFlags BudgetPriority=ECVF_SetByPluginHighPriority;
    struct FCapacity
    {
        const TCHAR* Name;
        int32 FDesktopPetConfig::* Member;
        int32 Original=0;
        int32 Applied=INDEX_NONE;
    };
    FCapacity Capacities[]={
        {TEXT("r.Shadow.Virtual.MaxPhysicalPages"),&FDesktopPetConfig::ShadowPageCapacity},
        {TEXT("r.LumenScene.SurfaceCache.AtlasSize"),&FDesktopPetConfig::SurfaceCacheCapacity},
        {TEXT("r.Lumen.ScreenProbeGather.RadianceCache.ProbeAtlasResolutionInProbes"),&FDesktopPetConfig::RadianceProbeCapacity},
        {TEXT("r.RenderTargetPoolMin"),&FDesktopPetConfig::IdleRenderTargetPoolMB}
    };
    // 插件使用独立标签层；撤销时显露原有设置，保留期间用户新写入的控制台/命令行值。
    void Restore()
    {
        if(!bBudgetActive)return;
        for(auto& C:Capacities)
        {
            if(auto* Var=IConsoleManager::Get().FindConsoleVariable(C.Name))Var->Unset(BudgetPriority,BudgetTag);
            C.Applied=INDEX_NONE;
        }
        bBudgetActive=false;
    }
    void Reconcile()
    {
        bool bAllowed=!GIsEditor&&!Requests.IsEmpty();
        for(const auto& R:Requests)
            bAllowed &= R.Value.bCompactMemory&&R.Value.bTransparentWindowEnabled&&R.Value.bHideGameWindow;
        if(!bAllowed){Restore();return;}
        bool Lumen=false;
        for(const auto& R:Requests)Lumen|=FDesktopPetRenderPolicy::Resolve(R.Key->Capture).UsesLumen();
        const auto* VSM=IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.Enable"));
        for(auto& C:Capacities)
        {
            auto* Var=IConsoleManager::Get().FindConsoleVariable(C.Name);
            if(!Var)continue; // 没有编译 Lumen/VSM 的引擎也可正常使用透明显示。
            // 未使用的渲染功能不设置其预算；运行时切换到传统路径时撤销对应标签。
            const bool Enabled=(&C==&Capacities[0])?(VSM&&VSM->GetInt()!=0):((&C==&Capacities[3])||Lumen);
            if(!Enabled)
            {
                if(C.Applied!=INDEX_NONE)Var->Unset(BudgetPriority,BudgetTag);
                C.Applied=INDEX_NONE;continue;
            }
            if(C.Applied==INDEX_NONE)C.Original=Var->GetInt();
            int32 Requested=0;
            for(const auto& R:Requests)Requested=FMath::Max(Requested,R.Value.*(C.Member));
            // 容量只向下限制，不抬高项目本来更低的预算；控制台高优先级始终胜出。
            const int32 Value=FMath::Min(C.Original,Requested);
            if(Value!=C.Applied){Var->Set(Value,BudgetPriority,BudgetTag);C.Applied=Value;}
        }
        bBudgetActive=true;
    }
}

void FDesktopPetMemory::Update(const ADesktopPetActor* Owner,const FDesktopPetConfig& Config)
{
    check(IsInGameThread());Requests.Add(Owner,Config);Reconcile();
}
void FDesktopPetMemory::Release(const ADesktopPetActor* Owner)
{
    check(IsInGameThread());Requests.Remove(Owner);Reconcile();
}
bool FDesktopPetMemory::IsActive(const ADesktopPetActor* Owner) {return bBudgetActive&&Requests.Contains(Owner);}
void FDesktopPetMemory::Shutdown() {Requests.Reset();Restore();}
