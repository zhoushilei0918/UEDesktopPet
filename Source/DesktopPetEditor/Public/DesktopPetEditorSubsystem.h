#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "DesktopPetEditorSubsystem.generated.h"

/** 编辑器工程接入工具；不引用任何项目角色、UI 或地图资产。 */
UCLASS()
class DESKTOPPETEDITOR_API UDesktopPetEditorSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()
public:
    /** 补齐配置，以当前已保存的地图作为游戏入口；选中模型可同时创建或补充宿主白名单。 */
    UFUNCTION(BlueprintCallable, Category="DesktopPet Utility|Editor")
    bool PetConfigureProject();

    /** PIE、模拟或未打开关卡时禁止修改配置。 */
    UFUNCTION(BlueprintPure, Category="DesktopPet Utility|Editor")
    bool PetCanConfigureProject() const;

    /** 最近一次配置的修改结果与待办；失败时也会给出具体原因。 */
    UFUNCTION(BlueprintPure, Category="DesktopPet Utility|Editor")
    FString PetGetLastSetupReport() const { return LastSetupReport; }

    /** 最近一次写入磁盘的报告路径；同目录保存修改前的 ini。 */
    UFUNCTION(BlueprintPure, Category="DesktopPet Utility|Editor")
    FString PetGetLastSetupReportPath() const { return LastSetupReportPath; }

private:
    // 这些编辑器状态不参与项目配置、地图序列化或游戏打包。
    FString LastSetupReport;
    FString LastSetupReportPath;
    bool FinishSetup(bool bSuccess, const FString& Report);
};
