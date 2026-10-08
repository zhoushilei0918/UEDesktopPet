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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetHover,bool,bHovered);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetAction,FName,Action);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDesktopPetClick);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetActorEvent,AActor*,Actor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetPointer,const FDesktopPetPointerEvent&,Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDesktopPetInteraction,float,Progress,FVector2D,CanvasSize);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDesktopPetDrag,bool,bDragging);
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
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Desktop Pet|Capture") TObjectPtr<USceneCaptureComponent2D> Capture;
    /** 要显示的 Actor，包括角色、道具及含 NiagaraComponent 的特效 Actor。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Capture") TArray<TObjectPtr<AActor>> VisibleActors;
    /** 组件级白名单，方便只显示一个 Actor 的部分模型或某个 NiagaraComponent。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Capture") TArray<TObjectPtr<UPrimitiveComponent>> VisibleComponents;
    /** 具有该 Actor Tag 的对象也会进入白名单；None 表示不按标签收集。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Capture") FName VisibleActorTag=TEXT("DesktopPetVisible");
    /** 是否显示本宿主的 PrimitiveComponent；通用宿主默认没有模型。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Capture") bool bIncludeOwnComponents=false;
    /** BeginPlay 是否自动创建桌宠原生窗口。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Lifecycle") bool bAutoStart=true;
    /** 选中后使用本 Actor 的 InitialConfig，否则使用项目 ini 默认值。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Configuration") bool bOverrideDefaultConfig=false;
    /** 可在关卡实例或蓝图默认值上填写的起始配置。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Configuration",meta=(EditCondition="bOverrideDefaultConfig"))
    FDesktopPetConfig InitialConfig;
    /** 可交互 Actor 白名单；为空时没有人物事件。显示名单与交互名单互相独立。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Interaction") TArray<TObjectPtr<AActor>> InteractionActors;
    /** 交互查询通道；被显示的遮挡物与角色须对该通道 Block，默认 Visibility。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Interaction") TEnumAsByte<ECollisionChannel> InteractionTraceChannel=ECC_Visibility;
    /** 反投影射线的最大距离，单位厘米；只影响拾取，不修改相机。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Interaction",meta=(ClampMin="1")) float InteractionTraceDistance=100000.f;
    /** 静态模型可使用复杂碰撞；骨骼模型通常使用 Physics Asset，默认简单查询。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|Interaction") bool bTraceComplexForInteraction=false;
    /** 指针进入白名单 Actor 时触发一次，并给出该 Actor。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Actor Events") FDesktopPetActorEvent OnActorMouseEnter;
    /** 指针移出、进入 UI、移除白名单、停止显示或销毁对象时触发离开。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Actor Events") FDesktopPetActorEvent OnActorMouseLeave;
    /** 左键按下命中白名单 Actor 时触发；UI 优先，不以窗口整体代替 Actor。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Actor Events") FDesktopPetActorEvent OnActorLeftClicked;
    /** 右键按下命中白名单 Actor 时触发。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Actor Events") FDesktopPetActorEvent OnActorRightClicked;
    /** 运行时替换交互名单；移除当前悬停对象时立即发出离开事件。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Interaction") void SetInteractionActors(const TArray<AActor*>& Actors);
    /** 添加交互对象；不会自动把对象加入显示白名单。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Interaction") void AddInteractionActor(AActor* Actor);
    /** 移除交互对象，不影响其渲染。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Interaction") void RemoveInteractionActor(AActor* Actor);
    /** 获取当前交互白名单副本，不包含失效对象。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Interaction") TArray<AActor*> GetInteractionActors() const;
    /** 当前悬停的白名单 Actor；空白或 UI 上返回 None。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Interaction") AActor* GetHoveredActor() const;
    /** 以桌宠窗口内物理像素查询 Actor，可与项目自己的增强输入组合；不广播事件。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Interaction") AActor* GetInteractionActorAtPixel(FVector2D PixelPosition) const;
    /** 可选项目 Widget 类；也可通过 SetOverlayWidget 传入已经创建的实例。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|UI") TSubclassOf<UUserWidget> WidgetOverride;
    /** 兼容旧接口：是否悬停在白名单 Actor 上；需要对象引用请使用 Actor Events。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetHover OnPetHoverChanged;
    /** 通用用户动作总线；插件不解释 Chat、Quit 等项目业务名称。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetAction OnAction;
    /** 兼容旧接口：白名单 Actor 的左键通知；新接口同时返回 Actor 引用。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetClick OnPetClicked;
    /** 原始指针事件，项目可以桥接到自己的输入或交互系统。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetPointer OnPointerInput;
    /** 0–1 的交互展开进度；具体菜单排布、动画和可见性完全由项目控制。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetInteraction OnInteractionProgress;
    /** 原生窗口拖拽的开始/结束通知。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetDrag OnDragStateChanged;
    /** 配置已经应用到窗口及渲染目标后发出，不在 Setter 调用中提前广播。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetConfigChanged OnRuntimeConfigApplied;

    /** 创建通用窗口及捕获资源；允许没有 Widget 或没有人物。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Lifecycle") bool StartDesktopWindow();
    /** 停止透明显示；不会自动恢复被要求隐藏的 UE 窗口。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Lifecycle") void StopDesktopWindow();
    /** 请求下一 Tick 重新创建；普通运行时修改无需调用此函数。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Lifecycle") void RestartDesktopWindow();
    /** 请求关闭桌宠，是否退出游戏由 bExitApplicationOnClose 决定。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Lifecycle") void RequestClose();
    /** 查询是否已启动。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Lifecycle") bool IsDesktopWindowRunning() const;
    /** 重新收集 Actor/组件/标签白名单，支持运行时生成的 Niagara。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Capture") void RefreshVisibleActors();
    /** 添加一个运行时对象并立即刷新捕获白名单。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Capture") void AddVisibleActor(AActor* Actor);
    /** 添加单个渲染组件，例如 NiagaraComponent。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Capture") void AddVisibleComponent(UPrimitiveComponent* Component);
    /** 移除白名单中的 Actor 并刷新。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Capture") void RemoveVisibleActor(AActor* Actor);
    /** 设置或清空项目创建的 Widget；UI 所有权和业务逻辑保留在项目中。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|UI") void SetOverlayWidget(UUserWidget* Widget);
    /** 获取当前项目 Widget，可能为空。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|UI") UUserWidget* GetOverlayWidget() const {return OverlayWidget;}
    /** 获取实例配置副本，可用 Set Members 修改后 Apply。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") FDesktopPetConfig GetRuntimeConfig() const;
    /** 获取 ini 中的新实例默认值；不会修改其他桌宠实例。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") static FDesktopPetConfig GetDefaultRuntimeConfig();

    // 每个配置项都有独立纯蓝图 Getter；统一读取实例配置，包含等待下一 Tick 应用的修改。
    /** 获取当前实例的逻辑画布尺寸。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") FIntPoint GetWindowSize() const {return GetRuntimeConfig().WindowSize;}
    /** 获取当前实例的显示缩放。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") float GetDisplayScale() const {return GetRuntimeConfig().DisplayScale;}
    /** 获取当前实例的置顶状态。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetAlwaysOnTop() const {return GetRuntimeConfig().bAlwaysOnTop;}
    /** 获取当前实例的主窗口隐藏策略。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetHideGameWindow() const {return GetRuntimeConfig().bHideGameWindow;}
    /** 获取当前实例的拖拽许可。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetDraggable() const {return GetRuntimeConfig().bDraggable;}
    /** 获取当前实例的左键自动拖拽策略。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetAutoDragOnPrimaryButton() const {return GetRuntimeConfig().bAutoDragOnPrimaryButton;}
    /** 获取当前实例的关闭时退出策略。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetExitApplicationOnClose() const {return GetRuntimeConfig().bExitApplicationOnClose;}
    /** 获取当前实例的超采样倍率。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") int32 GetSupersampleScale() const {return GetRuntimeConfig().SupersampleScale;}
    /** 获取当前实例的输出帧率上限。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") int32 GetTargetFrameRate() const {return GetRuntimeConfig().TargetFrameRate;}
    /** 获取当前实例的曝光。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") float GetExposure() const {return GetRuntimeConfig().Exposure;}
    /** 获取当前实例的透明区域穿透策略。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetClickThroughTransparentPixels() const {return GetRuntimeConfig().bClickThroughTransparentPixels;}
    /** 获取当前实例的输入 Alpha 阈值。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") int32 GetHitAlphaThreshold() const {return GetRuntimeConfig().HitAlphaThreshold;}
    /** 获取当前实例的离开保持时间。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") float GetMenuCloseDelay() const {return GetRuntimeConfig().MenuCloseDelay;}
    /** 获取当前实例的交互进度过渡时间。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") float GetMenuAnimationSeconds() const {return GetRuntimeConfig().MenuAnimationSeconds;}
    /** 获取当前实例的诊断开关。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetWriteDiagnostics() const {return GetRuntimeConfig().bWriteDiagnostics;}
    /** 获取当前实例的半透明捕获开关。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetCaptureTranslucency() const {return GetRuntimeConfig().bCaptureTranslucency;}
    /** 获取当前实例的加法覆盖率重建开关。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") bool GetPreserveAdditiveEffects() const {return GetRuntimeConfig().bPreserveAdditiveEffects;}
    /** 获取当前实例的加法覆盖率强度。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Configuration") float GetAdditiveAlphaStrength() const {return GetRuntimeConfig().AdditiveAlphaStrength;}

    /** 整组应用所有参数；合并到下一 Tick，避免在 UMG 回调内销毁绘制资源。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Configuration") void ApplyRuntimeConfig(const FDesktopPetConfig& Config);
    /** 设置未缩放的逻辑画布大小，UI 布局以此为准。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Window") void SetWindowSize(FIntPoint Size);
    /** 直接设置最终物理显示大小，保留当前缩放倍率并反算逻辑尺寸。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Window") void SetDisplaySize(FIntPoint PhysicalSize);
    /** 均匀缩放人物及 UI，保持逻辑画布布局。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Window") void SetDisplayScale(float Scale);
    /** 获取最终物理显示大小。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Window") FIntPoint GetDisplaySize() const;
    /** 设置桌面物理像素坐标。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Window") void SetWindowPosition(FIntPoint Position);
    /** 获取实际窗口位置，包括拖拽之后的位置。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Window") FIntPoint GetWindowPosition() const;
    /** 切换置顶状态。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Window") void SetAlwaysOnTop(bool Value);
    /** 切换 UE 游戏窗口隐藏与输入禁用；编辑器主窗口不受影响。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Window") void SetHideGameWindow(bool Value);
    /** 启停拖拽许可；关闭时会结束正在进行的拖拽。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Input") void SetDraggable(bool Value);
    /** 可接 IA_Drag 的 Started，或由项目 UMG 按下事件调用。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Input") bool BeginWindowDrag();
    /** 可接 IA_Drag 的 Completed / Canceled，或项目 UMG 抬起事件。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Input") void EndWindowDrag();
    /** 查询原生窗口是否处于拖拽状态。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Input") bool IsWindowDragging() const;
    /** 修改超采样倍率；重建纹理但保持 HWND 和 Widget 实例不变。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Quality") void SetSupersampleScale(int32 Scale);
    /** 修改捕获频率上限，下一 Tick 生效。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Quality") void SetTargetFrameRate(int32 FPS);
    /** 修改颜色曝光。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Quality") void SetExposure(float Value);
    /** 修改额外 Alpha 命中穿透策略。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Input") void SetClickThroughTransparentPixels(bool Value);
    /** 修改命中阈值，视觉透明度不变。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Input") void SetHitAlphaThreshold(int32 Value);
    /** 修改通用交互保持时间和过渡时间。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|UI") void SetInteractionTiming(float CloseDelay,float AnimationSeconds);
    /** 修改诊断开关。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Diagnostics") void SetWriteDiagnostics(bool Value);
    /** 广播项目定义的动作；具体行为由项目绑定。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Events") void DispatchAction(FName Action);
    /** 保存当前透明帧与状态数据。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Diagnostics") void SaveDiagnosticFrame();
    /** 获取当前交互进度，方便 UI 初次接入时同步状态。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|UI") float GetInteractionProgress() const;
    /** 查询已输出帧数量。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Diagnostics") int64 GetPresentedFrameCount() const;
    /** C++ 原生 Windows 集成入口，不将 HWND 暴露给蓝图。 */
    void* GetNativeWindowHandle() const;
    /** 获取当前是否使用引擎最终颜色管线。 */
    UFUNCTION(BlueprintPure,Category="Desktop Pet|Quality") bool GetUseEnginePostProcessing() const {return GetRuntimeConfig().bUseEnginePostProcessing;}
    /** 运行时切换引擎最终颜色/旧版简化颜色，保持窗口和 UI 实例。 */
    UFUNCTION(BlueprintCallable,Category="Desktop Pet|Quality") void SetUseEnginePostProcessing(bool Value);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
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
    TSharedPtr<FDesktopPetRuntime> Runtime;
    /** 悬停不延长 Actor 生命周期；销毁通知在对象失效之前清理事件状态。 */
    UPROPERTY(Transient) TWeakObjectPtr<AActor> HoveredInteractionActor;
    bool bStopping=false;
    /** 根据捕获相机和已显示组件查询最近的有效对象，再应用交互白名单。 */
    AActor* TraceInteractionActor(FVector2D UV) const;
    /** 处理 A→B、Actor→UI 等状态迁移，保证先 Leave 再 Enter。 */
    void UpdateHoveredActor(AActor* Actor);
    /** 被悬停对象销毁时发出最后一次离开，防止悬停状态残留。 */
    UFUNCTION() void HandleHoveredActorDestroyed(AActor* Actor);
    friend struct FDesktopPetRuntime;
};
