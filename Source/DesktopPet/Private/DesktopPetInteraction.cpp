#include "DesktopPetActor.h"
#include "DesktopPetRuntime.h"
#include "Components/PrimitiveComponent.h"
#include "Components/ShapeComponent.h"
#include "Kismet/GameplayStatics.h"
#include "SceneView.h"

namespace
{
    /** 两种鼠标查询共用桌宠实际窗口和相机，避免屏幕投影与世界坐标求交采用不同取景。 */
    struct FPetMouseProjection
    {
        FVector2D MousePixel;
        FIntPoint DisplaySize;
        FMinimalViewInfo ViewInfo;
        FMatrix ViewMatrix;
        FMatrix ProjectionMatrix;
        FMatrix ViewProjectionMatrix;

        bool Initialize(HWND InWindow, USceneCaptureComponent2D* InCapture, FIntPoint InCanvasSize)
        {
            // 鼠标和原生窗口都使用物理像素，避免系统缩放、多屏 DPI 或平滑缩放造成偏移。
            FPetDpiScope DpiScope;
            POINT MousePosition{};
            RECT ClientRect{};
            if (!GetCursorPos(&MousePosition) || !ScreenToClient(InWindow, &MousePosition) ||
                !GetClientRect(InWindow, &ClientRect))
            {
                return false;
            }
            MousePixel = FVector2D(MousePosition.x, MousePosition.y);
            DisplaySize = FIntPoint(ClientRect.right - ClientRect.left, ClientRect.bottom - ClientRect.top);
            if (DisplaySize.X <= 0 || DisplaySize.Y <= 0 || InCanvasSize.X <= 0 || InCanvasSize.Y <= 0)
            {
                return false;
            }

            InCapture->GetCameraView(0.f, ViewInfo);
            TOptional<FMatrix> CustomProjection;
            if (InCapture->bUseCustomProjectionMatrix)
            {
                CustomProjection = InCapture->CustomProjectionMatrix;
            }
            else
            {
                // 与 ViewExtension 一致：超采样和整数取整不改变逻辑宽高比。
                ViewInfo.AspectRatio = float(InCanvasSize.X) / InCanvasSize.Y;
                // SceneCapture 的正交渲染使用原始 OrthoWidth，Overscan 仅参与透视 FOV。
                ViewInfo.OrthoWidth = InCapture->OrthoWidth;
            }
            UGameplayStatics::CalculateViewProjectionMatricesFromMinimalView(
                ViewInfo, CustomProjection, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);
            return true;
        }
    };
} // namespace

/** 仅在蓝图查询时计算，不捕获画面，也不改变鼠标命中或窗口状态。 */
FVector2D ADesktopPetActor::PetGetActorToMouseScreenVector(bool& bOutValid) const
{
    bOutValid = false;
    if (!Runtime || !IsValid(Capture) || Runtime->Shell.IsInTray())
    {
        return FVector2D::ZeroVector;
    }

    FPetMouseProjection Projection;
    if (!Projection.Initialize(Runtime->Window, Capture, PetGetRuntimeConfig().WindowSize))
    {
        return FVector2D::ZeroVector;
    }
    const FVector WorldPosition = PetMouseScreenVectorOrigin;
    if (WorldPosition.ContainsNaN() || Projection.ViewProjectionMatrix.ContainsNaN())
    {
        return FVector2D::ZeroVector;
    }
    const FVector4 ClipPosition = Projection.ViewProjectionMatrix.TransformFVector4(FVector4(WorldPosition, 1.));
    if (!FMath::IsFinite(ClipPosition.W) || ClipPosition.W <= UE_DOUBLE_SMALL_NUMBER)
    {
        return FVector2D::ZeroVector;
    }

    FVector2D OriginPixel;
    if (!FSceneView::ProjectWorldToScreen(WorldPosition,
                                          FIntRect(FIntPoint::ZeroValue, Projection.DisplaySize),
                                          Projection.ViewProjectionMatrix,
                                          OriginPixel))
    {
        return FVector2D::ZeroVector;
    }
    const FVector2D Result = Projection.MousePixel - OriginPixel;
    if (Result.ContainsNaN())
    {
        return FVector2D::ZeroVector;
    }
    bOutValid = true;
    return Result;
}

