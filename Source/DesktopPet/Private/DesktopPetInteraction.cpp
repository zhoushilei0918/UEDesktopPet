#include "DesktopPetActor.h"
#include "DesktopPetRuntime.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SceneView.h"

/** 仅在蓝图查询时计算，不捕获画面，也不改变鼠标命中或窗口状态。 */
FVector2D ADesktopPetActor::PetGetActorToMouseScreenVector(bool& bOutValid) const
{
    bOutValid = false;
    if (!Runtime || !IsValid(Capture) || Runtime->Shell.IsInTray())
    {
        return FVector2D::ZeroVector;
    }

    // 鼠标和原生窗口都使用物理像素，避免系统缩放、多屏 DPI 或平滑缩放造成偏移。
    FPetDpiScope DpiScope;
    POINT MousePosition{};
    RECT ClientRect{};
    if (!GetCursorPos(&MousePosition) || !ScreenToClient(Runtime->Window, &MousePosition) ||
        !GetClientRect(Runtime->Window, &ClientRect))
    {
        return FVector2D::ZeroVector;
    }
    const FIntPoint DisplaySize(ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top);
    const FIntPoint CanvasSize = PetGetRuntimeConfig().WindowSize;
    if (DisplaySize.X <= 0 || DisplaySize.Y <= 0 || CanvasSize.X <= 0 || CanvasSize.Y <= 0)
    {
        return FVector2D::ZeroVector;
    }

    FMinimalViewInfo ViewInfo;
    Capture->GetCameraView(0.f, ViewInfo);
    TOptional<FMatrix> CustomProjection;
    if (Capture->bUseCustomProjectionMatrix)
    {
        CustomProjection = Capture->CustomProjectionMatrix;
    }
    else
    {
        // 与 ViewExtension 的取景修正一致；RenderTarget 的超采样和整数取整不改变逻辑宽高比。
        ViewInfo.AspectRatio = float(CanvasSize.X) / CanvasSize.Y;
        // SceneCapture 的正交渲染使用原始 OrthoWidth，Overscan 仅参与透视 FOV。
        ViewInfo.OrthoWidth = Capture->OrthoWidth;
    }
    FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
    UGameplayStatics::CalculateViewProjectionMatricesFromMinimalView(
        ViewInfo, CustomProjection, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);
    const FVector WorldPosition = GetActorLocation();
    if (WorldPosition.ContainsNaN() || ViewProjectionMatrix.ContainsNaN())
    {
        return FVector2D::ZeroVector;
    }
    const FVector4 ClipPosition = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPosition, 1.));
    if (!FMath::IsFinite(ClipPosition.W) || ClipPosition.W <= UE_DOUBLE_SMALL_NUMBER)
    {
        return FVector2D::ZeroVector;
    }

    FVector2D ActorPixel;
    if (!FSceneView::ProjectWorldToScreen(
            WorldPosition, FIntRect(FIntPoint::ZeroValue, DisplaySize), ViewProjectionMatrix, ActorPixel))
    {
        return FVector2D::ZeroVector;
    }
    const FVector2D Result = FVector2D(MousePosition.x, MousePosition.y) - ActorPixel;
    if (Result.ContainsNaN())
    {
        return FVector2D::ZeroVector;
    }
    bOutValid = true;
    return Result;
}

/** 查询与原生悬停使用同一条 UI 优先、Alpha、相机反投影和 Actor 白名单路径。 */
AActor* ADesktopPetActor::PetGetInteractionActorAtPixel(FVector2D Position) const
{
    return Runtime ? Runtime->PickActor(Position) : nullptr;
}

/** 可见名单就是默认交互名单；不要求角色创建 Physics Asset 或配置碰撞通道。 */
TArray<AActor*> ADesktopPetActor::PetGetInteractionActors() const
{
    TArray<AActor*> Result;
    if (!Capture)
    {
        return Result;
    }
    for (const auto& Weak : Capture->ShowOnlyComponents)
    {
        UPrimitiveComponent* C = Weak.Get();
        if (IsValid(C) && C->IsRegistered() && C->IsVisible() && !C->bHiddenInGame && !C->bHiddenInSceneCapture &&
            !Cast<UShapeComponent>(C))
        {
            if (AActor* A = C->GetOwner(); IsValid(A) && !A->IsHidden())
            {
                Result.AddUnique(A);
            }
        }
    }
    return Result;
}
AActor* ADesktopPetActor::PetGetHoveredActor() const
{
    return HoveredInteractionActor.Get();
}

