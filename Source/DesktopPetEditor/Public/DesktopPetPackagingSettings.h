#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DesktopPetPackagingSettings.generated.h"

/** 正常 Win64 打包时自动设置启动器与桌宠名称，只需填写一个名称。 */
UCLASS(Config=Editor,DefaultConfig,meta=(DisplayName="打包程序名称"))
class DESKTOPPETEDITOR_API UDesktopPetPackagingSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UDesktopPetPackagingSettings();
    /** 例如 WocaoKe：入口文件为 WocaoKe.exe，启动器显示为 WocaoKeGame，桌宠为 WocaoKe。输入有效的 Windows 文件名，不含 .exe 扩展名。留空使用 UE 项目名称，未设置项目名称时使用工程名。重新打包自动生效。 */
    UPROPERTY(Config,EditAnywhere,Category="发布",meta=(DisplayName="程序名称（留空使用 UE 默认）"))
    FString PetApplicationName;
    /** 模块自动登记 UAT 扩展；不暴露按钮、路径或蓝图操作。 */
    static void EnsurePackagingAutomation();
};
