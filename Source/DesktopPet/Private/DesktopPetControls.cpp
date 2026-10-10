#include "DesktopPetActor.h"
#include "DesktopPetRuntime.h"
#include "Camera/CameraActor.h"
#include "Components/ShapeComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"

// 菜单与常驻 Overlay 分开，关闭菜单不会丢失项目已有 UI。
void ADesktopPetActor::PetSetConfigWidgetClass(TSubclassOf<UUserWidget> Class)
{
    PetCloseConfigWidget();ConfigWidget=nullptr;ConfigWidgetClass=Class;
}
bool ADesktopPetActor::PetOpenConfigWidget()
{
    if(!Runtime||bStopping||Runtime->Shell.IsInTray()||!ConfigWidgetClass)return false;
    if(bConfigWidgetOpen)return true;
    if(!ConfigWidget)ConfigWidget=CreateWidget<UUserWidget>(GetWorld(),ConfigWidgetClass);
    if(!ConfigWidget)return false;
    Runtime->ReleaseHeldInput();
    UpdateHoveredActor(nullptr);
    if(!Runtime||bStopping)return false;
    bConfigWidgetOpen=true;bWidgetPending=true;
    PetConfigWidgetVisibilityChanged.Broadcast(true);
    return bConfigWidgetOpen;
}
void ADesktopPetActor::PetCloseConfigWidget()
{
    if(!bConfigWidgetOpen)return;
    bConfigWidgetOpen=false;bWidgetPending=true;
    if(Runtime)Runtime->ReleaseHeldInput();
    PetConfigWidgetVisibilityChanged.Broadcast(false);
}

// 所有快捷节点汇总到同一配置应用入口，便于蓝图做设置面板和 SaveGame。
void ADesktopPetActor::PetSetTransparentWindowEnabled(bool Value){auto C=PetGetRuntimeConfig();C.bTransparentWindowEnabled=Value;PetApplyRuntimeConfig(C);}
void ADesktopPetActor::PetSetWheelZoomEnabled(bool Value){auto C=PetGetRuntimeConfig();C.bEnableWheelZoom=Value;PetApplyRuntimeConfig(C);}
void ADesktopPetActor::PetSetSharpness(float Value){auto C=PetGetRuntimeConfig();C.Sharpness=Value;PetApplyRuntimeConfig(C);}
void ADesktopPetActor::PetSetCenterAnchoredScaling(bool Value){auto C=PetGetRuntimeConfig();C.bCenterAnchoredScaling=Value;PetApplyRuntimeConfig(C);}
void ADesktopPetActor::PetSetWheelZoomStep(float Value){auto C=PetGetRuntimeConfig();C.WheelZoomStep=Value;PetApplyRuntimeConfig(C);}

