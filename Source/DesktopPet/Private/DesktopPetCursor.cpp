#include "DesktopPetCursor.h"
#include "DesktopPetCursorLayout.h"
#include "DesktopPetCursorOverlay.h"

#include "Engine/GameViewportClient.h"
#include "Engine/UserInterfaceSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/CursorReply.h"
#include "Input/Events.h"
#include "Layout/WidgetPath.h"
#include "Slate/WidgetRenderer.h"
#include "Slate/SObjectWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Animation/UMGSequenceTickManager.h"
#include "Widgets/SWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "RHIGPUReadback.h"
#include "RenderingThread.h"
#include "TextureResource.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDesktopPetCursor, Log, All);

namespace
{
    TAutoConsoleVariable<float> CVarPetCursorOverflowPadding(
        TEXT("DesktopPet.CursorOverflowPadding"), 64.f,
        TEXT("Extra pixels around measured cursor widget bounds for custom OnPaint effects (0-1024)."));
    /** WM_SETCURSOR 和 Slate PostTick 的线程 DPI 上下文可能不同，原生判定始终用物理坐标。 */
    struct FPetCursorDpiScope
    {
        DPI_AWARENESS_CONTEXT Previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~FPetCursorDpiScope()
        {
            if (Previous)
            {
                SetThreadDpiAwarenessContext(Previous);
            }
        }
    };
    /** WS_EX_TRANSPARENT、遮挡、隐藏及窗口外均不属于桌宠，拖拽也不在窗口外接管光标。 */
    bool PetOwnsCursor(HWND Window)
    {
        FPetCursorDpiScope DpiScope;
        if (!Window || !IsWindowVisible(Window) || (GetWindowLongPtr(Window, GWL_EXSTYLE) & WS_EX_TRANSPARENT))
        {
            return false;
        }
        POINT Point{};
        RECT Rect{};
        if (!GetCursorPos(&Point) || !GetWindowRect(Window, &Rect) || !PtInRect(&Rect, Point))
        {
            return false;
        }
        return GetCapture() == Window || WindowFromPoint(Point) == Window;
    }

    /** Windows 的共享系统光标不释放；自建及文件光标另行持有和释放。 */
    HCURSOR PetSystemCursor(EMouseCursor::Type Type)
    {
        switch (Type)
        {
            case EMouseCursor::None:
            case EMouseCursor::Custom:
            {
                return nullptr;
            }
            case EMouseCursor::TextEditBeam:
            {
                return LoadCursor(nullptr, IDC_IBEAM);
            }
            case EMouseCursor::ResizeLeftRight:
            {
                return LoadCursor(nullptr, IDC_SIZEWE);
            }
            case EMouseCursor::ResizeUpDown:
            {
                return LoadCursor(nullptr, IDC_SIZENS);
            }
            case EMouseCursor::ResizeSouthEast:
            {
                return LoadCursor(nullptr, IDC_SIZENWSE);
            }
            case EMouseCursor::ResizeSouthWest:
            {
                return LoadCursor(nullptr, IDC_SIZENESW);
            }
            case EMouseCursor::CardinalCross:
            {
                return LoadCursor(nullptr, IDC_SIZEALL);
            }
            case EMouseCursor::Crosshairs:
            {
                return LoadCursor(nullptr, IDC_CROSS);
            }
            case EMouseCursor::Hand:
            case EMouseCursor::GrabHand:
            case EMouseCursor::GrabHandClosed:
            {
                return LoadCursor(nullptr, IDC_HAND);
            }
            case EMouseCursor::SlashedCircle:
            {
                return LoadCursor(nullptr, IDC_NO);
            }
            case EMouseCursor::EyeDropper:
            {
                return LoadCursor(nullptr, IDC_CROSS);
            }
            default:
            {
                return LoadCursor(nullptr, IDC_ARROW);
            }
        }
    }

