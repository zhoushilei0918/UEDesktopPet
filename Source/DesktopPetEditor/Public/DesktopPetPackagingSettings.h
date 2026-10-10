#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DesktopPetPackagingSettings.generated.h"

/** 只在编辑器处理已经打包的 EXE 版本资源；桌宠运行时不依赖此工具。 */
UCLASS(Config=Editor,DefaultConfig,meta=(DisplayName="打包程序名称"))
class DESKTOPPETEDITOR_API UDesktopPetPackagingSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UDesktopPetPackagingSettings();
    /** 留空沿用 UE 默认打包命名；填写后可应用为任务管理器“进程”页名称。支持中文；不改 EXE 文件名或工程名。 */
    UPROPERTY(Config,EditAnywhere,Category="发布",meta=(DisplayName="自定义程序显示名称（留空使用 UE 默认）"))
    FString PetApplicationName;
    /** 选择打包目录最外层的启动 EXE，可同时处理启动器和它指向的游戏 EXE。 */
    UPROPERTY(Config,EditAnywhere,Category="发布",meta=(DisplayName="已打包入口 EXE",FilePathFilter="exe"))
    FFilePath PetPackagedExecutable;
    /** 关闭已运行的打包程序后应用；重新打包会覆盖版本资源，需重新应用。已签名文件不处理。 */
    void PetApplyPackagedName();
    /** 蓝图/工具调用入口；返回完整结果，同一次处理涵盖启动器和游戏程序。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet Utility|Editor")
    bool PetApplyNameToExecutable(const FString& ExecutablePath,const FString& DisplayName,FString& Result);
};
