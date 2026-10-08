#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
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
    /** 可选项目 Widget 类；也可通过 SetOverlayWidget 传入已经创建的实例。 */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Desktop Pet|UI") TSubclassOf<UUserWidget> WidgetOverride;
    /** 鼠标是否进入场景有效 Alpha 区域。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetHover OnPetHoverChanged;
    /** 通用用户动作总线；插件不解释 Chat、Quit 等项目业务名称。 */
    UPROPERTY(BlueprintAssignable,Category="Desktop Pet|Events") FDesktopPetAction OnAction;
    /** 场景区域左键点击通知。 */
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
    /** C++ 原生集成入口；项目自测据此验证 Windows 命中，不暴露 HWND 给蓝图。 */
    void* GetNativeWindowHandle() const;
    /** 诊断读取最终场景 Alpha，不含 UMG。 */
    uint8 GetSceneAlphaAt(FIntPoint Pixel) const;
    /** 诊断读取实际交给 Windows 的最终 Alpha，包含 UMG，适合验证透明点。 */
    uint8 GetCompositedAlphaAt(FIntPoint Pixel) const;
    /** 项目自动测试使用的指针覆盖；Shipping 构建忽略它。 */
    void DebugSetPointerOverride(TOptional<FVector2D> Position);
    virtual void Tick(float DeltaSeconds) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    /** GPU 场景捕获目标，存储 HDR RGB 和反透明度。 */
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> SceneTarget;
    /** UMG 单独捕获，避免场景筛选剔除 UI。 */
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> UITarget;
    /** 项目交给插件显示的实例；插件不强制其 Widget 类。 */
    UPROPERTY(Transient) TObjectPtr<UUserWidget> OverlayWidget;
    FDesktopPetConfig DesiredConfig;
    bool bHasConfig=false,bConfigPending=false,bWidgetPending=false,bRestartRequested=false;
    TSharedPtr<FDesktopPetRuntime> Runtime;
    friend struct FDesktopPetRuntime;
};