    /** 输入为预乘 Alpha 的 BGRA，与 Slate 离屏输出一致；热点限制在有效像素内。 */
    HCURSOR PetCursorFromPixels(const TArray<FColor>& Pixels, FIntPoint Size, FVector2D HotSpot)
    {
        if (Size.X < 1 || Size.Y < 1 || Pixels.Num() != Size.X * Size.Y)
        {
            return nullptr;
        }
        BITMAPINFO Info{};
        Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Info.bmiHeader.biWidth = Size.X;
        Info.bmiHeader.biHeight = -Size.Y;
        Info.bmiHeader.biPlanes = 1;
        Info.bmiHeader.biBitCount = 32;
        Info.bmiHeader.biCompression = BI_RGB;
        void* Bits = nullptr;
        HBITMAP Color = CreateDIBSection(nullptr, &Info, DIB_RGB_COLORS, &Bits, nullptr, 0);
        const int32 MaskStride = ((Size.X + 15) / 16) * 2;
        TArray<uint8> MaskBytes;
        MaskBytes.SetNumZeroed(MaskStride * Size.Y);
        HBITMAP Mask = CreateBitmap(Size.X, Size.Y, 1, 1, MaskBytes.GetData());
        HCURSOR Result = nullptr;
        if (Color && Bits && Mask)
        {
            FMemory::Memcpy(Bits, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
            ICONINFO Icon{};
            Icon.fIcon = 0;
            Icon.hbmColor = Color;
            Icon.hbmMask = Mask;
            Icon.xHotspot = FMath::Clamp(FMath::RoundToInt(HotSpot.X * Size.X), 0, Size.X - 1);
            Icon.yHotspot = FMath::Clamp(FMath::RoundToInt(HotSpot.Y * Size.Y), 0, Size.Y - 1);
            Result = CreateIconIndirect(&Icon);
        }
        if (Color)
        {
            DeleteObject(Color);
        }
        if (Mask)
        {
            DeleteObject(Mask);
        }
        return Result;
    }

    /** 沿用 UE 的无扩展名路径、ANI/CUR 优先级，以及 @1.5x 等 PNG 倍率约定。 */
    HCURSOR PetLoadHardwareCursor(const FHardwareCursorReference& Ref, float DpiScale)
    {
        const FString Path = FPaths::ProjectContentDir() / Ref.CursorPath.ToString();
        const auto Platform = FSlateApplication::Get().GetPlatformCursor();
        if (Platform)
        {
            if (void* Handle = Platform->CreateCursorFromFile(Path, Ref.HotSpot))
            {
                return static_cast<HCURSOR>(Handle);
            }
        }
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Path + TEXT("*.png")), true, false);
        Files.Sort();
        FString Best;
        float BestDelta = MAX_flt;
        const FString Base = FPaths::GetCleanFilename(Path);
        for (const FString& File : Files)
        {
            const FString Stem = FPaths::GetBaseFilename(File);
            float Scale = 1.f;
            if (Stem != Base)
            {
                if (!Stem.StartsWith(Base + TEXT("@")))
                {
                    continue;
                }
                FString Suffix = Stem.Mid(Base.Len() + 1);
                Suffix.RemoveFromEnd(TEXT("x"));
                if (!Suffix.IsNumeric())
                {
                    continue;
                }
                Scale = FCString::Atof(*Suffix);
                if (Scale <= 0.f)
                {
                    continue;
                }
            }
            const float Delta = FMath::Abs(Scale - DpiScale);
            if (Delta < BestDelta)
            {
                BestDelta = Delta;
                Best = FPaths::GetPath(Path) / File;
            }
        }
        TArray<uint8> Data;
        if (Best.IsEmpty() || !FFileHelper::LoadFileToArray(Data, *Best))
        {
            return nullptr;
        }
        auto& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
        const auto Image = Images.CreateImageWrapper(EImageFormat::PNG);
        TArray64<uint8> Raw;
        if (!Image || !Image->SetCompressed(Data.GetData(), Data.Num()) || !Image->GetRaw(ERGBFormat::BGRA, 8, Raw))
        {
            return nullptr;
        }
        const FIntPoint Size(Image->GetWidth(), Image->GetHeight());
        if (Size.X < 1 || Size.Y < 1 || int64(Size.X) * Size.Y * 4 != Raw.Num())
        {
            return nullptr;
        }
        TArray<FColor> Pixels;
        Pixels.SetNumUninitialized(Size.X * Size.Y);
        FMemory::Memcpy(Pixels.GetData(), Raw.GetData(), Raw.Num());
        // PNG 是直通 Alpha，Windows 光标需要预乘；只转换这里，不重复转换 Slate 输出。
        for (FColor& P : Pixels)
        {
            P.R = (uint32(P.R) * P.A + 127) / 255;
            P.G = (uint32(P.G) * P.A + 127) / 255;
            P.B = (uint32(P.B) * P.A + 127) / 255;
        }
        return PetCursorFromPixels(Pixels, Size, Ref.HotSpot);
    }

    /** 小尺寸光标异步回读，与桌宠场景捕获和主 UI 合成完全独立。 */
    struct FPetCursorReadback
    {
        FRHIGPUTextureReadback GPU{TEXT("DesktopPetCursor")};
        TAtomic<bool> Submitted{false}, Copying{false}, Complete{false};
        FIntPoint Size = FIntPoint::ZeroValue;
        FIntPoint HotSpot = FIntPoint::ZeroValue;
        uint64 Generation = 0;
        TArray<FColor> Pixels;
    };
} // namespace

