#include "DesktopPetRuntime.h"
#include "DesktopPetWidgetGeometry.h"

/* 限频捕获、GPU 回读与像素合成；执行顺序保持在原有成员函数中。 */

void FDesktopPetRuntime::Composite(FPetReadback& F)
{
    FPetDpiScope DpiScope;
    if (F.UseGPU)
    {
        // Complete 的原子发布保证渲染线程已停止写入；旧数组交给下一轮读回复用。
        Swap(CapturedPixels, F.OutputPixels);
        Swap(CapturedAlpha, F.OutputAlpha);
    }
    else
    {

        DesktopPetCompositor::Composite(F.OpacityPixels,
                                        F.UIPixels,
                                        Width,
                                        Height,
                                        F.FrameConfig,
                                        CapturedPixels,
                                        CapturedAlpha,
                                        F.FinalColorPixels,
                                        PetScratch);
    }
    CapturedSize = FIntPoint(Width, Height);
    // 新图与缩放共用帧末提交，避免同一 Tick 两次缩放和 UpdateLayeredWindow。
    bFrameNeedsPresentation = true;
}

void FDesktopPetRuntime::CaptureFrame(float Delta)
{
    if (CloseRequested || !Config.bTransparentWindowEnabled)
    {
        return;
    }
    // 按提交顺序消费；同一 Tick 已就绪多帧时只呈现最新一帧，不增加显示延迟。
    TSharedPtr<FPetReadback, ESPMode::ThreadSafe> Latest;
    while (!PendingReadbacks.IsEmpty())
    {
        const auto Frame = PendingReadbacks[0];
        if (!Frame->Failed.Load() && !Frame->Complete.Load())
        {
            break;
        }
        PendingReadbacks.RemoveAt(0, 1, EAllowShrinking::No);
        if (Frame->Failed.Load())
        {
            UE_LOG(LogDesktopPet, Warning, TEXT("GPU readback lock failed; discarding frame"));
            continue;
        }
        if (Latest)
        {
            AvailableReadbacks.Add(Latest);
        }
        Latest = Frame;
    }
    if (Latest)
    {
        if (!Shell.IsInTray())
        {
            Composite(*Latest);
        }
        AvailableReadbacks.Add(Latest);
    }
    for (const auto& Frame : PendingReadbacks)
    {
        if (Frame->Submitted.Load() && !Frame->Copying.Load() &&
            (Frame->UseGPU ?
                 Frame->Packed.IsReady() :
                 ((!Frame->HasUI || Frame->UI.IsReady()) && Frame->FinalColor.IsReady() && Frame->Geometry.IsReady())))
        {
            Frame->Copying.Store(true);
            ENQUEUE_RENDER_COMMAND(DesktopPetReadback)(
                [Frame](FRHICommandListImmediate& Cmd)
                {
                    if (Frame->UseGPU)
                    {
                        const int32 Count = Frame->Width * Frame->Height;
                        const uint32 Bytes = DesktopPetGPUCompositor::GetReadbackBytes(Count);
                        const auto* Data = static_cast<const uint8*>(Frame->Packed.Lock(Bytes));
                        if (!Data)
                        {
                            Frame->Failed.Store(true);
                            return;
                        }
                        Frame->OutputPixels.SetNumUninitialized(Count);
                        Frame->OutputAlpha.SetNumUninitialized(Count);
                        FMemory::Memcpy(Frame->OutputPixels.GetData(), Data, Count * sizeof(FColor));
                        FMemory::Memcpy(Frame->OutputAlpha.GetData(), Data + Count * sizeof(FColor), Count);
                        Frame->Packed.Unlock();
                        Frame->Complete.Store(true);
                        return;
                    }
                    int32 Pitch = 0;
                    {
                        const auto* C = static_cast<const FFloat16Color*>(Frame->FinalColor.Lock(Pitch));
                        if (!C)
                        {
                            Frame->Failed.Store(true);
                            return;
                        }
                        Frame->FinalColorPixels.SetNumUninitialized(Frame->SceneWidth * Frame->SceneHeight);
                        for (int32 Y = 0; Y < Frame->SceneHeight; ++Y)
                        {
                            FMemory::Memcpy(Frame->FinalColorPixels.GetData() + Y * Frame->SceneWidth,
                                            C + Y * Pitch,
                                            Frame->SceneWidth * sizeof(FFloat16Color));
                        }
                        Frame->FinalColor.Unlock();
                    }
                    const auto* G = static_cast<const FFloat16*>(Frame->Geometry.Lock(Pitch));
                    if (!G)
                    {
                        Frame->Failed.Store(true);
                        return;
                    }
                    Frame->OpacityPixels.SetNumUninitialized(Frame->SceneWidth * Frame->SceneHeight);
                    for (int32 Y = 0; Y < Frame->SceneHeight; ++Y)
                    {
                        FMemory::Memcpy(Frame->OpacityPixels.GetData() + Y * Frame->SceneWidth,
                                        G + Y * Pitch,
                                        Frame->SceneWidth * sizeof(FFloat16));
                    }
                    Frame->Geometry.Unlock();
                    if (Frame->HasUI)
                    {
                        const auto* U = static_cast<const FColor*>(Frame->UI.Lock(Pitch));
                        if (!U)
                        {
                            Frame->Failed.Store(true);
                            return;
                        }
                        Frame->UIPixels.SetNumUninitialized(Frame->Width * Frame->Height);
                        for (int32 Y = 0; Y < Frame->Height; ++Y)
                        {
                            FMemory::Memcpy(Frame->UIPixels.GetData() + Y * Frame->Width,
                                            U + Y * Pitch,
                                            Frame->Width * sizeof(FColor));
                        }
                        Frame->UI.Unlock();
                    }
                    else
                    {
                        Frame->UIPixels.Reset();
                    }
                    Frame->Complete.Store(true);
                });
        }
    }
    const double Now = FPlatformTime::Seconds();
    if (Shell.IsInTray() || PendingReadbacks.Num() >= MaxReadbacks || Now < NextCaptureTime)
    {
        return;
    }
    const double Interval = 1.0 / FrameRate;
    const float CaptureDelta = LastCapture > 0. ? float(Now - LastCapture) : float(Interval);
    // 按绝对时间推进目标节拍，不把 Tick 量化的延迟累加到每一帧；慢帧跳过过期节拍，不突发补帧。
    if (NextCaptureTime <= 0.)
    {
        NextCaptureTime = Now;
    }
    NextCaptureTime += (FMath::FloorToDouble((Now - NextCaptureTime) / Interval) + 1.) * Interval;
    LastCapture = Now;
    FDesktopPetMemory::Update(Owner, Config);
    Owner->SyncOpacityCapture();
    Owner->OpacityCapture->CaptureScene();
    Owner->Capture->CaptureScene();
    if (HasUI)
    {
        FPetDpiScope DpiScope;
        POINT Origin{};
        if (ClientToScreen(Window, &Origin))
        {
            UIOrigin = FVector2D(Origin.x, Origin.y);
        }
        UIHitSize = FIntPoint(Width, Height);
        DesktopPetWidgetGeometry::DrawWindow(*Renderer,
                                             Owner->UITarget,
                                             VirtualWindow.ToSharedRef(),
                                             UIOrigin,
                                             FVector2D(UIHitSize),
                                             Config.DisplayScale,
                                             CaptureDelta);
    }
    // 复用 staging 纹理及 CPU 数组，避免每帧分配数十 MB 的读回对象。
    TSharedPtr<FPetReadback, ESPMode::ThreadSafe> Frame;
    if (!AvailableReadbacks.IsEmpty())
    {
        Frame = AvailableReadbacks.Pop(EAllowShrinking::No);
    }
    if (!Frame)
    {
        Frame = MakeShared<FPetReadback, ESPMode::ThreadSafe>();
    }
    Frame->Submitted.Store(false);
    Frame->Copying.Store(false);
    Frame->Complete.Store(false);
    Frame->Failed.Store(false);
    Frame->HasUI = HasUI;
    Frame->UseGPU = DesktopPetGPUCompositor::IsEnabled(Config);
    Frame->FrameConfig = Config;
    PendingReadbacks.Add(Frame);
    Frame->Width = Width;
    Frame->Height = Height;
    Frame->SceneWidth = Width * Scale;
    Frame->SceneHeight = Height * Scale;
    FTextureRenderTargetResource* UR = Owner->UITarget->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* CR = Owner->FinalColorTarget->GameThread_GetRenderTargetResource();
    FTextureRenderTargetResource* GR = Owner->GeometryTarget->GameThread_GetRenderTargetResource();
    ENQUEUE_RENDER_COMMAND(DesktopPetCopy)(
        [Frame, UR, CR, GR](FRHICommandListImmediate& Cmd)
        {
            if (Frame->UseGPU)
            {
                DesktopPetGPUCompositor::Enqueue(Cmd,
                                                 CR->GetRenderTargetTexture(),
                                                 GR->GetRenderTargetTexture(),
                                                 Frame->HasUI ? UR->GetRenderTargetTexture() : nullptr,
                                                 FIntPoint(Frame->Width, Frame->Height),
                                                 Frame->FrameConfig,
                                                 Frame->Packed);
                Frame->Submitted.Store(true);
                return;
            }
            if (Frame->HasUI)
            {
                FRHITexture* UT = UR->GetRenderTargetTexture();
                Cmd.Transition(FRHITransitionInfo(UT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
                Frame->UI.EnqueueCopy(Cmd, UT);
                Cmd.Transition(FRHITransitionInfo(UT, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
            }
            {
                FRHITexture* CT = CR->GetRenderTargetTexture();
                Cmd.Transition(FRHITransitionInfo(CT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
                Frame->FinalColor.EnqueueCopy(Cmd, CT);
                Cmd.Transition(FRHITransitionInfo(CT, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
            }
            FRHITexture* GT = GR->GetRenderTargetTexture();
            Cmd.Transition(FRHITransitionInfo(GT, ERHIAccess::Unknown, ERHIAccess::CopySrc));
            Frame->Geometry.EnqueueCopy(Cmd, GT);
            Cmd.Transition(FRHITransitionInfo(GT, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
            Frame->Submitted.Store(true);
        });
}
