#if WITH_DEV_AUTOMATION_TESTS

#include "DesktopPetCursorLayout.h"
#include "DesktopPetCursorOverlay.h"
#include "Misc/AutomationTest.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/CoreStyle.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"

namespace DesktopPetCursorTests
{
    TSharedRef<SVirtualWindow> MakeWindow(FVector2D Offset, TSharedPtr<SImage>& Image)
    {
        TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow).Size(FVector2D(100, 100));
        Window->SetContent(SNew(SBox).WidthOverride(100).HeightOverride(100)
            [SAssignNew(Image, SImage).Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
                .RenderTransform(FSlateRenderTransform(Offset))]);
        return Window;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetCursorLayoutTest, "DesktopPet.Cursor.OverflowLayoutAndHotspot",
                                EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetCursorLayoutTest::RunTest(const FString&)
{
    TSharedPtr<SImage> Image;
    const auto Window = DesktopPetCursorTests::MakeWindow(FVector2D(75, 50), Image);
    FSlateRect Bounds;
    auto Layout = DesktopPetCursorLayout::Measure(Window, FVector2D(100, 100), 0, Bounds);
    TestEqual(TEXT("Right/bottom overflow is included"), Layout.Size, FIntPoint(175, 150));
    TestEqual(TEXT("Padding does not recenter original hotspot"), Layout.HotSpot, FIntPoint(50, 50));
    Image->SetRenderTransform(FSlateRenderTransform(FVector2D(-60, -40)));
    Bounds = FSlateRect();
    Layout = DesktopPetCursorLayout::Measure(Window, FVector2D(100, 100), 0, Bounds);
    TestEqual(TEXT("Negative overflow is included"), Layout.Size, FIntPoint(160, 140));
    TestEqual(TEXT("Negative canvas origin compensates hotspot"), Layout.HotSpot, FIntPoint(110, 90));
    Image->SetRenderTransform(FSlateRenderTransform(FVector2D(0, 0)));
    Layout = DesktopPetCursorLayout::Measure(Window, FVector2D(100, 100), 0, Bounds);
    TestEqual(TEXT("Contracting animation retains allocation"), Layout.Size, FIntPoint(160, 140));
    Image->SetRenderTransform(FSlateRenderTransform(FQuat2D(FMath::DegreesToRadians(45.f))));
    Bounds = FSlateRect();
    Layout = DesktopPetCursorLayout::Measure(Window, FVector2D(100, 100), 0, Bounds);
    TestTrue(TEXT("Rotated child bounds extend beyond original canvas"), Layout.Size.X > 100 && Layout.Size.Y > 100);
    Image->SetRenderTransform(FSlateRenderTransform(FVector2D(10000, 0)));
    Bounds = FSlateRect();
    Layout = DesktopPetCursorLayout::Measure(Window, FVector2D(100, 100), 0, Bounds);
    TestTrue(TEXT("Extreme effects are scaled to a bounded allocation"), Layout.Scale < 1 && Layout.Size.X <= 4096 && Layout.Size.Y <= 4096);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetCursorRenderTest, "DesktopPet.Cursor.OverflowPixels",
                                EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetCursorRenderTest::RunTest(const FString&)
{
    for (const FVector2D Offset : {FVector2D(75, 50), FVector2D(-60, -40)})
    {
        TSharedPtr<SImage> Image;
        const auto Window = DesktopPetCursorTests::MakeWindow(Offset, Image);
        FSlateRect Bounds;
        const auto Layout = DesktopPetCursorLayout::Measure(Window, FVector2D(100, 100), 16, Bounds);
        TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
        Target->ClearColor = FLinearColor::Transparent;
        Target->InitCustomFormat(Layout.Size.X, Layout.Size.Y, PF_B8G8R8A8, true);
        Target->UpdateResourceImmediate(true);
        FWidgetRenderer Renderer(true, true);
        Renderer.DrawWindow(Target.Get(), Window->GetHittestGrid(), Window, Layout.Geometry, Layout.ClipRect, 0, false);
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        if (!TestTrue(TEXT("Rendered pixels can be read"), Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels)))
        {
            return false;
        }
        int32 OpaqueCount = 0;
        FIntRect Painted(Layout.Size.X, Layout.Size.Y, 0, 0);
        for (int32 Y = 0; Y < Layout.Size.Y; ++Y)
        {
            for (int32 X = 0; X < Layout.Size.X; ++X)
            {
                if (Pixels[Y * Layout.Size.X + X].A > 200)
                {
                    ++OpaqueCount;
                    Painted.Min.X = FMath::Min(Painted.Min.X, X);
                    Painted.Min.Y = FMath::Min(Painted.Min.Y, Y);
                    Painted.Max.X = FMath::Max(Painted.Max.X, X + 1);
                    Painted.Max.Y = FMath::Max(Painted.Max.Y, Y + 1);
                }
            }
        }
        TestEqual(TEXT("Full 100x100 image survives offset"), OpaqueCount, 10000);
        TestEqual(TEXT("Image width is not clipped"), Painted.Width(), 100);
        TestEqual(TEXT("Image height is not clipped"), Painted.Height(), 100);
        TestEqual(TEXT("Position relative to mouse is unchanged"), Painted.Min - Layout.HotSpot,
                  FIntPoint(int32(Offset.X) - 50, int32(Offset.Y) - 50));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPetCursorOverlayTest, "DesktopPet.Cursor.OverlayBoundsAndFocus",
                                EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FPetCursorOverlayTest::RunTest(const FString&)
{
    const HWND Foreground = GetForegroundWindow(), Focus = GetFocus();
    HWND Owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"PetCursorTestOwner",
                                 WS_POPUP, 20000, 20000, 24, 24, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!TestNotNull(TEXT("Test owner created"), Owner)) return false;
    ShowWindow(Owner, SW_SHOWNOACTIVATE);
    HWND OverlayWindow = nullptr;
    {
        FDesktopPetCursorOverlay Overlay;
        TArray<FColor> Pixels;
        Pixels.Init(FColor::White, 240 * 200);
        TestTrue(TEXT("Oversized cursor presented"), Overlay.Present(Owner, Pixels, FIntPoint(240, 200), FIntPoint(100, 80), 1, POINT{20005, 20005}));
        struct FLookup { HWND Owner; HWND Result; } Lookup{Owner, nullptr};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND Window, LPARAM Param) -> BOOL
        {
            auto& Data = *reinterpret_cast<FLookup*>(Param);
            wchar_t ClassName[64]{};
            GetClassNameW(Window, ClassName, UE_ARRAY_COUNT(ClassName));
            if (GetWindow(Window, GW_OWNER) == Data.Owner &&
                FCString::Strcmp(ClassName, TEXT("UE58DesktopPetCursorOverlay")) == 0)
            {
                Data.Result = Window;
            }
            return 1;
        }, reinterpret_cast<LPARAM>(&Lookup));
        OverlayWindow = Lookup.Result;
        RECT Rect{};
        TestTrue(TEXT("Overlay exists"), GetWindowRect(OverlayWindow, &Rect) != 0);
        TestEqual(TEXT("Overlay extends beyond pet bounds"), FIntPoint(Rect.right - Rect.left, Rect.bottom - Rect.top), FIntPoint(240, 200));
        TestEqual(TEXT("Overlay anchor remains the actual mouse point"), FIntPoint(Rect.left + 100, Rect.top + 80), FIntPoint(20005, 20005));
        const LONG_PTR Style = GetWindowLongPtr(OverlayWindow, GWL_EXSTYLE);
        TestTrue(TEXT("Overlay passes input and cannot activate"), (Style & (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) == (WS_EX_TRANSPARENT | WS_EX_NOACTIVATE));
        TestTrue(TEXT("Foreground unchanged"), GetForegroundWindow() == Foreground);
        TestTrue(TEXT("Keyboard focus unchanged"), GetFocus() == Focus);
        Overlay.Hide();
        TestFalse(TEXT("Overlay hides immediately on release"), Overlay.IsVisible());
    }
    TestFalse(TEXT("Overlay destroyed with resources"), IsWindow(OverlayWindow) != 0);
    DestroyWindow(Owner);
    return true;
}

#endif