struct FDesktopPetCursor::FState
{
    struct FHardwareEntry
    {
        FName Path;
        FVector2D HotSpot = FVector2D::ZeroVector;
        UINT Dpi = 0;
        HCURSOR Handle = nullptr;
    };
    HWND Window = nullptr;
    bool Owned = false;
    bool CompositeSoftware = true;
    uint64 PixelRevision = 0;
    /** 原生句柄对应的像素版本；新帧就绪后再替换，避免先销毁旧光标造成闪切。 */
    uint64 SoftwareRevision = 0;
    HCURSOR Selected = LoadCursor(nullptr, IDC_ARROW), Software = nullptr;
    TMap<EMouseCursor::Type, FHardwareEntry> Hardware;
    TSharedPtr<SWidget> Widget;
    TSharedPtr<SVirtualWindow> RenderWindow;
    FSlateRect RetainedBounds;
    FDesktopPetCursorOverlay Overlay;
    TUniquePtr<FWidgetRenderer> Renderer;
    TStrongObjectPtr<UTextureRenderTarget2D> Target;
    TSharedPtr<FPetCursorReadback, ESPMode::ThreadSafe> Pending, Reusable;
    TArray<FColor> LastPixels;
    FIntPoint LastSize = FIntPoint::ZeroValue;
    FIntPoint LastHotSpot = FIntPoint::ZeroValue;
    uint64 Generation = 0;
    double LastRender = 0.;

    /** 删除前先解除 Win32 对旧句柄的使用，避免留下失效的当前光标。 */
    void DestroyOwnedCursor(HCURSOR& Handle)
    {
        if (!Handle)
        {
            return;
        }
        if (GetCursor() == Handle)
        {
            SetCursor(LoadCursor(nullptr, IDC_ARROW));
        }
        if (Selected == Handle)
        {
            Selected = LoadCursor(nullptr, IDC_ARROW);
        }
        DestroyCursor(Handle);
        Handle = nullptr;
    }

