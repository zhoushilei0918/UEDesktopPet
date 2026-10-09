#include "DesktopPetActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/TextureRenderTarget2D.h"

// 可见名单就是默认交互名单；不要求角色创建 Physics Asset 或配置碰撞通道。
TArray<AActor*> ADesktopPetActor::PetGetInteractionActors() const
{
    TArray<AActor*> Result;
    if(!Capture)return Result;
    for(const auto& Weak:Capture->ShowOnlyComponents)
    {
        UPrimitiveComponent* C=Weak.Get();
        if(IsValid(C)&&C->IsRegistered()&&C->IsVisible()&&!C->bHiddenInGame&&!C->bHiddenInSceneCapture&&!Cast<UShapeComponent>(C))
            if(AActor* A=C->GetOwner();IsValid(A)&&!A->IsHidden())Result.AddUnique(A);
    }
    return Result;
}
AActor* ADesktopPetActor::PetGetHoveredActor() const{return HoveredInteractionActor.Get();}

// 先由最终透明覆盖率拒绝空白，再在可见组件包围盒中寻找最近对象。
// 多 Actor 重叠时这是自动包围盒拾取，并非逐三角形 ID 缓冲；不会修改组件碰撞。
AActor* ADesktopPetActor::TraceInteractionActor(FVector2D UV) const
{
    const TArray<AActor*> Actors=PetGetInteractionActors();
    if(Actors.IsEmpty()||!GetWorld())return nullptr;
    if(Actors.Num()==1)return Actors[0];
    if(!Capture->bUseCustomProjectionMatrix&&Capture->TextureTarget)
    {
        const FIntPoint Canvas=PetGetRuntimeConfig().WindowSize;
        const double RenderAspect=double(Capture->TextureTarget->SizeX)/Capture->TextureTarget->SizeY;
        UV.Y=.5+(UV.Y-.5)*RenderAspect/(double(Canvas.X)/Canvas.Y);
    }
    FVector Start,Direction;
    if(!UGameplayStatics::DeprojectSceneCaptureComponentToWorld(Capture,UV,Start,Direction))return nullptr;
    const FVector End=Start+Direction*10000000.f;
    double Nearest=TNumericLimits<double>::Max();AActor* Hit=nullptr;
    for(const auto& Weak:Capture->ShowOnlyComponents)
    {
        UPrimitiveComponent* C=Weak.Get();
        if(!IsValid(C)||!Actors.Contains(C->GetOwner())||!C->IsVisible()||C->bHiddenInGame||C->bHiddenInSceneCapture||Cast<UShapeComponent>(C))continue;
        FVector Location,Normal;float Time=0;
        if(FMath::LineExtentBoxIntersection(C->Bounds.GetBox(),Start,End,FVector::ZeroVector,Location,Normal,Time)&&Time<Nearest)
        {Nearest=Time;Hit=C->GetOwner();}
    }
    return Hit;
}

// 先清理状态再通知项目，允许蓝图在回调中销毁对象或关闭桌宠。
void ADesktopPetActor::UpdateHoveredActor(AActor* NewActor)
{
    AActor* Previous=HoveredInteractionActor.Get();
    if(Previous==NewActor)return;
    HoveredInteractionActor.Reset();
    if(Previous)
    {
        Previous->OnDestroyed.RemoveDynamic(this,&ADesktopPetActor::HandleHoveredActorDestroyed);
        PetActorMouseLeave.Broadcast(Previous);PetEventActorMouseLeave(Previous);
    }
    if(!HoveredInteractionActor.IsValid()&&!bStopping&&!bConfigWidgetOpen&&IsValid(NewActor)&&PetGetInteractionActors().Contains(NewActor))
    {
        HoveredInteractionActor=NewActor;
        NewActor->OnDestroyed.AddUniqueDynamic(this,&ADesktopPetActor::HandleHoveredActorDestroyed);
        PetActorMouseEnter.Broadcast(NewActor);PetEventActorMouseEnter(NewActor);
    }
    if((Previous!=nullptr)!=HoveredInteractionActor.IsValid())PetHoverChanged.Broadcast(HoveredInteractionActor.IsValid());
}
void ADesktopPetActor::HandleHoveredActorDestroyed(AActor* Actor)
{
    HoveredInteractionActor.Reset();PetActorMouseLeave.Broadcast(Actor);PetEventActorMouseLeave(Actor);PetHoverChanged.Broadcast(false);
}
