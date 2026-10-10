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
    /** 在项目设置中使用独立 Pet 分类；ini 节名不变，兼容已有工程配置。 */
    UDesktopPetSettings() { CategoryName=TEXT("Pet"); }
    /** 是否显示任务栏按钮；与屏幕捕获许可独立。进程始终在任务管理器中正常可见。 */
    UPROPERTY(Config, EditAnywhere, Category="窗口标记", meta=(DisplayName="任务栏显示", InvalidEnumValues="ProjectDefault", DisplayPriority="0"))
    EDesktopPetWindowMode WindowMode = EDesktopPetWindowMode::Application;
    /** 正常进程始终在任务管理器可见。此项仅控制 Windows 对窗口的捕获许可；部分旧捕获路径可能不遵循排除设置。 */
    UPROPERTY(Config, EditAnywhere, Category="窗口标记", meta=(DisplayName="允许屏幕捕获"))
    bool bAllowScreenCapture=true;
    /** 独立透明桌宠自动限制引擎主循环；30 FPS 输出使用最高 120 Hz 调度。PIE/普通游戏视口不受影响，不降低图像质量。 */
    UPROPERTY(Config, EditAnywhere, Category="性能", meta=(DisplayName="自动限制引擎帧率"))
    bool bLimitEngineFrameRate=true;
    /** 全部桌宠收进托盘后的引擎 Tick 上限；桌宠捕获完全停止，项目游戏逻辑按此频率继续运行。 */
    UPROPERTY(Config, EditAnywhere, Category="性能", meta=(DisplayName="托盘后台帧率", ClampMin="1", ClampMax="30"))
    int32 TrayFrameRate=5;
    /** 关闭时隐藏桌宠窗口并恢复普通 UE 游戏窗口，便于调试。 */
    UPROPERTY(Config, EditAnywhere, Category="Window") bool bTransparentWindowEnabled=true;
    /** 直接设置尺寸时保持窗口中心；滚轮和 PetZoomAtScreenPosition 始终以鼠标为中心。 */
    UPROPERTY(Config, EditAnywhere, Category="Window") bool bCenterAnchoredScaling=true;
    /** 在人物上滚动鼠标滚轮时自动缩放，无需增强输入。 */
    UPROPERTY(Config, EditAnywhere, Category="Input") bool bEnableWheelZoom=true;
    /** 每格滚轮的相对缩放量，例如 0.1 表示约 10%。 */
    UPROPERTY(Config, EditAnywhere, Category="Input", meta=(ClampMin="0.01",ClampMax="0.5")) float WheelZoomStep=0.1f;
    /** 鼠标锚点缩放的过渡秒数；使用真实时间，不受游戏时间倍率影响，0 表示立即完成。 */
    UPROPERTY(Config, EditAnywhere, Category="Input", meta=(ClampMin="0",ClampMax="1")) float ZoomAnimationSeconds=0.18f;
    /** 仅在不透明内部做受限锐化，保留边缘 Alpha，0 表示关闭。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="0",ClampMax="1")) float Sharpness=0.2f;
    /** 窗口的逻辑画布尺寸；最终物理尺寸还要乘以 DisplayScale。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    FIntPoint WindowSize = FIntPoint(600,760);
    /** 窗口左上角在虚拟桌面中的物理像素坐标，允许负坐标。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    FIntPoint WindowPosition = FIntPoint(120,100);
    /** 整个显示面的缩放倍率，人物与 UI 同步缩放；不是修改角色世界缩放。 */
    UPROPERTY(Config, EditAnywhere, Category="Window", meta=(ClampMin="0.25",ClampMax="3"))
    float DisplayScale = 1.f;
    /** 是否把桌宠原生窗口置顶；与任务栏显示和捕获许可独立。 */
    UPROPERTY(Config, EditAnywhere, Category="窗口标记", meta=(DisplayName="始终置顶", DisplayPriority="1"))
    bool bAlwaysOnTop = true;
    /** 仅隐藏并禁用底层 UE 游戏窗口，不隐藏进程，也不影响独立桌宠或编辑器主窗口。 */
    UPROPERTY(Config, EditAnywhere, Category="窗口标记", meta=(DisplayName="隐藏 UE 游戏窗口", DisplayPriority="2"))
    bool bHideGameWindow = true;
    /** 是否允许 PetBeginWindowDrag 启动拖拽。 */
    UPROPERTY(Config, EditAnywhere, Category="Input")
    bool bDraggable = true;
    /** 用户关闭桌宠时是否退出游戏进程；编辑器 PIE 不退出编辑器。 */
    UPROPERTY(Config, EditAnywhere, Category="Window")
    bool bExitApplicationOnClose = true;
    /** 每个方向的超采样倍率；2 表示四个采样，保留平滑的边缘 Alpha。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="1",ClampMax="4"))
    int32 SupersampleScale = 2;
    /** 透明帧输出频率上限；启用自动限帧时会同时协调引擎调度频率，不降低捕获质量。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="5",ClampMax="60"))
    int32 TargetFrameRate = 30;
    /** 捕获颜色的曝光倍数，修改后下一帧生效。 */
    UPROPERTY(Config, EditAnywhere, Category="Quality", meta=(ClampMin="0.01",ClampMax="16"))
    float Exposure = 1.f;
    /** 启用按命中阈值和 UI 命中判定的额外穿透；零 Alpha 像素始终由 Windows 穿透。 */
    UPROPERTY(Config, EditAnywhere, Category="窗口标记", meta=(DisplayName="透明区域鼠标穿透", DisplayPriority="3"))
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
    /** 小型桌宠缓存预算：只在隐藏主窗口的独立游戏中生效；PIE 和普通调试窗口保持项目原值。大型场景可关闭或增加容量。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory") bool bCompactMemory = true;
    /** VSM 阴影页池容量上限，不修改每页精度；页数不足时引擎会降级，增加灯光/模型后应重新评估。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory", meta=(ClampMin="512",ClampMax="16384")) int32 ShadowPageCapacity = 512;
    /** Lumen 表面缓存图集的边长上限，不修改单张 Card 的采样密度；复杂场景需要更大的图集。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory", meta=(ClampMin="1024",ClampMax="8192")) int32 SurfaceCacheCapacity = 1024;
    /** Lumen 辐射缓存图集每轴的探针数上限，不修改单探针分辨率；有效容量为此值的平方。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory", meta=(ClampMin="64",ClampMax="256")) int32 RadianceProbeCapacity = 64;
    /** 可复用渲染目标池保留的最低容量（MiB），不是硬上限；正在使用的纹理永远不会因此被释放。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory", meta=(ClampMin="0",ClampMax="1000")) int32 IdleRenderTargetPoolMB = 64;
    /** 启动时采用较小的分配块，并启用 Cascade 粒子按需扩容；不改变分辨率/采样/粒子数量。引擎在初始化时读取，修改后须重启进程。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory|Startup", meta=(ConfigRestartRequired=true)) bool bCompactStartupAllocations = true;
    /** D3D12 瞬态堆的最小分配块，单位 MiB；按需继续增长，不是总量限制。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory|Startup", meta=(ClampMin="16",ClampMax="128",ConfigRestartRequired=true)) int32 TransientHeapChunkMB = 16;
    /** D3D12 只读纹理池的最小分配块，单位 MiB；不限制贴图尺寸或驻留 mip。 */
    UPROPERTY(Config, EditAnywhere, Category="Memory|Startup", meta=(ClampMin="16",ClampMax="64",ConfigRestartRequired=true)) int32 TexturePoolChunkMB = 16;
    /** 复制默认配置；返回的结构可以在蓝图中自由修改。 */
    FDesktopPetConfig MakeRuntimeConfig() const;
};