    HCURSOR GetHardware(EMouseCursor::Type Type)
    {
        const auto* Ref = GetDefault<UUserInterfaceSettings>()->HardwareCursors.Find(Type);
        if (!Ref || Ref->CursorPath.IsNone())
        {
            return PetSystemCursor(Type);
        }
        const UINT Dpi = GetDpiForWindow(Window);
        FHardwareEntry& Entry = Hardware.FindOrAdd(Type);
        if (Entry.Path != Ref->CursorPath || Entry.HotSpot != Ref->HotSpot || Entry.Dpi != Dpi)
        {
            DestroyOwnedCursor(Entry.Handle);
            Entry.Path = Ref->CursorPath;
            Entry.HotSpot = Ref->HotSpot;
            Entry.Dpi = Dpi;
            Entry.Handle = PetLoadHardwareCursor(*Ref, float(Dpi ? Dpi : 96) / 96.f);
            if (!Entry.Handle)
            {
                UE_LOG(LogDesktopPetCursor,
                       Warning,
                       TEXT("无法加载项目硬件光标 %s；回退到系统光标。请检查路径及打包文件。"),
                       *Entry.Path.ToString());
            }
        }
        return Entry.Handle ? Entry.Handle : PetSystemCursor(Type);
    }

    void SetWidget(TSharedPtr<SWidget> NewWidget)
    {
        if (NewWidget == Widget)
        {
            return;
        }
        RenderWindow.Reset();
        Widget = NewWidget;
        RetainedBounds = FSlateRect();
        ++Generation;
        LastRender = 0.;
        // Default、Hand 等类型即使使用同一 UMG 类，也可能是不同 Slate 实例。
        // 新实例的 GPU 回读尚未完成时继续显示上一张有效光标，不能在这里清空后回退系统箭头。
        // Generation 仍会变化，旧实例在途回读不得覆盖当前选择。
        if (!Widget)
        {
            ClearSoftwarePixels();
        }
    }

    /** 仅在无软件光标或控件尺寸无效时清除画面；句柄在选好替代项后再释放。 */
    void ClearSoftwarePixels()
    {
        if (LastPixels.IsEmpty())
        {
            return;
        }
        LastPixels.Reset();
        LastSize = FIntPoint::ZeroValue;
        LastHotSpot = FIntPoint::ZeroValue;
        ++PixelRevision;
    }

    /** 原生显示路径先建立新句柄并直接切换，再释放旧句柄，不经过系统箭头。 */
    void RefreshSoftwareHandle()
    {
        if (Software && SoftwareRevision == PixelRevision)
        {
            return;
        }
        HCURSOR Replacement = PetCursorFromPixels(
            LastPixels, LastSize, FVector2D(double(LastHotSpot.X) / LastSize.X, double(LastHotSpot.Y) / LastSize.Y));
        if (!Replacement)
        {
            return; // 创建失败仍保留有效旧句柄，下次更新重试。
        }
        HCURSOR Previous = Software;
        Software = Replacement;
        SoftwareRevision = PixelRevision;
        if (Previous)
        {
            if (GetCursor() == Previous)
            {
                SetCursor(Replacement);
            }
            if (Selected == Previous)
            {
                Selected = Replacement;
            }
            DestroyCursor(Previous);
        }
    }

