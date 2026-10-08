#include "DesktopPetInputBridgeComponent.h"
#include "DesktopPetActor.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

// 早于 Actor 常规 Tick 注入，让玩家控制器本帧能处理上帧收到的原生窗口事件。
UDesktopPetInputBridgeComponent::UDesktopPetInputBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.TickGroup=TG_PrePhysics;
}
void UDesktopPetInputBridgeComponent::BeginPlay()
{
    Super::BeginPlay();
    InitializeBridge(Host?Host.Get():Cast<ADesktopPetActor>(GetOwner()),PlayerController);
}
// 销毁组件时解除动态委托，防止宿主继续向失效组件广播。
void UDesktopPetInputBridgeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Host)Host->OnPointerInput.RemoveDynamic(this,&UDesktopPetInputBridgeComponent::HandlePointer);
    Super::EndPlay(Reason);
}
void UDesktopPetInputBridgeComponent::InitializeBridge(ADesktopPetActor* InHost,APlayerController* InController)
{
    if(Host)Host->OnPointerInput.RemoveDynamic(this,&UDesktopPetInputBridgeComponent::HandlePointer);
    Host=InHost;PlayerController=InController;
    if(Host)Host->OnPointerInput.AddDynamic(this,&UDesktopPetInputBridgeComponent::HandlePointer);
    bPrimary=bSecondary=bHadPrimary=bHadSecondary=false;
    bPointerRelevant=false;DeltaPosition=FVector2D::ZeroVector;Wheel=0;
}
// 抬起总会清理保持状态，即使鼠标已移动到 UI 上，避免动作卡在按下状态。
void UDesktopPetInputBridgeComponent::HandlePointer(const FDesktopPetPointerEvent& E)
{
    Position=E.CanvasPosition;
    bPointerRelevant=!(bIgnoreUIInput&&E.bOverUI)&&(E.bOverScene||E.bOverUI);
    if(E.Type==EDesktopPetPointerEvent::Release)
    {
        if(E.Key==EKeys::LeftMouseButton)bPrimary=false;
        if(E.Key==EKeys::RightMouseButton)bSecondary=false;
        return;
    }
    if((bIgnoreUIInput&&E.bOverUI)||(!bPointerRelevant&&!bPrimary&&!bSecondary))return;
    if(E.Type==EDesktopPetPointerEvent::Press)
    {
        if(E.Key==EKeys::LeftMouseButton)bPrimary=true;
        if(E.Key==EKeys::RightMouseButton)bSecondary=true;
    }
    if(E.Type==EDesktopPetPointerEvent::Move&&Host)DeltaPosition+=E.Delta/Host->GetRuntimeConfig().DisplayScale;
    if(E.Type==EDesktopPetPointerEvent::Wheel)Wheel+=E.WheelDelta;
}
// 连续按住每帧注入，抬起额外注入一次零值，以便 Completed / Canceled 正常触发。
void UDesktopPetInputBridgeComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta,TickType,Function);
    if(!PlayerController&&GetWorld())PlayerController=GetWorld()->GetFirstPlayerController();
    auto* Input=PlayerController?Cast<UEnhancedPlayerInput>(PlayerController->PlayerInput):nullptr;
    if(!Input)return;
    if(PrimaryAction&&(bPrimary||bHadPrimary))Input->InjectInputForAction(PrimaryAction,FInputActionValue(bPrimary));
    if(SecondaryAction&&(bSecondary||bHadSecondary))Input->InjectInputForAction(SecondaryAction,FInputActionValue(bSecondary));
    const bool Relevant=bPointerRelevant||bPrimary||bSecondary;
    if(PointerPositionAction)Input->InjectInputForAction(PointerPositionAction,FInputActionValue(Relevant?Position:FVector2D::ZeroVector));
    if(PointerDeltaAction)Input->InjectInputForAction(PointerDeltaAction,FInputActionValue(DeltaPosition));
    if(WheelAction)Input->InjectInputForAction(WheelAction,FInputActionValue(Wheel));
    bHadPrimary=bPrimary;bHadSecondary=bSecondary;
    DeltaPosition=FVector2D::ZeroVector;Wheel=0;
}
