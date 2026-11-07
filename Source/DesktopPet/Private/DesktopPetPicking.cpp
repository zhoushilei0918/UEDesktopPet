#include "DesktopPetPicking.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/ShapeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"

namespace DesktopPetPicking
{
    bool ResolveChannel(FName Name, ECollisionChannel& OutChannel)
    {
        OutChannel = ECC_MAX;
        if (Name.IsNone()) return false;
        const UCollisionProfile* Profile = UCollisionProfile::Get();
        // TraceTypeQuery indices are not collision channel numbers. Resolve the configured name.
        for (int32 Index = 0; Index < int32(TraceTypeQuery_MAX); ++Index)
        {
            const ECollisionChannel Channel = Profile->ConvertToCollisionChannel(true, Index);
            if (Channel != ECC_MAX && Profile->ReturnChannelNameFromContainerIndex(Channel) == Name)
            {
                OutChannel = Channel;
                return true;
            }
        }
        return false;
    }

    TArray<AActor*> GetVisibleActors(const USceneCaptureComponent2D* Capture)
    {
        TArray<AActor*> Actors;
        if (!IsValid(Capture)) return Actors;
        for (const auto& Weak : Capture->ShowOnlyComponents)
        {
            UPrimitiveComponent* C = Weak.Get();
            if (IsValid(C) && C->IsRegistered() && C->IsVisible() && !C->bHiddenInGame &&
                !C->bHiddenInSceneCapture && !Cast<UShapeComponent>(C))
            {
                AActor* A = C->GetOwner();
                if (IsValid(A) && !A->IsHidden()) Actors.AddUnique(A);
            }
        }
        return Actors;
    }

    void GatherComponents(const USceneCaptureComponent2D* Capture, ECollisionChannel Channel,
                          TArray<UPrimitiveComponent*>& OutComponents)
    {
        OutComponents.Reset();
        if (Channel == ECC_MAX) return;
        for (AActor* Actor : GetVisibleActors(Capture))
        {
            TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
            for (UPrimitiveComponent* C : Components)
            {
                if (!IsValid(C) || !C->IsRegistered() || !C->IsQueryCollisionEnabled() ||
                    C->GetCollisionResponseToChannel(Channel) != ECR_Block)
                {
                    continue;
                }
                // Query shapes are intentionally hidden and need not be in the render list.
                // A mesh must actually be rendered; hiding a mesh must also remove its event hit.
                if (!Cast<UShapeComponent>(C) &&
                    (!C->IsVisible() || C->bHiddenInGame || C->bHiddenInSceneCapture ||
                     !Capture->ShowOnlyComponents.Contains(C)))
                {
                    continue;
                }
                OutComponents.Add(C);
            }
        }
    }

    AActor* TraceComponents(const TArray<UPrimitiveComponent*>& Components, ECollisionChannel Channel,
                           const FVector& Start, const FVector& End)
    {
        if (Channel == ECC_MAX || Start.ContainsNaN() || End.ContainsNaN()) return nullptr;
        AActor* Result = nullptr;
        double Nearest = TNumericLimits<double>::Max();
        FCollisionQueryParams Params(SCENE_QUERY_STAT(DesktopPetInteraction), false);
        for (UPrimitiveComponent* C : Components)
        {
            if (!IsValid(C) || !C->IsRegistered() || !C->IsQueryCollisionEnabled() ||
                C->GetCollisionResponseToChannel(Channel) != ECR_Block) continue;
            // Static props use their complex collision; capsules and skeletal physics bodies
            // use simple collision. A precise miss must never fall back to a bounding box.
            Params.bTraceComplex = C->IsA<UStaticMeshComponent>();
            FHitResult Hit;
            if (C->LineTraceComponent(Hit, Start, End, Params) && Hit.Distance < Nearest)
            {
                Nearest = Hit.Distance;
                Result = C->GetOwner();
            }
        }
        return Result;
    }
}