    /** 按桌宠帧率更新光标外观；窗口合成路径独立更新位置，不增加任何场景捕获。 */
    void UpdateSoftware(int32 FrameRate)
    {
        if (Pending)
        {
            if (Pending->Complete.Load())
            {
                if (Pending->Generation == Generation && Widget && Pending->Pixels.Num() > 0)
                {
                    const bool Changed = LastSize != Pending->Size || LastHotSpot != Pending->HotSpot ||
                                         LastPixels.Num() != Pending->Pixels.Num() ||
                                         FMemory::Memcmp(LastPixels.GetData(),
                                                         Pending->Pixels.GetData(),
                                                         Pending->Pixels.Num() * sizeof(FColor)) != 0;
                    if (Changed)
                    {
                        // 两种显示方式共用同一份 Slate 像素；统一合成时无需反复创建 Win32 光标句柄。
                        LastPixels = Pending->Pixels;
                        LastSize = Pending->Size;
                        LastHotSpot = Pending->HotSpot;
                        ++PixelRevision;
                    }
                }
                Reusable = Pending;
                Pending.Reset();
            }
            else if (Pending->Submitted.Load() && !Pending->Copying.Load() && Pending->GPU.IsReady())
            {
                Pending->Copying.Store(true);
                const auto Frame = Pending;
                ENQUEUE_RENDER_COMMAND(PetCursorReadback)(
                    [Frame](FRHICommandListImmediate&)
                    {
                        int32 Pitch = 0;
                        const auto* Data = static_cast<const FColor*>(Frame->GPU.Lock(Pitch));
                        Frame->Pixels.Reset();
                        if (Data)
                        {
                            Frame->Pixels.SetNumUninitialized(Frame->Size.X * Frame->Size.Y);
                            for (int32 Y = 0; Y < Frame->Size.Y; ++Y)
                            {
                                FMemory::Memcpy(Frame->Pixels.GetData() + Y * Frame->Size.X,
                                                Data + Y * Pitch,
                                                Frame->Size.X * sizeof(FColor));
                            }
                            Frame->GPU.Unlock();
                        }
                        Frame->Complete.Store(true);
                    });
            }
        }
        if (!Widget || Pending)
        {
            return;
        }
        const double Now = FPlatformTime::Seconds();
        if (Now - LastRender < 1. / FMath::Clamp(FrameRate, 1, 60))
        {
            return;
        }
        // UMG 内容仍按屏幕像素绘制；扩展透明画布不会缩放正常尺寸的光标。
        // 光标不再叠乘显示器 DPI、Slate 应用缩放或桌宠缩放；两种光标显示路径共用此尺寸。
        constexpr float Scale = 1.f;
        Widget->SlatePrepass(Scale);
        const FVector2D Desired = Widget->GetDesiredSize();
        // 零尺寸 UMG 回退到硬件光标；保持原始布局，另行测量 RenderTransform 后的子控件范围。
        if (!FMath::IsFinite(Desired.X) || !FMath::IsFinite(Desired.Y) || Desired.X <= 0 || Desired.Y <= 0)
        {
            ClearSoftwarePixels();
            return;
        }
        if (!RenderWindow)
        {
            RenderWindow = SNew(SVirtualWindow).Size(Desired);
        }
        const TSharedPtr<SWidget> OldParent = Widget->GetParentWidget();
        RenderWindow->SetContent(Widget.ToSharedRef());
        const auto Layout = DesktopPetCursorLayout::Measure(
            RenderWindow.ToSharedRef(), Desired, CVarPetCursorOverflowPadding.GetValueOnGameThread(), RetainedBounds);
        const FIntPoint Size = Layout.Size;
        if (!Renderer)
        {
            Renderer = MakeUnique<FWidgetRenderer>(true, true);
        }
        if (!Target)
        {
            Target.Reset(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
            Target->ClearColor = FLinearColor::Transparent;
        }
        if (Target->SizeX != Size.X || Target->SizeY != Size.Y)
        {
            Target->InitCustomFormat(Size.X, Size.Y, PF_B8G8R8A8, true);
            Target->UpdateResourceImmediate(true);
        }
        const float Delta = LastRender > 0 ? float(Now - LastRender) : 0.f;
        LastRender = Now;
        Renderer->DrawWindow(Target.Get(), RenderWindow->GetHittestGrid(), RenderWindow.ToSharedRef(),
                             Layout.Geometry, Layout.ClipRect, Delta, false);
        // 与 FWidgetRenderer::DrawWidget 一样，不接管项目光标 Widget 原有的父级。
        if (OldParent)
        {
            Widget->AssignParentWidget(OldParent);
        }
        Pending = Reusable ? Reusable : MakeShared<FPetCursorReadback, ESPMode::ThreadSafe>();
        Reusable.Reset();
        Pending->Submitted.Store(false);
        Pending->Copying.Store(false);
        Pending->Complete.Store(false);
        Pending->Generation = Generation;
        Pending->Size = Size;
        Pending->HotSpot = Layout.HotSpot;
        const auto Frame = Pending;
        auto* Resource = Target->GameThread_GetRenderTargetResource();
        ENQUEUE_RENDER_COMMAND(PetCursorCopy)(
            [Frame, Resource](FRHICommandListImmediate& Cmd)
            {
                FRHITexture* Texture = Resource->GetRenderTargetTexture();
                Cmd.Transition(FRHITransitionInfo(Texture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
                Frame->GPU.EnqueueCopy(Cmd, Texture);
                Cmd.Transition(FRHITransitionInfo(Texture, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
                Frame->Submitted.Store(true);
            });
    }
};

FDesktopPetCursor::FDesktopPetCursor() : State(MakeUnique<FState>())
{
}

FDesktopPetCursor::~FDesktopPetCursor()
{
    Release();
    // 只在销毁时等待在途命令；正常帧不 Flush、不同步阻塞 GPU。
    if (State->Renderer)
    {
        FlushRenderingCommands();
    }
    State->Pending.Reset();
    State->Reusable.Reset();
    State->Renderer.Reset();
    State->Target.Reset();
    State->DestroyOwnedCursor(State->Software);
    for (auto& Entry : State->Hardware)
    {
        State->DestroyOwnedCursor(Entry.Value.Handle);
    }
}

void FDesktopPetCursor::Update(
    HWND Window, UWorld* World, const FWidgetPath& Path, const FPointerEvent& Event, int32 FrameRate)
{
    State->Window = Window;
    if (!PetOwnsCursor(Window))
    {
        Release();
        return;
    }
    FCursorReply Reply = FCursorReply::Unhandled();
    // 与 Slate 相同，从命中子控件向父控件查找，保留 Cursor=None 和自定义 OnMapCursor。
    for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
    {
        const FArrangedWidget& Item = Path.Widgets[Index];
        Reply = Item.Widget->OnCursorQuery(Item.Geometry, Event);
        if (!Reply.IsEventHandled())
        {
            continue;
        }
        if (!Reply.GetCursorWidget())
        {
            for (int32 Parent = Index; Parent >= 0; --Parent)
            {
                const auto Mapped = Path.Widgets[Parent].Widget->OnMapCursor(Reply);
                if (Mapped.IsSet())
                {
                    Reply.SetCursorWidget(nullptr, Mapped.GetValue());
                    break;
                }
            }
        }
        break;
    }
    if (!Reply.IsEventHandled())
    {
        // 桌面窗口独立于隐藏游戏视口的捕获/焦点状态，使用 CurrentMouseCursor 作为背景类型。
        const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
        Reply = FCursorReply::Cursor(PC ? PC->CurrentMouseCursor.GetValue() : EMouseCursor::Default);
    }
    TSharedPtr<SWidget> Software = Reply.GetCursorWidget();
    if (Reply.GetCursorType() != EMouseCursor::None && !Software)
    {
        if (UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr)
        {
            if (Viewport->GetIsUsingSoftwareCursorWidgets())
            {
                Software = Viewport->GetSoftwareCursorWidget(Reply.GetCursorType());
            }
        }
    }
    State->SetWidget(Software);
    State->UpdateSoftware(FrameRate);
    if (Software && !State->LastPixels.IsEmpty())
    {
        if (State->CompositeSoftware)
        {
            State->Selected = nullptr; // 由无输入光标图层绘制，移除同位置的原生小光标。
        }
        else
        {
            State->RefreshSoftwareHandle();
            State->Selected = State->Software ? State->Software : State->GetHardware(Reply.GetCursorType());
        }
    }
    else
    {
        State->Selected = State->GetHardware(Reply.GetCursorType());
    }
    Apply(Window);
    // None/硬件光标立即按项目设置生效；先应用新选择，再释放不再使用的原生软件光标。
    if (!Software || State->LastPixels.IsEmpty() || State->CompositeSoftware)
    {
        State->DestroyOwnedCursor(State->Software);
    }
}

void FDesktopPetCursor::SetCompositeSoftwareCursor(bool bEnabled)
{
    State->CompositeSoftware = bEnabled;
    if (!bEnabled)
    {
        State->Overlay.Hide();
    }
}

bool FDesktopPetCursor::GetCompositeFrame(FDesktopPetCursorFrame& Out) const
{
    Out = {};
    // 独立图层已呈现整张光标时，主窗口不重复混合；图层创建失败仍保留原合成路径作为回退。
    if (State->Overlay.IsVisible() || !State->CompositeSoftware || !State->Owned || !State->Widget || State->LastPixels.IsEmpty() ||
        !PetOwnsCursor(State->Window))
    {
        return false;
    }
    Out.Pixels = &State->LastPixels;
    Out.Size = State->LastSize;
    Out.HotSpot = State->LastHotSpot;
    Out.Revision = State->PixelRevision;
    return true;
}

bool FDesktopPetCursor::Apply(HWND Window)
{
    FPetCursorDpiScope DpiScope;
    if (!PetOwnsCursor(Window))
    {
        State->Overlay.Hide();
        return false;
    }
    State->Owned = true;
    State->Window = Window;
    if (State->CompositeSoftware && State->Widget && !State->LastPixels.IsEmpty())
    {
        POINT Mouse{};
        if (GetCursorPos(&Mouse))
        {
            State->Overlay.Present(Window, State->LastPixels, State->LastSize,
                                   State->LastHotSpot, State->PixelRevision, Mouse);
        }
        else
        {
            State->Overlay.Hide();
        }
    }
    else
    {
        State->Overlay.Hide();
    }
    SetCursor(State->Selected);
    return true;
}

void FDesktopPetCursor::TickAnimations()
{
    if (!State->Owned || !State->Widget || !PetOwnsCursor(State->Window))
    {
        return;
    }
    if (State->Widget->GetWidgetClass().GetWidgetType() != SObjectWidget::StaticWidgetClass().GetWidgetType())
    {
        return;
    }
    UUserWidget* Root = StaticCastSharedPtr<SObjectWidget>(State->Widget)->GetWidgetObject();
    if (!IsValid(Root) || !Root->IsConstructed() || !Root->IsVisible())
    {
        return;
    }
    const auto Mark = [](UUserWidget* Widget)
    {
        if (IsValid(Widget) && Widget->IsConstructed() && Widget->IsVisible() && Widget->IsAnyAnimationPlaying())
        {
            UUMGSequenceTickManager::Get(Widget)->OnWidgetTicked(Widget);
        }
    };
    Mark(Root);
    if (Root->WidgetTree)
    {
        Root->WidgetTree->ForEachWidgetAndDescendants(
            [&Mark](UWidget* Child)
            {
                if (UUserWidget* Nested = Cast<UUserWidget>(Child))
                {
                    for (UWidget* Parent = Nested->GetParent(); Parent; Parent = Parent->GetParent())
                    {
                        if (!Parent->IsVisible())
                        {
                            return;
                        }
                    }
                    Mark(Nested);
                }
            });
    }
}

void FDesktopPetCursor::Release()
{
    FPetCursorDpiScope DpiScope;
    State->Overlay.Hide();
    if (!State->Owned)
    {
        return;
    }
    State->Owned = false;
    // 只撤销自己设置的句柄，别的窗口已切换光标时不覆盖它。
    if (GetCursor() != State->Selected)
    {
        return;
    }
    SetCursor(LoadCursor(nullptr, IDC_ARROW));
    POINT Point{};
    if (GetCursorPos(&Point))
    {
        HWND Below = WindowFromPoint(Point);
        if (Below && Below != State->Window)
        {
            // 请当前目标窗口恢复自己的光标；跨进程最多等待 20ms，避免卡住桌宠。
            DWORD_PTR Ignored = 0;
            SendMessageTimeout(Below,
                               WM_SETCURSOR,
                               reinterpret_cast<WPARAM>(Below),
                               MAKELPARAM(HTCLIENT, WM_MOUSEMOVE),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK,
                               20,
                               &Ignored);
        }
    }
}
