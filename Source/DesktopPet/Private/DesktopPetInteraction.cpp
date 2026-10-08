#include "DesktopPetActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "CollisionQueryParams.h"

// 名单变化只影响输入，不隐式改变项目管理的显示内容。
void ADesktopPetActor::SetInteractionActors(const TArray<AActor*>& Actors)
{
    InteractionActors.Reset();
    for(AActor* Actor:Actors)if(IsValid(Actor))InteractionActors.AddUnique(Actor);
    if(!InteractionActors.Contains(GetHoveredActor()))UpdateHoveredActor(nullptr);
}

// 去重后登记，Actor 可以由项目在运行时生成。
void ADesktopPetActor::AddInteractionActor(AActor* Actor)
{
    if(IsValid(Actor))InteractionActors.AddUnique(Actor);
}

// 正在悬停的对象被移除时不必等下一帧才发出离开事件。
void ADesktopPetActor::RemoveInteractionActor(AActor* Actor)
{
    InteractionActors.Remove(Actor);
    if(GetHoveredActor()==Actor)UpdateHoveredActor(nullptr);
}

// 过滤被销毁的弱生命周期对象，蓝图不需要处理失效条目。
TArray<AActor*> ADesktopPetActor::GetInteractionActors() const
{
    TArray<AActor*> Result;
    for(AActor* Actor:InteractionActors)if(IsValid(Actor))Result.AddUnique(Actor);
    return Result;
}

// 返回当前有效对象，不让“仅仅显示”的 Actor 冒充可交互对象。
AActor* ADesktopPetActor::GetHoveredActor() const
{
    return HoveredInteractionActor.Get();
}

// 先对所有已捕获的查询组件取最近命中，再判断白名单，防止点击穿过未选中的遮挡物。
// 忽略未被捕获的背景碰撞；不使用 Actor 包围盒替代模型查询，否则两个角色重叠时会误报。
AActor* ADesktopPetActor::TraceInteractionActor(FVector2D UV) const
{
    if(!Capture||InteractionActors.IsEmpty()||!GetWorld())return nullptr;
    FVector Start,Direction;
    if(!UGameplayStatics::DeprojectSceneCaptureComponentToWorld(Capture,UV,Start,Direction))return nullptr;
    const float Distance=FMath::Clamp(FMath::IsFinite(InteractionTraceDistance)?InteractionTraceDistance:100000.f,1.f,10000000.f);
    const FVector End=Start+Direction*Distance;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(DesktopPetInteraction),bTraceComplexForInteraction);
    double Nearest=TNumericLimits<double>::Max();AActor* HitActor=nullptr;
    for(const auto& Weak:Capture->ShowOnlyComponents)
    {
        UPrimitiveComponent* Component=Weak.Get();
        if(!IsValid(Component)||!Component->IsRegistered()||!Component->IsQueryCollisionEnabled())continue;
        AActor* Candidate=Component->GetOwner();
        if(!IsValid(Candidate)||Candidate->IsHidden()||Candidate->GetWorld()!=GetWorld())continue;
        // 隐藏的 ShapeComponent 可作为角色查询代理；隐藏模型或禁止场景捕获的模型不参与。
        if(!Cast<UShapeComponent>(Component)&&(!Component->IsVisible()||Component->bHiddenInGame||Component->bHiddenInSceneCapture))continue;
        if(Component->GetCollisionResponseToChannel(InteractionTraceChannel)!=ECR_Block)continue;
        FHitResult Hit;
        if(Component->LineTraceComponent(Hit,Start,End,Params)&&Hit.Distance<Nearest)
        {
            Nearest=Hit.Distance;HitActor=Candidate;
        }
    }
    return IsValid(HitActor)&&InteractionActors.Contains(HitActor)?HitActor:nullptr;
}

// 状态先清空再通知项目；用户在事件内删除对象、修改名单或停止显示也不会重复 Leave。
void ADesktopPetActor::UpdateHoveredActor(AActor* NewActor)
{
    AActor* Previous=HoveredInteractionActor.Get();
    if(Previous==NewActor)return;
    HoveredInteractionActor.Reset();
    if(Previous)
    {
        Previous->OnDestroyed.RemoveDynamic(this,&ADesktopPetActor::HandleHoveredActorDestroyed);
        OnActorMouseLeave.Broadcast(Previous);
    }
    // Leave 回调可能改变名单或启动另一条状态迁移，不能覆盖回调已经选择的新对象。
    if(!HoveredInteractionActor.IsValid()&&!bStopping&&IsValid(NewActor)&&InteractionActors.Contains(NewActor))
    {
        HoveredInteractionActor=NewActor;
        NewActor->OnDestroyed.AddUniqueDynamic(this,&ADesktopPetActor::HandleHoveredActorDestroyed);
        OnActorMouseEnter.Broadcast(NewActor);
    }
    if((Previous!=nullptr)!=HoveredInteractionActor.IsValid())OnPetHoverChanged.Broadcast(HoveredInteractionActor.IsValid());
}

// OnDestroyed 参数仍能指明离开的对象；广播前清空引用，避免用户回调造成重复事件。
void ADesktopPetActor::HandleHoveredActorDestroyed(AActor* Actor)
{
    HoveredInteractionActor.Reset();
    OnActorMouseLeave.Broadcast(Actor);
    OnPetHoverChanged.Broadcast(false);
}
