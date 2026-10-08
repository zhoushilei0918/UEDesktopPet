#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "DesktopPetTypes.generated.h"
class AActor;

/** 一个桌宠实例的完整配置；可在蓝图中 Make / Set Members 后交给 ApplyRuntimeConfig。 */
USTRUCT(BlueprintType)
struct DESKTOPPET_API FDesktopPetConfig
{
    GENERATED_BODY()
    /** 窗口的逻辑画布尺寸；最终物理尺寸还要乘以 DisplayScale。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Window")
    FIntPoint WindowSize = FIntPoint(600,760);
    /** 窗口左上角在虚拟桌面中的物理像素坐标，允许负坐标。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Window")
    FIntPoint WindowPosition = FIntPoint(120,100);
    /** 整个显示面的缩放倍率，人物与 UI 同步缩放；不是修改角色世界缩放。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Window", meta=(ClampMin="0.25",ClampMax="3"))
    float DisplayScale = 1.f;
    /** 是否把桌宠原生窗口置顶。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Window")
    bool bAlwaysOnTop = true;
    /** 隐藏、禁用 UE 游戏窗口并从任务栏/Alt+Tab 中排除；不影响编辑器主窗口。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Window")
    bool bHideGameWindow = true;
    /** 是否允许 BeginWindowDrag 启动拖拽。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
    bool bDraggable = true;
    /** 人物区域左键是否自动开始拖拽；接增强输入时可关闭并自行调用接口。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
    bool bAutoDragOnPrimaryButton = true;
    /** 用户关闭桌宠时是否退出游戏进程；编辑器 PIE 不退出编辑器。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Window")
    bool bExitApplicationOnClose = true;
    /** 每个方向的超采样倍率；2 表示四个采样，保留平滑的边缘 Alpha。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quality", meta=(ClampMin="1",ClampMax="4"))
    int32 SupersampleScale = 2;
    /** 透明帧的输出频率上限；不直接修改全局游戏帧率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quality", meta=(ClampMin="5",ClampMax="60"))
    int32 TargetFrameRate = 30;
    /** 捕获颜色的曝光倍数，修改后下一帧生效。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quality", meta=(ClampMin="0.01",ClampMax="16"))
    float Exposure = 1.f;
    /** 启用按命中阈值和 UI 命中判定的额外穿透；零 Alpha 像素始终由 Windows 穿透。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input")
    bool bClickThroughTransparentPixels = true;
    /** 接收场景鼠标事件所需的 Alpha，范围 1–255；不会裁剪视觉透明度。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Input", meta=(ClampMin="1",ClampMax="255"))
    int32 HitAlphaThreshold = 8;
    /** 悬停离开后的交互保持时间；只产生通用进度事件，不创建菜单。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta=(ClampMin="0",ClampMax="10"))
    float MenuCloseDelay = .4f;
    /** 通用交互展开进度的过渡时间，由项目自行驱动 UI。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction", meta=(ClampMin="0.01",ClampMax="5"))
    float MenuAnimationSeconds = .18f;
    /** 是否定期输出运行诊断与透明 PNG；正常运行建议关闭。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Diagnostics")
    bool bWriteDiagnostics = false;
    /** 是否捕获普通半透明材质与 Niagara 的半透明粒子。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects")
    bool bCaptureTranslucency = true;
    /** 对有颜色但无 Alpha 的 Additive 像素重建覆盖率，避免发光粒子被丢弃。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects")
    bool bPreserveAdditiveEffects = true;
    /** Additive 覆盖率重建强度；Windows 使用 source-over，不能完全复现对任意桌面的加法混合。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effects", meta=(ClampMin="0.01",ClampMax="4"))
    float AdditiveAlphaStrength = 1.f;
    /** 将外部配置约束到合法范围，避免超大纹理或除零。 */
    void Normalize();
    /** 使用引擎最终颜色（含关卡色调映射/调色）；关闭时恢复旧版简化 HDR 合成。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Quality")
    bool bUseEnginePostProcessing = true;
    /** 逻辑尺寸乘显示缩放后的实际物理像素尺寸。 */
    FIntPoint GetDisplaySize() const;
};

/** 与角色、菜单无关的原始指针事件类型。 */
UENUM(BlueprintType,meta=(ScriptName="DesktopPetPointerEventType"))
enum class EDesktopPetPointerEvent : uint8 { Move, Press, Release, Wheel };

/** 传给项目蓝图及自定义输入系统的统一指针数据。 */
USTRUCT(BlueprintType)
struct DESKTOPPET_API FDesktopPetPointerEvent
{
    GENERATED_BODY()
    /** 按下、抬起、移动或滚轮。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") EDesktopPetPointerEvent Type = EDesktopPetPointerEvent::Move;
    /** 鼠标键；移动和滚轮事件可为空键。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") FKey Key;
    /** 原生窗口内的物理像素坐标。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") FVector2D PixelPosition = FVector2D::ZeroVector;
    /** 除以显示缩放后的 UI 逻辑画布坐标。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") FVector2D CanvasPosition = FVector2D::ZeroVector;
    /** 当前相对上一次指针位置的物理像素增量。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") FVector2D Delta = FVector2D::ZeroVector;
    /** 指针命中的交互白名单 Actor；UI、空白或未选择对象上为空。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") TObjectPtr<AActor> HitActor=nullptr;
    /** 鼠标滚轮格数；可以为负值。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") float WheelDelta = 0;
    /** 当前点是否命中交互 UI；项目可以用它决定是否消费输入。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") bool bOverUI = false;
    /** 当前点是否命中场景 Alpha。 */
    UPROPERTY(BlueprintReadOnly, Category="Desktop Pet|Input") bool bOverScene = false;
};
