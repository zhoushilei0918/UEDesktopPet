#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
class USceneCaptureComponent2D;
class UPrimitiveComponent;
class AActor;

/** Mouse event queries are separate from the legacy visible-pixel window controls. */
namespace DesktopPetPicking
{
    bool ResolveChannel(FName Name, ECollisionChannel& OutChannel);
    TArray<AActor*> GetVisibleActors(const USceneCaptureComponent2D* Capture);
    void GatherComponents(const USceneCaptureComponent2D* Capture, ECollisionChannel Channel,
                          TArray<UPrimitiveComponent*>& OutComponents);
    AActor* TraceComponents(const TArray<UPrimitiveComponent*>& Components, ECollisionChannel Channel,
                           const FVector& Start, const FVector& End);
}