/** 在相机相对空间求交，避免大世界坐标先乘完整视图投影矩阵造成精度损失。 */
FVector ADesktopPetActor::PetGetMouseWorldPositionOnPlane(bool& bOutValid) const
{
    bOutValid = false;
    if (!Runtime || !IsValid(Capture) || Runtime->Shell.IsInTray() || PetMouseScreenVectorOrigin.ContainsNaN())
    {
        return FVector::ZeroVector;
    }

    FPetMouseProjection Projection;
    if (!Projection.Initialize(Runtime->Window, Capture, PetGetRuntimeConfig().WindowSize) ||
        Projection.ViewMatrix.ContainsNaN() || Projection.ProjectionMatrix.ContainsNaN())
    {
        return FVector::ZeroVector;
    }
    const FMatrix& Matrix = Projection.ProjectionMatrix;
    const double ProjectionDeterminant = Matrix.Determinant();
    if (!FMath::IsFinite(ProjectionDeterminant) || ProjectionDeterminant == 0.)
    {
        return FVector::ZeroVector;
    }

    // 正对相机的平面在视图空间中为 Z = 基准点深度；先减相机位置再旋转。
    const FVector OriginView =
        Projection.ViewMatrix.TransformVector(PetMouseScreenVectorOrigin - Projection.ViewInfo.Location);
    if (OriginView.ContainsNaN() || (Projection.ViewInfo.ProjectionMode == ECameraProjectionMode::Perspective &&
                                     OriginView.Z <= UE_DOUBLE_SMALL_NUMBER))
    {
        return FVector::ZeroVector;
    }
    const double ScreenX = 2. * Projection.MousePixel.X / Projection.DisplaySize.X - 1.;
    const double ScreenY = 1. - 2. * Projection.MousePixel.Y / Projection.DisplaySize.Y;

    // 固定 Z 后，以 ClipX = ScreenX * ClipW、ClipY = ScreenY * ClipW 联立求 X/Y。
    // 这等价于鼠标射线与平面求交，且不需要选择远裁剪点或将像素转为 float。
    const double A = Matrix.M[0][0] - ScreenX * Matrix.M[0][3];
    const double B = Matrix.M[1][0] - ScreenX * Matrix.M[1][3];
    const double C = Matrix.M[0][1] - ScreenY * Matrix.M[0][3];
    const double D = Matrix.M[1][1] - ScreenY * Matrix.M[1][3];
    const double PlaneW = OriginView.Z * Matrix.M[2][3] + Matrix.M[3][3];
    const double RightX = ScreenX * PlaneW - OriginView.Z * Matrix.M[2][0] - Matrix.M[3][0];
    const double RightY = ScreenY * PlaneW - OriginView.Z * Matrix.M[2][1] - Matrix.M[3][1];
    const double Determinant = A * D - B * C;
    const double CoefficientScale =
        FMath::Max(FMath::Max(FMath::Abs(A), FMath::Abs(B)), FMath::Max(FMath::Abs(C), FMath::Abs(D)));
    if (!FMath::IsFinite(Determinant) || FMath::Abs(Determinant) <= 1.e-12 * CoefficientScale * CoefficientScale)
    {
        // 平行或接近平行时没有稳定的唯一交点，不返回异常的大坐标。
        return FVector::ZeroVector;
    }

    const FVector HitView(
        (RightX * D - B * RightY) / Determinant, (A * RightY - RightX * C) / Determinant, OriginView.Z);
    const FVector4 HitClip = Matrix.TransformFVector4(FVector4(HitView, 1.));
    if (HitView.ContainsNaN() || !FMath::IsFinite(HitClip.W) || HitClip.W <= 0.)
    {
        return FVector::ZeroVector;
    }

    // 反向旋转相机相对交点，再加回相机世界位置；基准点横向很远时也不引入额外的大数相消。
    const FVector HitRelativeToCamera = Projection.ViewMatrix.GetTransposed().TransformVector(HitView);
    const FVector Result = Projection.ViewInfo.Location + HitRelativeToCamera;
    if (Result.ContainsNaN())
    {
        return FVector::ZeroVector;
    }
    bOutValid = true;
    return Result;
}

/** 复用原有屏幕投影，直接给出无单位的鼠标距离比例，保持圆形范围的横纵尺度一致。 */
double ADesktopPetActor::PetGetActorToMouseScreenDistanceRatio(bool& bOutValid) const
{
    bOutValid = false;
    if (!Runtime || !IsWindow(Runtime->Window))
    {
        return 0.;
    }

    bool bScreenVectorValid = false;
    const FVector2D ScreenVector = PetGetActorToMouseScreenVector(bScreenVectorValid);
    if (!bScreenVectorValid)
    {
        return 0.;
    }

    FPetDpiScope DpiScope;
    const HMONITOR Monitor = MonitorFromWindow(Runtime->Window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO MonitorInfo{};
    MonitorInfo.cbSize = sizeof(MonitorInfo);
    if (!Monitor || !GetMonitorInfo(Monitor, &MonitorInfo))
    {
        return 0.;
    }

    // 与原屏幕向量统一使用物理像素；取完整显示器尺寸，不受任务栏和副屏负坐标影响。
    const LONG ScreenWidth = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;
    const LONG ScreenHeight = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;
    if (ScreenWidth <= 0 || ScreenHeight <= 0)
    {
        return 0.;
    }

    const double ScreenDiagonal = FVector2D(ScreenWidth, ScreenHeight).Size();
    const double DistanceRatio = ScreenVector.Size() / ScreenDiagonal;
    if (!FMath::IsFinite(DistanceRatio))
    {
        return 0.;
    }

    bOutValid = true;
    return FMath::Clamp(DistanceRatio, 0., 1.);
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