// 不创建 SpringArm 碰撞：环绕只改变捕获相机，不改变角色或原项目相机。
FVector ADesktopPetActor::PetGetOrbitCenter() const
{
    return IsValid(OrbitFocusActor)?OrbitFocusActor->GetActorLocation()+OrbitCenterOffset:Capture->GetComponentLocation();
}
void ADesktopPetActor::PetSetOrbitFocusActor(AActor* Actor,bool KeepView)
{
    OrbitFocusActor=IsValid(Actor)?Actor:nullptr;
    if(OrbitFocusActor&&KeepView)
    {
        const FVector ToCenter=PetGetOrbitCenter()-Capture->GetComponentLocation();
        OrbitDistance=FMath::Max(10.f,float(ToCenter.Size()));OrbitRotation=ToCenter.Rotation();
    }
    UpdateOrbitCamera();
}
void ADesktopPetActor::PetSetOrbitSettings(FVector Offset,float Distance,float Sensitivity)
{
    if(!Offset.ContainsNaN())OrbitCenterOffset=Offset;
    OrbitDistance=FMath::Clamp(FMath::IsFinite(Distance)?Distance:250.f,10.f,1000000.f);
    OrbitSensitivity=FMath::Clamp(FMath::IsFinite(Sensitivity)?Sensitivity:0.2f,0.01f,5.f);
    UpdateOrbitCamera();
}
void ADesktopPetActor::PetSetOrbitRotation(FRotator Rotation)
{
    if(Rotation.ContainsNaN())return;
    OrbitRotation=FRotator(FMath::Clamp(Rotation.Pitch,-85.,85.),FMath::UnwindDegrees(Rotation.Yaw),0);
    UpdateOrbitCamera();
}
void ADesktopPetActor::UpdateOrbitCamera()
{
    if(!IsValid(OrbitFocusActor))return;
    OrbitRotation.Pitch=FMath::Clamp(OrbitRotation.Pitch,-85.,85.);OrbitRotation.Roll=0;
    OrbitDistance=FMath::Clamp(OrbitDistance,10.f,1000000.f);
    Capture->SetWorldLocationAndRotation(PetGetOrbitCenter()-OrbitRotation.Vector()*OrbitDistance,OrbitRotation);
}
bool ADesktopPetActor::PetFrameActor(AActor* Actor,float Margin)
{
    if(!IsValid(Actor))return false;
    FBox Bounds(ForceInit);
    TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
    for(UPrimitiveComponent* C:Components)if(C->IsRegistered()&&C->IsVisible()&&!C->bHiddenInGame&&!C->bHiddenInSceneCapture&&!C->IsA<UShapeComponent>())Bounds+=C->Bounds.GetBox();
    if(!Bounds.IsValid)return false;
    OrbitCenterOffset=Bounds.GetCenter()-Actor->GetActorLocation();OrbitFocusActor=Actor;
    const FIntPoint Size=PetGetDisplaySize();
    const double HalfH=FMath::DegreesToRadians(FMath::Clamp(Capture->FOVAngle,5.f,160.f)*0.5f);
    const double HalfV=FMath::Atan(FMath::Tan(HalfH)*Size.Y/FMath::Max(1,Size.X));
    // 按当前观察方向投影包围盒，充分利用画面；后续大幅转动时可再次 PetFrameActor。
    const FRotationMatrix Basis(OrbitRotation);
    const FVector Forward=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
    double Distance=10.;
    for(int32 I=0;I<8;++I)
    {
        const FVector Corner((I&1)?Bounds.Max.X:Bounds.Min.X,(I&2)?Bounds.Max.Y:Bounds.Min.Y,(I&4)?Bounds.Max.Z:Bounds.Min.Z);
        const FVector Delta=Corner-Bounds.GetCenter();
        Distance=FMath::Max(Distance,FMath::Max(FMath::Abs(Delta.Dot(Right))/FMath::Tan(HalfH),FMath::Abs(Delta.Dot(Up))/FMath::Tan(HalfV))-Delta.Dot(Forward));
    }
    OrbitDistance=Distance*FMath::Clamp(Margin,1.f,3.f);
    UpdateOrbitCamera();return true;
}
void ADesktopPetActor::PetResetView()
{
    OrbitRotation=InitialOrbitRotation;OrbitDistance=InitialOrbitDistance;
    Capture->SetWorldTransform(InitialCaptureTransform);UpdateOrbitCamera();
}

// 普通窗口调试沿用引擎自身游戏视口；相机临时同步到桌宠捕获，停止时恢复 ViewTarget。
void ADesktopPetActor::UpdateDebugCamera()
{
    if(PetGetRuntimeConfig().bTransparentWindowEnabled){RestoreDebugCamera();return;}
    APlayerController* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr;
    if(!PC)return;
    if(!DebugCamera)
    {
        SavedViewTarget=PC->GetViewTarget();
        FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;Params.Owner=this;
        DebugCamera=GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(),Capture->GetComponentTransform(),Params);
        if(DebugCamera)PC->SetViewTarget(DebugCamera);
    }
    if(DebugCamera)
    {
        DebugCamera->SetActorTransform(Capture->GetComponentTransform());
        auto* Camera=DebugCamera->GetCameraComponent();
        Camera->SetFieldOfView(Capture->FOVAngle);Camera->SetProjectionMode(Capture->ProjectionType);
        Camera->SetOrthoWidth(Capture->OrthoWidth);Camera->SetConstraintAspectRatio(false);
        Camera->PostProcessSettings=Capture->PostProcessSettings;Camera->PostProcessBlendWeight=Capture->PostProcessBlendWeight;
    }
}
void ADesktopPetActor::RestoreDebugCamera()
{
    if(!DebugCamera)return;
    if(APlayerController* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)
        if(PC->GetViewTarget()==DebugCamera&&SavedViewTarget.IsValid())PC->SetViewTarget(SavedViewTarget.Get());
    DebugCamera->Destroy();DebugCamera=nullptr;SavedViewTarget.Reset();
}
