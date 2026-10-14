#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DesktopPetMenuInterface.generated.h"

/** 可选的菜单关闭协议。只处理生命周期，不依赖项目、角色、控件布局或动画名称。 */
UINTERFACE(BlueprintType, Blueprintable)
class DESKTOPPET_API UDesktopPetMenuInterface : public UInterface
{
    GENERATED_BODY()
};

class DESKTOPPET_API IDesktopPetMenuInterface
{
    GENERATED_BODY()
public:
    /**
     * 菜单即将关闭时调用。返回 true 表示 UMG 接管收尾，插件暂时保留显示和命中。
     * UMG 播完收回动画后调用所属桌宠的 PetCompleteConfigWidgetClose。
     * 返回 false 或未实现本接口时沿用立即关闭；停止、收进托盘和替换菜单类会立即清理。
     */
    UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "DesktopPet|UI")
    bool PetRequestMenuClose();
};