/** 先由最终透明覆盖率拒绝空白，再在可见组件包围盒中寻找最近对象。 多 Actor 重叠时这是自动包围盒拾取，并非逐三角形 ID 缓冲；不会修改组件碰撞。 */
AActor* ADesktopPetActor::TraceInteractionActor(FVector2D UV) const
{
    const TArray<AActor*> Actors = PetGetInteractionActors();
    if (Actors.IsEmpty() || !GetWorld())
    {
        return nullptr;
    }
    if (Actors.Num() == 1)
    {
        return Actors[0];
    }
    if (!Capture->bUseCustomProjectionMatrix && Capture->TextureTarget)
    {
        const FIntPoint Canvas = PetGetRuntimeConfig().WindowSize;
        const double RenderAspect = double(Capture->TextureTarget->SizeX) / Capture->TextureTarget->SizeY;
        UV.Y = .5 + (UV.Y - .5) * RenderAspect / (double(Canvas.X) / Canvas.Y);
    }
    FVector Start, Direction;
    if (!UGameplayStatics::DeprojectSceneCaptureComponentToWorld(Capture, UV, Start, Direction))
    {
        return nullptr;
    }
    const FVector End = Start + Direction * 10000000.f;
    double Nearest = TNumericLimits<double>::Max();
    AActor* Hit = nullptr;
    for (const auto& Weak : Capture->ShowOnlyComponents)
    {
        UPrimitiveComponent* C = Weak.Get();
        if (!IsValid(C) || !Actors.Contains(C->GetOwner()) || !C->IsVisible() || C->bHiddenInGame ||
            C->bHiddenInSceneCapture || Cast<UShapeComponent>(C))
        {
            continue;
        }
        FVector Location, Normal;
        float Time = 0;
        if (FMath::LineExtentBoxIntersection(
                C->Bounds.GetBox(), Start, End, FVector::ZeroVector, Location, Normal, Time) &&
            Time < Nearest)
        {
            Nearest = Time;
            Hit = C->GetOwner();
        }
    }
    return Hit;
}

/** 先清理状态再通知项目，允许蓝图在回调中销毁对象或关闭桌宠。 */
void ADesktopPetActor::UpdateHoveredActor(AActor* NewActor)
{
    AActor* Previous = HoveredInteractionActor.Get();
    if (Previous == NewActor)
    {
        return;
    }
    HoveredInteractionActor.Reset();
    if (Previous)
    {
        Previous->OnDestroyed.RemoveDynamic(this, &ADesktopPetActor::HandleHoveredActorDestroyed);
        PetActorMouseLeave.Broadcast(Previous);
        PetEventActorMouseLeave(Previous);
    }
    if (!HoveredInteractionActor.IsValid() && !bStopping && !bConfigWidgetOpen && IsValid(NewActor) &&
        PetGetInteractionActors().Contains(NewActor))
    {
        HoveredInteractionActor = NewActor;
        NewActor->OnDestroyed.AddUniqueDynamic(this, &ADesktopPetActor::HandleHoveredActorDestroyed);
        PetActorMouseEnter.Broadcast(NewActor);
        PetEventActorMouseEnter(NewActor);
    }
    if ((Previous != nullptr) != HoveredInteractionActor.IsValid())
    {
        PetHoverChanged.Broadcast(HoveredInteractionActor.IsValid());
    }
}
void ADesktopPetActor::HandleHoveredActorDestroyed(AActor* Actor)
{
    HoveredInteractionActor.Reset();
    PetActorMouseLeave.Broadcast(Actor);
    PetEventActorMouseLeave(Actor);
    PetHoverChanged.Broadcast(false);
}
