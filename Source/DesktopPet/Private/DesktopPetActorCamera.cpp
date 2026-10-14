#include "DesktopPetActor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"

/** 不创建 SpringArm 碰撞：环绕只改变捕获相机，不改变角色或原项目相机。 */
FVector ADesktopPetActor::PetGetOrbitCenter() const
{
    return IsValid(OrbitFocusActor) ? OrbitFocusActor->GetActorLocation() + OrbitCenterOffset :
                                      Capture->GetComponentLocation();
}

void ADesktopPetActor::PetSetOrbitFocusActor(AActor* Actor, bool KeepView)
{
    OrbitFocusActor = IsValid(Actor) ? Actor : nullptr;
    if (OrbitFocusActor && KeepView)
    {
        const FVector ToCenter = PetGetOrbitCenter() - Capture->GetComponentLocation();
        OrbitDistance = FMath::Max(10.f, float(ToCenter.Size()));
        OrbitRotation = ToCenter.Rotation();
    }
    UpdateOrbitCamera();
}

void ADesktopPetActor::PetSetOrbitSettings(FVector Offset, float Distance, float Sensitivity)
{
    if (!Offset.ContainsNaN())
    {
        OrbitCenterOffset = Offset;
    }
    OrbitDistance = FMath::Clamp(FMath::IsFinite(Distance) ? Distance : 250.f, 10.f, 1000000.f);
    OrbitSensitivity = FMath::Clamp(FMath::IsFinite(Sensitivity) ? Sensitivity : 0.2f, 0.01f, 5.f);
    UpdateOrbitCamera();
}

void ADesktopPetActor::PetSetOrbitRotation(FRotator Rotation)
{
    if (Rotation.ContainsNaN())
    {
        return;
    }
    OrbitRotation = FRotator(FMath::Clamp(Rotation.Pitch, -85., 85.), FMath::UnwindDegrees(Rotation.Yaw), 0);
    UpdateOrbitCamera();
}

void ADesktopPetActor::UpdateOrbitCamera()
{
    if (!IsValid(OrbitFocusActor))
    {
        return;
    }
    OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch, -85., 85.);
    OrbitRotation.Roll = 0;
    OrbitDistance = FMath::Clamp(OrbitDistance, 10.f, 1000000.f);
    Capture->SetWorldLocationAndRotation(PetGetOrbitCenter() - OrbitRotation.Vector() * OrbitDistance, OrbitRotation);
}

bool ADesktopPetActor::PetFrameActor(AActor* Actor, float Margin)
{
    if (!IsValid(Actor))
    {
        return false;
    }
    FBox Bounds(ForceInit);
    TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
    for (UPrimitiveComponent* C : Components)
    {
        if (C->IsRegistered() && C->IsVisible() && !C->bHiddenInGame && !C->bHiddenInSceneCapture &&
            !C->IsA<UShapeComponent>())
        {
            Bounds += C->Bounds.GetBox();
        }
    }
    if (!Bounds.IsValid)
    {
        return false;
    }
    OrbitCenterOffset = Bounds.GetCenter() - Actor->GetActorLocation();
    OrbitFocusActor = Actor;
    const FIntPoint Size = PetGetDisplaySize();
    const double HalfH = FMath::DegreesToRadians(FMath::Clamp(Capture->FOVAngle, 5.f, 160.f) * 0.5f);
    const double HalfV = FMath::Atan(FMath::Tan(HalfH) * Size.Y / FMath::Max(1, Size.X));
    /** 按当前观察方向投影包围盒，充分利用画面；后续大幅转动时可再次 PetFrameActor。 */
    const FRotationMatrix Basis(OrbitRotation);
    const FVector Forward = Basis.GetUnitAxis(EAxis::X), Right = Basis.GetUnitAxis(EAxis::Y),
                  Up = Basis.GetUnitAxis(EAxis::Z);
    double Distance = 10.;
    for (int32 I = 0; I < 8; ++I)
    {
        const FVector Corner((I & 1) ? Bounds.Max.X : Bounds.Min.X,
                             (I & 2) ? Bounds.Max.Y : Bounds.Min.Y,
                             (I & 4) ? Bounds.Max.Z : Bounds.Min.Z);
        const FVector Delta = Corner - Bounds.GetCenter();
        Distance = FMath::Max(Distance,
                              FMath::Max(FMath::Abs(Delta.Dot(Right)) / FMath::Tan(HalfH),
                                         FMath::Abs(Delta.Dot(Up)) / FMath::Tan(HalfV)) -
                                  Delta.Dot(Forward));
    }
    OrbitDistance = Distance * FMath::Clamp(Margin, 1.f, 3.f);
    UpdateOrbitCamera();
    return true;
}

void ADesktopPetActor::PetResetView()
{
    OrbitRotation = InitialOrbitRotation;
    OrbitDistance = InitialOrbitDistance;
    Capture->SetWorldTransform(InitialCaptureTransform);
    UpdateOrbitCamera();
}
