#pragma once

#include "CoreMinimal.h"
#include "Windows/WindowsHWrapper.h"

/** A small owned layered popup keeps cursor effects outside the pet's rectangle without receiving input. */
class FDesktopPetCursorOverlay
{
public:
    ~FDesktopPetCursorOverlay()
    {
        Reset();
    }

    bool IsVisible() const { return Window && IsWindow(Window) && IsWindowVisible(Window); }

    void Hide()
    {
        if (IsVisible())
        {
            ShowWindow(Window, SW_HIDE);
        }
    }

    bool Present(HWND InOwner, const TArray<FColor>& Pixels, FIntPoint Size,
                 FIntPoint HotSpot, uint64 Revision, POINT Mouse)
    {
        if (!IsWindow(InOwner) || !IsWindowVisible(InOwner) || IsIconic(InOwner) ||
            Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != int64(Size.X) * Size.Y)
        {
            Hide();
            return false;
        }
        if (Owner != InOwner || !IsWindow(Window))
        {
            Reset();
            WNDCLASSEXW Class{};
            Class.cbSize = sizeof(Class);
            Class.hInstance = GetModuleHandle(nullptr);
            Class.lpfnWndProc = WindowProc;
            Class.lpszClassName = L"UE58DesktopPetCursorOverlay";
            RegisterClassExW(&Class);
            Window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                     Class.lpszClassName, L"DesktopPet Cursor", WS_POPUP,
                                     0, 0, 1, 1, InOwner, nullptr, Class.hInstance, nullptr);
            Owner = InOwner;
            if (!Window)
            {
                return false;
            }
        }
        const bool Resized = BitmapSize != Size;
        if (Resized)
        {
            if (!DC)
            {
                DC = CreateCompatibleDC(nullptr);
            }
            BITMAPINFO Info{};
            Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            Info.bmiHeader.biWidth = Size.X;
            Info.bmiHeader.biHeight = -Size.Y;
            Info.bmiHeader.biPlanes = 1;
            Info.bmiHeader.biBitCount = 32;
            Info.bmiHeader.biCompression = BI_RGB;
            void* NewBits = nullptr;
            HBITMAP NewBitmap = CreateDIBSection(DC, &Info, DIB_RGB_COLORS, &NewBits, nullptr, 0);
            if (!DC || !NewBitmap || !NewBits)
            {
                if (NewBitmap) DeleteObject(NewBitmap);
                Hide();
                return false;
            }
            HGDIOBJ Previous = SelectObject(DC, NewBitmap);
            if (!OldBitmap) OldBitmap = Previous;
            if (Bitmap) DeleteObject(Bitmap);
            Bitmap = NewBitmap;
            Bits = NewBits;
            BitmapSize = Size;
        }
        const FIntPoint Position(Mouse.x - HotSpot.X, Mouse.y - HotSpot.Y);
        DWORD Affinity = WDA_NONE;
        GetWindowDisplayAffinity(InOwner, &Affinity);
        if (Affinity != LastAffinity && SetWindowDisplayAffinity(Window, Affinity))
        {
            LastAffinity = Affinity;
        }
        const bool UpdatePixels = Resized || LastRevision != Revision || !IsVisible();
        if (UpdatePixels)
        {
            FMemory::Memcpy(Bits, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
            POINT Destination{Position.X, Position.Y}, Source{0, 0};
            SIZE NativeSize{Size.X, Size.Y};
            BLENDFUNCTION Blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            if (!UpdateLayeredWindow(Window, nullptr, &Destination, &NativeSize, DC, &Source, 0, &Blend, ULW_ALPHA))
            {
                Hide();
                return false;
            }
            LastRevision = Revision;
        }
        const bool Topmost = (GetWindowLongPtr(InOwner, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        const bool WasTopmost = (GetWindowLongPtr(Window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
        if (Position != LastPosition || Topmost != WasTopmost || !IsVisible())
        {
            const UINT Flags = SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW |
                               (Topmost == WasTopmost ? SWP_NOZORDER : 0);
            if (!SetWindowPos(Window, Topmost ? HWND_TOPMOST : HWND_NOTOPMOST, Position.X, Position.Y, 0, 0, Flags))
            {
                Hide();
                return false;
            }
        }
        LastPosition = Position;
        return true;
    }

private:
    static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
    {
        if (Message == WM_NCHITTEST) return HTTRANSPARENT;
        if (Message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (Message == WM_ERASEBKGND) return 1;
        return DefWindowProcW(Window, Message, WParam, LParam);
    }

    void Reset()
    {
        if (IsWindow(Window)) DestroyWindow(Window);
        if (DC && OldBitmap) SelectObject(DC, OldBitmap);
        if (Bitmap) DeleteObject(Bitmap);
        if (DC) DeleteDC(DC);
        Window = Owner = nullptr;
        DC = nullptr;
        Bitmap = nullptr;
        OldBitmap = nullptr;
        Bits = nullptr;
        BitmapSize = FIntPoint::ZeroValue;
        LastPosition = FIntPoint::ZeroValue;
        LastRevision = MAX_uint64;
        LastAffinity = MAX_uint32;
    }

    HWND Window = nullptr, Owner = nullptr;
    HDC DC = nullptr;
    HBITMAP Bitmap = nullptr;
    HGDIOBJ OldBitmap = nullptr;
    void* Bits = nullptr;
    FIntPoint BitmapSize = FIntPoint::ZeroValue, LastPosition = FIntPoint::ZeroValue;
    uint64 LastRevision = MAX_uint64;
    DWORD LastAffinity = MAX_uint32;
};
