#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "DesktopPetTypes.h"
#include "DesktopPetActor.generated.h"
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UUserWidget;
class UPrimitiveComponent;
struct FDesktopPetRuntime;
class FDesktopPetViewExtension;
class ACameraActor;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetHover,bool,bHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetAction,FName,Action);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDesktopPetClick);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetActorEvent,AActor*,Actor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetPointer,const FDesktopPetPointerEvent&,Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDesktopPetInteraction,float,Progress,FVector2D,CanvasSize);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetDrag,bool,bDragging);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetTray,bool,bHiddenToTray);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetConfigChanged,const FDesktopPetConfig&,Config);

/** 通用透明显示宿主：只接收场景白名单和可选 Widget，不创建任何角色、灯光或菜单。 */
UCLASS(Blueprintable)
class DESKTOPPET_API ADesktopPetActor : public AActor
{
    GENERATED_BODY()
public:
    ADesktopPetActor();
    virtual ~ADesktopPetActor() override;
    /** 项目自行设置相机位置、投影和视角；模型始终留在项目场景中。 */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DesktopPet|Capture") TObjectPtr<USceneCaptureComponent2D> Capture;
    /** 要显示的 Actor，包括角色、道具及含 NiagaraComponent 的特效 Actor。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Capture") TArray<TObjectPtr<AActor>> VisibleActors;
    /** 组件级白名单，方便只显示一个 Actor 的部分模型或某个 NiagaraComponent。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Capture") TArray<TObjectPtr<UPrimitiveComponent>> VisibleComponents;
    /** 具有该 Actor Tag 的对象也会进入白名单；None 表示不按标签收集。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Capture") FName VisibleActorTag=TEXT("DesktopPetVisible");
    /** 是否显示本宿主的 PrimitiveComponent；通用宿主默认没有模型。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Capture") bool bIncludeOwnComponents=false;
    /** BeginPlay 是否自动创建桌宠原生窗口。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Lifecycle") bool bAutoStart=true;
    /** 选中后使用本 Actor 的 InitialConfig，否则使用项目 ini 默认值。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Configuration") bool bOverrideDefaultConfig=false;
    /** 可在关卡实例或蓝图默认值上填写的起始配置。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Configuration",meta=(EditCondition="bOverrideDefaultConfig"))
    FDesktopPetConfig InitialConfig;
    /** 指针进入白名单 Actor 时触发一次，并给出该 Actor。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetActorEvent PetActorMouseEnter;
    /** 指针移出、进入 UI、移除白名单、停止显示或销毁对象时触发离开。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetActorEvent PetActorMouseLeave;
    /** 左键按下命中白名单 Actor 时触发；UI 优先，不以窗口整体代替 Actor。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetActorEvent PetActorLeftClicked;
    /** 右键按下命中白名单 Actor 时触发。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetActorEvent PetActorRightClicked;
    /** 获取由可见名单推导的交互对象副本，不包含失效对象。 */
    /** 鼠标移入的蓝图覆写入口：可直接添加事件，无需手动绑定；Actor 为命中的可见对象。 */
    UFUNCTION(BlueprintImplementableEvent,Category="DesktopPetEvent") void PetEventActorMouseEnter(AActor* Actor);
    /** 鼠标移出的蓝图覆写入口：可直接添加事件，无需手动绑定；Actor 为命中的可见对象。 */
    UFUNCTION(BlueprintImplementableEvent,Category="DesktopPetEvent") void PetEventActorMouseLeave(AActor* Actor);
    /** 持续悬停的蓝图覆写入口：可直接添加事件，无需手动绑定；Actor 为命中的可见对象。 */
    UFUNCTION(BlueprintImplementableEvent,Category="DesktopPetEvent") void PetEventActorMouseHover(AActor* Actor);
    /** 左键点击的蓝图覆写入口：可直接添加事件，无需手动绑定；Actor 为命中的可见对象。 */
    UFUNCTION(BlueprintImplementableEvent,Category="DesktopPetEvent") void PetEventActorLeftClicked(AActor* Actor);
    /** 右键点击的蓝图覆写入口：可直接添加事件，无需手动绑定；Actor 为命中的可见对象。 */
    UFUNCTION(BlueprintImplementableEvent,Category="DesktopPetEvent") void PetEventActorRightClicked(AActor* Actor);

    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Interaction") TArray<AActor*> PetGetInteractionActors() const;
    /** 当前悬停的白名单 Actor；空白或 UI 上返回 None。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Interaction") AActor* PetGetHoveredActor() const;
    /** 以桌宠窗口内物理像素查询 Actor，可与项目自己的增强输入组合；不广播事件。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Interaction") AActor* PetGetInteractionActorAtPixel(FVector2D PixelPosition) const;
    /** 可选项目 Widget 类；也可通过 PetSetOverlayWidget 传入已经创建的实例。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|UI") TSubclassOf<UUserWidget> WidgetOverride;
    /** 兼容旧接口：是否悬停在白名单 Actor 上；需要对象引用请使用 Actor Events。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetHover PetHoverChanged;
    /** 通用用户动作总线；插件不解释 Chat、Quit 等项目业务名称。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetAction PetAction;
    /** 兼容旧接口：白名单 Actor 的左键通知；新接口同时返回 Actor 引用。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetClick PetClicked;
    /** 原始指针事件，项目可以桥接到自己的输入或交互系统。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetPointer PetPointerInput;
    /** 0–1 的交互展开进度；具体菜单排布、动画和可见性完全由项目控制。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetInteraction PetInteractionProgress;
    /** 原生窗口拖拽的开始/结束通知。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetDrag PetDragStateChanged;
    /** 配置已经应用到窗口及渲染目标后发出，不在 Setter 调用中提前广播。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetConfigChanged PetRuntimeConfigApplied;

    /** 创建通用窗口及捕获资源；允许没有 Widget 或没有人物。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Lifecycle") bool PetStartDesktopWindow();
    /** 停止透明显示；不会自动恢复被要求隐藏的 UE 窗口。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Lifecycle") void PetStopDesktopWindow();
    /** 请求下一 Tick 重新创建；普通运行时修改无需调用此函数。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Lifecycle") void PetRestartDesktopWindow();
    /** 请求关闭桌宠，是否退出游戏由 bExitApplicationOnClose 决定。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Lifecycle") void PetRequestClose();
    /** 查询是否已启动。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Lifecycle") bool PetIsDesktopWindowRunning() const;
    /** 隐藏到系统托盘；只有托盘入口创建成功才返回 true，已经收起时重复调用也返回 true。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Tray") bool PetHideToTray();
    /** 显示原桌宠窗口并移除托盘图标；也由托盘双击自动调用。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Tray") bool PetRestoreFromTray();
    /** 查询本实例是否收在托盘中；停止/销毁实例会删除其托盘入口。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Tray") bool PetIsHiddenToTray() const;
    /** 托盘状态变化：true 为收起，false 为恢复。项目可以据此暂停自己的音频等业务。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetTray PetTrayStateChanged;
    /** PetActor 蓝图可直接添加此事件，不需要手动绑定委托。 */
    UFUNCTION(BlueprintImplementableEvent,Category="DesktopPetEvent") void PetEventTrayStateChanged(bool bHiddenToTray);
    /** 独立控制任务栏按钮；不更改录屏许可，也不隐藏进程。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetShowInTaskbar(bool bShow);
    /** 查询任务栏按钮的配置请求值；收进托盘时按钮始终隐藏。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") bool PetGetShowInTaskbar() const {return PetGetWindowMode()!=EDesktopPetWindowMode::ToolWindow;}
    /** 修改 Windows 窗口捕获许可；排除效果取决于操作系统及软件所用捕获接口。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetAllowScreenCapture(bool bAllow);
    /** 查询捕获许可配置。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") bool PetGetAllowScreenCapture() const {return PetGetRuntimeConfig().bAllowScreenCapture;}
    /** 启停独立桌宠的主循环限帧；关闭后恢复项目自己的上限。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Performance") void PetSetLimitEngineFrameRate(bool bEnabled);
    /** 查询自动限帧开关。 */
    /** 下一次捕获使用 GPU 合成；不会修改角色、UI、材质或项目渲染设置。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Performance") void PetSetGPUCompositingEnabled(bool bEnabled);
    /** 读取实例请求的合成方式。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Performance") bool PetGetGPUCompositingEnabled() const {return PetGetRuntimeConfig().bUseGPUCompositing;}
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Performance") bool PetGetLimitEngineFrameRate() const {return PetGetRuntimeConfig().bLimitEngineFrameRate;}
    /** 设置全部实例收起时的后台 Tick 上限，允许 1–30 Hz。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Performance") void PetSetTrayFrameRate(int32 FPS);
    /** 查询后台帧率配置。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Performance") int32 PetGetTrayFrameRate() const {return PetGetRuntimeConfig().TrayFrameRate;}
    /** 引擎当前有效 t.MaxFPS；0 为不限帧，不代表实际测得 FPS。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Performance") float PetGetEffectiveEngineFrameRateLimit() const;
    /** 重新收集 Actor/组件/标签白名单，支持运行时生成的 Niagara。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Capture") void PetRefreshVisibleActors();
    /** 添加一个运行时对象并立即刷新捕获白名单。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Capture") void PetAddVisibleActor(AActor* Actor);
    /** 添加单个渲染组件，例如 NiagaraComponent。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Capture") void PetAddVisibleComponent(UPrimitiveComponent* Component);
    /** 移除白名单中的 Actor 并刷新。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Capture") void PetRemoveVisibleActor(AActor* Actor);
    /** 设置或清空项目创建的 Widget；UI 所有权和业务逻辑保留在项目中。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|UI") void PetSetOverlayWidget(UUserWidget* Widget);
    /** 获取当前项目 Widget，可能为空。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|UI") UUserWidget* PetGetOverlayWidget() const {return OverlayWidget;}
    /** 获取实例配置副本，可用 Set Members 修改后 Apply。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") FDesktopPetConfig PetGetRuntimeConfig() const;
    /** 获取 ini 中的新实例默认值；不会修改其他桌宠实例。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") static FDesktopPetConfig PetGetDefaultRuntimeConfig();

    // 每个配置项都有独立纯蓝图 Getter；统一读取实例配置，包含等待下一 Tick 应用的修改。
    /** 获取当前实例的逻辑画布尺寸。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") FIntPoint PetGetWindowSize() const {return PetGetRuntimeConfig().WindowSize;}
    /** 获取当前实例的显示缩放。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") float PetGetDisplayScale() const {return PetGetRuntimeConfig().DisplayScale;}
    /** 获取解析项目默认值后的有效窗口模式。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") EDesktopPetWindowMode PetGetWindowMode() const {auto C=PetGetRuntimeConfig();C.Normalize();return C.WindowMode;}
    /** 获取当前实例的置顶状态。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetAlwaysOnTop() const {return PetGetRuntimeConfig().bAlwaysOnTop;}
    /** 获取当前实例的主窗口隐藏策略。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetHideGameWindow() const {return PetGetRuntimeConfig().bHideGameWindow;}
    /** 获取当前实例的拖拽许可。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetDraggable() const {return PetGetRuntimeConfig().bDraggable;}
    /** 获取当前实例的关闭时退出策略。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetExitApplicationOnClose() const {return PetGetRuntimeConfig().bExitApplicationOnClose;}
    /** 获取当前实例的超采样倍率。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") int32 PetGetSupersampleScale() const {return PetGetRuntimeConfig().SupersampleScale;}
    /** 获取当前实例的输出帧率上限。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") int32 PetGetTargetFrameRate() const {return PetGetRuntimeConfig().TargetFrameRate;}
    /** 获取当前实例的曝光。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") float PetGetExposure() const {return PetGetRuntimeConfig().Exposure;}
    /** 获取当前实例的透明区域穿透策略。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetClickThroughTransparentPixels() const {return PetGetRuntimeConfig().bClickThroughTransparentPixels;}
    /** 获取当前实例的输入 Alpha 阈值。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") int32 PetGetHitAlphaThreshold() const {return PetGetRuntimeConfig().HitAlphaThreshold;}
    /** 获取当前实例的离开保持时间。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") float PetGetMenuCloseDelay() const {return PetGetRuntimeConfig().MenuCloseDelay;}
    /** 获取当前实例的交互进度过渡时间。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") float PetGetMenuAnimationSeconds() const {return PetGetRuntimeConfig().MenuAnimationSeconds;}
    /** 获取当前实例的诊断开关。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetWriteDiagnostics() const {return PetGetRuntimeConfig().bWriteDiagnostics;}
    /** 获取当前实例的半透明捕获开关。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetCaptureTranslucency() const {return PetGetRuntimeConfig().bCaptureTranslucency;}
    /** 获取当前实例的加法覆盖率重建开关。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") bool PetGetPreserveAdditiveEffects() const {return PetGetRuntimeConfig().bPreserveAdditiveEffects;}
    /** 获取当前实例的加法覆盖率强度。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Configuration") float PetGetAdditiveAlphaStrength() const {return PetGetRuntimeConfig().AdditiveAlphaStrength;}

    /** 开关紧凑缓存预算，下一 Tick 应用；全局 CVar 由所有运行中的桌宠共同协商。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Memory") void PetSetCompactMemory(bool bEnabled);
    /** 配置缓存容量上限，自动向上取整到二次幂；不会修改 SSAA、曝光、Lumen 质量或材质。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Memory") void PetSetMemoryCapacities(int32 ShadowPages,int32 SurfaceAtlasSize,int32 RadianceProbes,int32 IdlePoolMB=64);
    /** 获取当前实例的 bCompactMemory 配置；实际全局值可能被其他宿主或控制台的更高优先级设置覆盖。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Memory") bool PetGetCompactMemory() const {return PetGetRuntimeConfig().bCompactMemory;}
    /** 获取当前实例的 ShadowPageCapacity 配置；实际全局值可能被其他宿主或控制台的更高优先级设置覆盖。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Memory") int32 PetGetShadowPageCapacity() const {return PetGetRuntimeConfig().ShadowPageCapacity;}
    /** 获取当前实例的 SurfaceCacheCapacity 配置；实际全局值可能被其他宿主或控制台的更高优先级设置覆盖。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Memory") int32 PetGetSurfaceCacheCapacity() const {return PetGetRuntimeConfig().SurfaceCacheCapacity;}
    /** 获取当前实例的 RadianceProbeCapacity 配置；实际全局值可能被其他宿主或控制台的更高优先级设置覆盖。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Memory") int32 PetGetRadianceProbeCapacity() const {return PetGetRuntimeConfig().RadianceProbeCapacity;}
    /** 获取当前实例的 IdleRenderTargetPoolMB 配置；实际全局值可能被其他宿主或控制台的更高优先级设置覆盖。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Memory") int32 PetGetIdleRenderTargetPoolMB() const {return PetGetRuntimeConfig().IdleRenderTargetPoolMB;}
    /** 当前实例是否参与全局紧凑缓存协商；不代表显存硬限制或每个 CVar 均未被控制台覆盖。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Memory") bool PetIsCompactMemoryActive() const;

    /** 整组应用所有参数；合并到下一 Tick，避免在 UMG 回调内销毁绘制资源。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Configuration") void PetApplyRuntimeConfig(const FDesktopPetConfig& Config);
    /** 设置未缩放的逻辑画布大小，UI 布局以此为准。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetWindowSize(FIntPoint Size);
    /** 直接设置最终物理显示大小，保留当前缩放倍率并反算逻辑尺寸。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetDisplaySize(FIntPoint PhysicalSize);
    /** 均匀缩放人物及 UI，保持逻辑画布布局。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetDisplayScale(float Scale);
    /** 获取最终物理显示大小。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") FIntPoint PetGetDisplaySize() const;
    /** 设置桌面物理像素坐标。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetWindowPosition(FIntPoint Position);
    /** 获取实际窗口位置，包括拖拽之后的位置。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") FIntPoint PetGetWindowPosition() const;
    /** 兼容旧蓝图的任务栏策略接口；新蓝图推荐 PetSetShowInTaskbar，窗口始终保持可发现身份。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetWindowMode(EDesktopPetWindowMode Mode);
    /** 切换置顶状态。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetAlwaysOnTop(bool Value);
    /** 切换 UE 游戏窗口隐藏与输入禁用；编辑器主窗口不受影响。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetHideGameWindow(bool Value);
    /** 启停拖拽许可；关闭时会结束正在进行的拖拽。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetSetDraggable(bool Value);
    /** 可接 IA_Drag 的 Started，或由项目 UMG 按下事件调用。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") bool PetBeginWindowDrag();
    /** 可接 IA_Drag 的 Completed / Canceled，或项目 UMG 抬起事件。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetEndWindowDrag();
    /** 查询原生窗口是否处于拖拽状态。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Input") bool PetIsWindowDragging() const;
    /** 修改超采样倍率；重建纹理但保持 HWND 和 Widget 实例不变。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Quality") void PetSetSupersampleScale(int32 Scale);
    /** 修改捕获频率上限，下一 Tick 生效。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Quality") void PetSetTargetFrameRate(int32 FPS);
    /** 修改颜色曝光。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Quality") void PetSetExposure(float Value);
    /** 修改额外 Alpha 命中穿透策略。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetSetClickThroughTransparentPixels(bool Value);
    /** 修改命中阈值，视觉透明度不变。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetSetHitAlphaThreshold(int32 Value);
    /** 修改通用交互保持时间和过渡时间。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|UI") void PetSetInteractionTiming(float CloseDelay,float AnimationSeconds);
    /** 修改诊断开关。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Diagnostics") void PetSetWriteDiagnostics(bool Value);
    /** 广播项目定义的动作；具体行为由项目绑定。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Events") void PetDispatchAction(FName Action);
    /** 保存当前透明帧与状态数据。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Diagnostics") void PetSaveDiagnosticFrame();
    /** 获取当前交互进度，方便 UI 初次接入时同步状态。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|UI") float PetGetInteractionProgress() const;
    /** 查询已输出帧数量。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Diagnostics") int64 PetGetPresentedFrameCount() const;
    /** C++ 原生 Windows 集成入口，不将 HWND 暴露给蓝图。 */
    void* GetNativeWindowHandle() const;
    /** 持续悬停时每帧触发，参数为可见名单中的实际 Actor。菜单开启期间暂停。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetActorEvent PetActorMouseHover;
    /** 双击菜单显示状态变化；与角色/菜单业务解耦。 */
    UPROPERTY(BlueprintAssignable,Category="DesktopPetEvent") FDesktopPetHover PetConfigWidgetVisibilityChanged;
    /** 双击时创建的项目 Widget 类；未配置时双击不打开任何菜单。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|UI") TSubclassOf<UUserWidget> ConfigWidgetClass;
    /** 设置菜单类；下次打开时使用新类，传 None 可禁用。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|UI") void PetSetConfigWidgetClass(TSubclassOf<UUserWidget> WidgetClass);
    /** 打开菜单并暂停人物事件、拖拽和环绕，鼠标离开窗口矩形自动关闭。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|UI") bool PetOpenConfigWidget();
    /** 关闭菜单并恢复人物交互。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|UI") void PetCloseConfigWidget();
    /** 获取当前菜单显示状态。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|UI") bool PetIsConfigWidgetOpen() const {return bConfigWidgetOpen;}
    /** 获取插件创建的菜单实例，可在蓝图中初始化其业务数据。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|UI") UUserWidget* PetGetConfigWidget() const {return ConfigWidget;}
    /** 有效聚焦对象启用右键环绕；不设置时保持当前固定视角。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Orbit") TObjectPtr<AActor> OrbitFocusActor;
    /** 相对聚焦 Actor 原点的世界轴偏移，单位厘米，可用于对准胸口。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Orbit") FVector OrbitCenterOffset=FVector(0,0,80);
    /** 环绕臂长，单位厘米。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Orbit",meta=(ClampMin="10")) float OrbitDistance=250.f;
    /** 初始环绕角度；Yaw 为左右，Pitch 为上下，不使用 Roll。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Orbit") FRotator OrbitRotation=FRotator(0,-90,0);
    /** 每个鼠标物理像素对应的旋转角度，按显示缩放归一化。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DesktopPet|Orbit",meta=(ClampMin="0.01",ClampMax="5")) float OrbitSensitivity=0.2f;
    /** 设置聚焦对象；保留当前视角时自动计算当前臂长和朝向。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Orbit") void PetSetOrbitFocusActor(AActor* Actor,bool bKeepCurrentView=true);
    /** 获取有效聚焦对象。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Orbit") AActor* PetGetOrbitFocusActor() const {return OrbitFocusActor;}
    /** 设置环绕中心相对偏移和臂长。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Orbit") void PetSetOrbitSettings(FVector CenterOffset,float ArmLength,float Sensitivity=0.2f);
    /** 获取世界坐标中的环绕中心。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Orbit") FVector PetGetOrbitCenter() const;
    /** 获取当前环绕臂长。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Orbit") float PetGetOrbitDistance() const {return OrbitDistance;}
    /** 设置观察角度，俯仰限制在 -85 到 85 度。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Orbit") void PetSetOrbitRotation(FRotator Rotation);
    /** 根据可见组件包围盒自动对准一个 Actor 并让它填充镜头。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Orbit") bool PetFrameActor(AActor* Actor,float Margin=1.15f);
    /** 恢复 Start 时的观察相机和环绕参数。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Orbit") void PetResetView();
    /** 切换透明桌宠/正常 UE 窗口，支持运行时修改。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetTransparentWindowEnabled(bool Enabled);
    /** 获取透明窗口模式。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") bool PetGetTransparentWindowEnabled() const {return PetGetRuntimeConfig().bTransparentWindowEnabled;}
    /** 启停原生滚轮缩放。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetSetWheelZoomEnabled(bool Enabled);
    /** 获取原生滚轮缩放开关。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Input") bool PetGetWheelZoomEnabled() const {return PetGetRuntimeConfig().bEnableWheelZoom;}
    /** 设置清晰度增强强度，保留 Alpha 与 UI 原始像素。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Quality") void PetSetSharpness(float Value);
    /** 获取清晰度增强强度。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Quality") float PetGetSharpness() const {return PetGetRuntimeConfig().Sharpness;}
    /** 查询中心锚定缩放状态。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Window") bool PetGetCenterAnchoredScaling() const {return PetGetRuntimeConfig().bCenterAnchoredScaling;}
    /** 设置中心锚定缩放。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Window") void PetSetCenterAnchoredScaling(bool Enabled);
    /** 获取滚轮缩放步长。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Input") float PetGetWheelZoomStep() const {return PetGetRuntimeConfig().WheelZoomStep;}
    /** 设置滚轮缩放步长。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetSetWheelZoomStep(float Value);
    /** 增强输入也可调用；ScreenPosition 为物理屏幕坐标，仅命中可见 Actor 且未被 UI 遮挡时接受。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") bool PetZoomAtScreenPosition(float WheelDelta,FVector2D ScreenPosition);
    /** 当前是否正在执行鼠标锚点平滑缩放。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Input") bool PetIsZooming() const;
    /** 连续滚轮积累后的目标倍率；PetGetDisplayScale 返回当前已显示的倍率。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Input") float PetGetZoomTargetScale() const;
    /** 运行时设置鼠标缩放动画时长。 */
    UFUNCTION(BlueprintCallable,Category="DesktopPet|Input") void PetSetZoomAnimationSeconds(float Value);
    /** 获取鼠标缩放动画时长。 */
    UFUNCTION(BlueprintPure,Category="DesktopPet Utility|Input") float PetGetZoomAnimationSeconds() const {return PetGetRuntimeConfig().ZoomAnimationSeconds;}
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** 从实际深度取得不透明覆盖率，保留不写 Alpha 的描边等 Pass。 */
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> GeometryTarget;
    TSharedPtr<FDesktopPetViewExtension,ESPMode::ThreadSafe> ViewExtension;
    /** 菜单实例和显示状态不写入地图资产。 */
    UPROPERTY(Transient) TObjectPtr<UUserWidget> ConfigWidget;
    bool bConfigWidgetOpen=false;
    /** 调试模式中的普通游戏相机，退出时恢复项目原来的 ViewTarget。 */
    UPROPERTY(Transient) TObjectPtr<ACameraActor> DebugCamera;
    UPROPERTY(Transient) TWeakObjectPtr<AActor> SavedViewTarget;
    FTransform InitialCaptureTransform;
    FRotator InitialOrbitRotation;
    float InitialOrbitDistance=250.f;
    void UpdateOrbitCamera();
    void UpdateDebugCamera();
    void RestoreDebugCamera();
    /** GPU 场景捕获目标，存储 HDR RGB 和反透明度。 */
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> SceneTarget;
    /** 引擎后处理后的线性 sRGB 颜色；不依赖此纹理的 Alpha。 */
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> FinalColorTarget;
    /** 独立保留模型/半透明覆盖率，避免 FinalColor 捕获丢失透明背景。 */
    UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> OpacityCapture;
    /** 应用颜色模式，始终由 Capture 作为用户配置的主相机。 */
    void ConfigureCapturePipeline();
    /** 同步两个捕获的相机和显示名单，保证颜色与轮廓逐帧对齐。 */
    void SyncOpacityCapture();
    /** UMG 单独捕获，避免场景筛选剔除 UI。 */
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> UITarget;
    /** 项目交给插件显示的实例；插件不强制其 Widget 类。 */
    UPROPERTY(Transient) TObjectPtr<UUserWidget> OverlayWidget;
    FDesktopPetConfig DesiredConfig;
    bool bHasConfig=false,bConfigPending=false,bWidgetPending=false,bRestartRequested=false;
    /** 保留精确中心，批量滚轮和往返缩放不累计整数舍入误差。 */
    FVector2D ExactWindowCenter=FVector2D::ZeroVector;
    FIntPoint CenterAnchorPosition=FIntPoint::ZeroValue;
    bool bCenterAnchorValid=false;
    TSharedPtr<FDesktopPetRuntime> Runtime;
    /** 悬停不延长 Actor 生命周期；销毁通知在对象失效之前清理事件状态。 */
    UPROPERTY(Transient) TWeakObjectPtr<AActor> HoveredInteractionActor;
    bool bStopping=false;
    /** 根据捕获相机和已显示组件包围盒自动拾取，不依赖碰撞或 Physics Asset。 */
    AActor* TraceInteractionActor(FVector2D UV) const;
    /** 处理 A→B、Actor→UI 等状态迁移，保证先 Leave 再 Enter。 */
    void UpdateHoveredActor(AActor* Actor);
    /** 被悬停对象销毁时发出最后一次离开，防止悬停状态残留。 */
    UFUNCTION() void HandleHoveredActorDestroyed(AActor* Actor);
    friend struct FDesktopPetRuntime;
};
