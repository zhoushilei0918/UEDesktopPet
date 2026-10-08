#pragma once
#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DesktopPetTypes.h"
#include "DesktopPetSettings.generated.h"

/** ini 只提供新实例的默认值；运行时更改保存在 Actor 自己的配置中，不修改全局 CDO。 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Desktop Pet"))
class DESKTOPPET_API UDesktopPetSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    /** 在项目设置的 Plugins 分类中注册。 */
    UDesktopPetSettings() { CategoryName=TEXT("Plugins"); }
    /** 窗口的逻辑画布尺寸；最终物理尺寸还要乘以 DisplayScale。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    FIntPoint WindowSize = FIntPoint(600,760);
    /** 窗口左上角在虚拟桌面中的物理像素坐标，允许负坐标。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    FIntPoint WindowPosition = FIntPoint(120,100);
    /** 整个显示面的缩放倍率，人物与 UI 同步缩放；不是修改角色世界缩放。 */
    UPROPERTY(Config, EditAnywhere, Category="Window", meta=(ClampMin="0.25",ClampMax="3"))
    float DisplayScale = 1.f;
    /** 是否把桌宠原生窗口置顶。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    bool bAlwaysOnTop = true;
    /** 隐藏、禁用 UE 游戏窗口并从任务栏/Alt+Tab 中排除；不影响编辑器主窗口。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    bool bHideGameWindow = true;
    /** 是否允许 BeginWindowDrag 启动拖拽。 */
    UPROPERTY(Config, EditAnywhere, Category="Input")
    bool bDraggable = true;
    /** 人物区域左键是否自动开始拖拽；接增强输入时可关闭并自行调用接口。 */
    UPROPERTY(Config, EditAnywhere, Category="Input")
    bool bAutoDragOnPrimaryButton = true;
    /** 用户关闭桌宠时是否退出游戏进程；编辑器 PIE 不退出编辑器。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    bool bExitApplicationOnClose = true;
    /** 每个方向的超采样倍率；2 表示四个采样，保留平滑的边缘 Alpha。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="1",ClampMax="4"))
    int32 SupersampleScale = 2;
    /** 透明帧的输出频率上限；不直接修改全局游戏帧率。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="5",ClampMax="60"))
    int32 TargetFrameRate = 30;
    /** 捕获颜色的曝光倍数，修改后下一帧生效。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="0.01",ClampMax="16"))
    float Exposure = 1.f;
    /** 启用按命中阈值和 UI 命中判定的额外穿透；零 Alpha 像素始终由 Windows 穿透。 */
    UPROPERTY(Config, EditAnywhere, Category="Input")
    bool bClickThroughTransparentPixels = true;
    /** 接收场景鼠标事件所需的 Alpha，范围 1–255；不会裁剪视觉透明度。 */
    UPROPERTY(Config, EditAnywhere, Category="Input", meta=(ClampMin="1",ClampMax="255"))
    int32 HitAlphaThreshold = 8;
    /** 悬停离开后的交互保持时间；只产生通用进度事件，不创建菜单。 */
    UPROPERTY(Config, EditAnywhere, Category="Interaction", meta=(ClampMin="0",ClampMax="10"))
    float MenuCloseDelay = .4f;
    /** 通用交互展开进度的过渡时间，由项目自行驱动 UI。 */
    UPROPERTY(Config, EditAnywhere, Category="Interaction", meta=(ClampMin="0.01",ClampMax="5"))
    float MenuAnimationSeconds = .18f;
    /** 是否定期输出运行诊断与透明 PNG；正常运行建议关闭。 */
    UPROPERTY(Config, EditAnywhere, Category="Diagnostics")
    bool bWriteDiagnostics = false;
    /** 是否捕获普通半透明材质与 Niagara 的半透明粒子。 */
    UPROPERTY(Config, EditAnywhere, Category="Effects")
    bool bCaptureTranslucency = true;
    /** 对有颜色但无 Alpha 的 Additive 像素重建覆盖率，避免发光粒子被丢弃。 */
    UPROPERTY(Config, EditAnywhere, Category="Effects")
    bool bPreserveAdditiveEffects = true;
    /** Additive 覆盖率重建强度；Windows 使用 source-over，不能完全复现对任意桌面的加法混合。 */
    UPROPERTY(Config, EditAnywhere, Category="Effects", meta=(ClampMin="0.01",ClampMax="4"))
    float AdditiveAlphaStrength = 1.f;
    /** 可选的项目 Widget 类；为空时允许纯人物/纯特效运行，不创建任何默认菜单。 */
    UPROPERTY(Config, EditAnywhere, Category="UI")
    TSoftClassPtr<class UUserWidget> WidgetClass;
    /** 复制默认配置；返回的结构可以在蓝图中自由修改。 */
    FDesktopPetConfig MakeRuntimeConfig() const;
};
