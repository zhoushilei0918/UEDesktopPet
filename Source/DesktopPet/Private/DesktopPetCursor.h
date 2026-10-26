#pragma once

#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"

class UWorld;
class FWidgetPath;
struct FPointerEvent;

/** 游戏线程上的只读光标像素视图；指向已完成异步回读的缓存，不能跨 Tick 持有。 */
struct FDesktopPetCursorFrame
{
    const TArray<FColor>* Pixels = nullptr;
    FIntPoint Size = FIntPoint::ZeroValue;
    FIntPoint HotSpot = FIntPoint::ZeroValue;
    uint64 Revision = 0;
};

/** 仅接管桌宠 HWND 下的光标；独立无输入图层保留越界绘制，不改 Slate 全局光标映射。 */
class FDesktopPetCursor
{
public:
    FDesktopPetCursor();
    ~FDesktopPetCursor();

    /**
     * 查询 UMG 命中路径并应用当前项目的光标设置。
     * Window - 本实例的桌宠原生窗口；
     * World - 提供游戏视口与 PlayerController 的世界；
     * Path - 当前指针位置对应的 Slate 命中路径；
     * Event - 查询光标时传给 Widget 的指针数据；
     * FrameRate - 软件光标图像刷新的目标频率。
     */
    void Update(HWND Window, UWorld* World, const FWidgetPath& Path, const FPointerEvent& Event, int32 FrameRate);
    /** 切换 UMG 光标输出；原生硬件光标不受此设置影响。 */
    void SetCompositeSoftwareCursor(bool bEnabled);
    /** 独立图层不可用时提供主窗口合成回退；正常情况由独立图层完整显示光标。 */
    bool GetCompositeFrame(FDesktopPetCursorFrame& Out) const;
    /** 响应 WM_SETCURSOR，只使用已经准备好的资源，不在窗口消息中加载资产。 */
    bool Apply(HWND Window);
    /** 有动画的软件光标使用 Slate 的真实时钟，避免离屏刷新限帧导致慢放。 */
    void TickAnimations();
    /** 移出、透明穿透、收起和停止时交还光标，不触碰 ShowCursor 的全局计数。 */
    void Release();

private:
    struct FState;
    TUniquePtr<FState> State;
};
