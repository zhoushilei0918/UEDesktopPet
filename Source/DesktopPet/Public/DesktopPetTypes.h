#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "DesktopPetTypes.generated.h"
class AActor;

/** 面向使用者的窗口模式；透明、置顶和鼠标穿透由各自独立选项控制。 */
UENUM(BlueprintType)
enum class EDesktopPetWindowMode : uint8
{
    /** 实例沿用项目设置；旧地图新增此字段后也能继承项目默认值。 */
    ProjectDefault UMETA(DisplayName="使用项目设置"),
    /** 普通应用窗口：出现在任务栏、Alt+Tab 和常规录屏窗口列表中。 */
    Application UMETA(DisplayName="标准应用窗口"),
    /** 工具窗口：隐藏任务栏和 Alt+Tab 入口，部分录屏软件会过滤此类窗口。 */
    ToolWindow UMETA(DisplayName="悬浮工具窗口")
};

/** 一个桌宠实例的完整配置；可在蓝图中 Make / Set Members 后交给 PetApplyRuntimeConfig。 */
USTRUCT(BlueprintType)
struct DESKTOPPET_API FDesktopPetConfig
{
    GENERATED_BODY()
    /** 只控制桌宠的系统窗口身份；默认继承 Project Settings → Pet，不改变画面和输入穿透。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window", meta=(DisplayName="窗口模式"))
    EDesktopPetWindowMode WindowMode = EDesktopPetWindowMode::ProjectDefault;
    /** 关闭时隐藏桌宠窗口并恢复普通 UE 游戏窗口，便于调试。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window") bool bTransparentWindowEnabled=true;
    /** 直接设置尺寸时保持窗口中心；滚轮和 PetZoomAtScreenPosition 始终以鼠标为中心。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window") bool bCenterAnchoredScaling=true;
    /** 在人物上滚动鼠标滚轮时自动缩放，无需增强输入。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Input") bool bEnableWheelZoom=true;
    /** 每格滚轮的相对缩放量，例如 0.1 表示约 10%。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Input", meta=(ClampMin="0.01",ClampMax="0.5")) float WheelZoomStep=0.1f;
    /** 鼠标锚点缩放的过渡秒数；使用真实时间，不受游戏时间倍率影响，0 表示立即完成。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Input", meta=(ClampMin="0",ClampMax="1")) float ZoomAnimationSeconds=0.18f;
    /** 仅在不透明内部做受限锐化，保留边缘 Alpha，0 表示关闭。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Quality", meta=(ClampMin="0",ClampMax="1")) float Sharpness=0.2f;
    /** 窗口的逻辑画布尺寸；最终物理尺寸还要乘以 DisplayScale。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window")
    FIntPoint WindowSize = FIntPoint(600,760);
    /** 窗口左上角在虚拟桌面中的物理像素坐标，允许负坐标。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window")
    FIntPoint WindowPosition = FIntPoint(120,100);
    /** 整个显示面的缩放倍率，人物与 UI 同步缩放；不是修改角色世界缩放。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window", meta=(ClampMin="0.25",ClampMax="3"))
    float DisplayScale = 1.f;
    /** 是否把桌宠原生窗口置顶。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window")
    bool bAlwaysOnTop = true;
    /** 隐藏、禁用 UE 游戏窗口并从任务栏/Alt+Tab 中排除；不影响编辑器主窗口。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window")
    bool bHideGameWindow = true;
    /** 是否允许 PetBeginWindowDrag 启动拖拽。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Input")
    bool bDraggable = true;
    /** 用户关闭桌宠时是否退出游戏进程；编辑器 PIE 不退出编辑器。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Window")
    bool bExitApplicationOnClose = true;
    /** 每个方向的超采样倍率；2 表示四个采样，保留平滑的边缘 Alpha。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Quality", meta=(ClampMin="1",ClampMax="4"))
    int32 SupersampleScale = 2;
    /** 透明帧的输出频率上限；不直接修改全局游戏帧率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Quality", meta=(ClampMin="5",ClampMax="60"))
    int32 TargetFrameRate = 30;
    /** 捕获颜色的曝光倍数，修改后下一帧生效。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Quality", meta=(ClampMin="0.01",ClampMax="16"))
    float Exposure = 1.f;
    /** 启用按命中阈值和 UI 命中判定的额外穿透；零 Alpha 像素始终由 Windows 穿透。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Input")
    bool bClickThroughTransparentPixels = true;
    /** 接收场景鼠标事件所需的 Alpha，范围 1–255；不会裁剪视觉透明度。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Input", meta=(ClampMin="1",ClampMax="255"))
    int32 HitAlphaThreshold = 8;
    /** 悬停离开后的交互保持时间；只产生通用进度事件，不创建菜单。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Interaction", meta=(ClampMin="0",ClampMax="10"))
    float MenuCloseDelay = .4f;
    /** 通用交互展开进度的过渡时间，由项目自行驱动 UI。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Interaction", meta=(ClampMin="0.01",ClampMax="5"))
    float MenuAnimationSeconds = .18f;
    /** 是否定期输出运行诊断与透明 PNG；正常运行建议关闭。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Diagnostics")
    bool bWriteDiagnostics = false;
    /** 是否捕获普通半透明材质与 Niagara 的半透明粒子。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Effects")
    bool bCaptureTranslucency = true;
    /** 对有颜色但无 Alpha 的 Additive 像素重建覆盖率，避免发光粒子被丢弃。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Effects")
    bool bPreserveAdditiveEffects = true;
    /** Additive 覆盖率重建强度；Windows 使用 source-over，不能完全复现对任意桌面的加法混合。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Effects", meta=(ClampMin="0.01",ClampMax="4"))
    float AdditiveAlphaStrength = 1.f;
    /** 小型桌宠缓存预算：只在隐藏主窗口的独立游戏中生效；PIE 和普通调试窗口保持项目原值。大型场景可关闭或增加容量。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Memory") bool bCompactMemory = true;
    /** VSM 阴影页池容量上限，不修改每页精度；页数不足时引擎会降级，增加灯光/模型后应重新评估。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Memory", meta=(ClampMin="512",ClampMax="16384")) int32 ShadowPageCapacity = 512;
    /** Lumen 表面缓存图集的边长上限，不修改单张 Card 的采样密度；复杂场景需要更大的图集。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Memory", meta=(ClampMin="1024",ClampMax="8192")) int32 SurfaceCacheCapacity = 1024;
    /** Lumen 辐射缓存图集每轴的探针数上限，不修改单探针分辨率；有效容量为此值的平方。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Memory", meta=(ClampMin="64",ClampMax="256")) int32 RadianceProbeCapacity = 64;
    /** 可复用渲染目标池保留的最低容量（MiB），不是硬上限；正在使用的纹理永远不会因此被释放。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="DesktopPet|Memory", meta=(ClampMin="0",ClampMax="1000")) int32 IdleRenderTargetPoolMB = 64;
    /** 将外部配置约束到合法范围，避免超大纹理或除零。 */
    void Normalize();
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
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") EDesktopPetPointerEvent Type = EDesktopPetPointerEvent::Move;
    /** 鼠标键；移动和滚轮事件可为空键。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") FKey Key;
    /** 原生窗口内的物理像素坐标。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") FVector2D PixelPosition = FVector2D::ZeroVector;
    /** 除以显示缩放后的 UI 逻辑画布坐标。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") FVector2D CanvasPosition = FVector2D::ZeroVector;
    /** 当前相对上一次指针位置的物理像素增量。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") FVector2D Delta = FVector2D::ZeroVector;
    /** 指针命中的交互白名单 Actor；UI、空白或未选择对象上为空。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") TObjectPtr<AActor> HitActor=nullptr;
    /** 鼠标滚轮格数；可以为负值。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") float WheelDelta = 0;
    /** 当前点是否命中交互 UI；项目可以用它决定是否消费输入。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") bool bOverUI = false;
    /** 当前点是否命中场景 Alpha。 */
    UPROPERTY(BlueprintReadOnly, Category="DesktopPet|Input") bool bOverScene = false;
};
