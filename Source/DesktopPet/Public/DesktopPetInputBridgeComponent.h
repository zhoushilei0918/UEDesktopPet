#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DesktopPetTypes.h"
#include "DesktopPetInputBridgeComponent.generated.h"
class ADesktopPetActor;
class UInputAction;
class APlayerController;

/** 可选增强输入桥：把透明窗口输入直接注入本地玩家的 Input Action，无需 UE 主窗口获得焦点。 */
UCLASS(ClassGroup=(Input),meta=(BlueprintSpawnableComponent))
class DESKTOPPET_API UDesktopPetInputBridgeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDesktopPetInputBridgeComponent();
    /** 透明显示宿主；为空时尝试使用此组件所在的 Actor。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<ADesktopPetActor> Host;
    /** 接收增强输入的本地玩家控制器；为空时使用世界中的第一个本地控制器。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<APlayerController> PlayerController;
    /** 左键保持动作，推荐 Bool 类型；可将 Started/Completed 接到拖拽接口。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<UInputAction> PrimaryAction;
    /** 右键保持动作，推荐 Bool 类型。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<UInputAction> SecondaryAction;
    /** 逻辑画布中的鼠标位置，推荐 Axis2D 类型。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<UInputAction> PointerPositionAction;
    /** 每帧逻辑画布鼠标位移，推荐 Axis2D 类型。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<UInputAction> PointerDeltaAction;
    /** 滚轮增量，推荐 Axis1D 类型。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") TObjectPtr<UInputAction> WheelAction;
    /** UI 已消费的按键不注入游戏动作，避免点按钮时触发人物拖拽。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Input") bool bIgnoreUIInput=true;
    /** 运行时重新绑定宿主与控制器；会先解除旧宿主的事件。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Input") void InitializeBridge(ADesktopPetActor* InHost,APlayerController* InController);
    virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** 绑定透明窗口事件，缓存输入给下一次 Enhanced Input 处理。 */
    UFUNCTION() void HandlePointer(const FDesktopPetPointerEvent& Event);
    bool bPrimary=false,bSecondary=false,bHadPrimary=false,bHadSecondary=false;
    /** 仅有效命中或正在保持的拖拽允许位置动作，透明空白不会产生游戏输入。 */
    bool bPointerRelevant=false;
    FVector2D Position=FVector2D::ZeroVector,DeltaPosition=FVector2D::ZeroVector;
    float Wheel=0;
};
