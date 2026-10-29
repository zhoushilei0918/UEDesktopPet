#include "DesktopPetInputState.h"
#include <windowsx.h>

bool FDesktopPetInputState::IsMouseKey(uint32 VirtualKey)
{
    return VirtualKey == VK_LBUTTON || VirtualKey == VK_RBUTTON || VirtualKey == VK_MBUTTON ||
           VirtualKey == VK_XBUTTON1 || VirtualKey == VK_XBUTTON2;
}

void FDesktopPetInputState::SetMouseKey(uint32 VirtualKey, bool bDown)
{
    if (bDown)
    {
        MouseKeys.Add(VirtualKey);
    }
    else
    {
        MouseKeys.Remove(VirtualKey);
    }
}

void FDesktopPetInputState::HandleMessage(UINT Message, WPARAM WParam)
{
    switch (Message)
    {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: SetMouseKey(VK_LBUTTON, true); break;
        case WM_LBUTTONUP: SetMouseKey(VK_LBUTTON, false); break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONDBLCLK: SetMouseKey(VK_RBUTTON, true); break;
        case WM_RBUTTONUP: SetMouseKey(VK_RBUTTON, false); break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK: SetMouseKey(VK_MBUTTON, true); break;
        case WM_MBUTTONUP: SetMouseKey(VK_MBUTTON, false); break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONDBLCLK:
            SetMouseKey(GET_XBUTTON_WPARAM(WParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2, true);
            break;
        case WM_XBUTTONUP:
            SetMouseKey(GET_XBUTTON_WPARAM(WParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2, false);
            break;
        case WM_KILLFOCUS:
        case WM_CANCELMODE:
            Reset();
            break;
        case WM_SHOWWINDOW:
        case WM_ACTIVATEAPP:
            if (!WParam)
            {
                Reset();
            }
            break;
    }
}

void FDesktopPetInputState::Refresh()
{
    for (auto It = MouseKeys.CreateIterator(); It; ++It)
    {
        // 不读取低位的“自上次查询以来按过”，避免把一次点击误判成持续按住。
        if ((GetAsyncKeyState(GetPhysicalKey(*It)) & 0x8000) == 0)
        {
            It.RemoveCurrent();
        }
    }
}

void FDesktopPetInputState::Reset()
{
    MouseKeys.Reset();
}

bool FDesktopPetInputState::IsKeyDown(uint32 VirtualKey, bool bKeyboardFocus, bool bPhysicallyDown) const
{
    if (VirtualKey == 0 || VirtualKey > 255 || !bPhysicallyDown)
    {
        return false;
    }
    return IsMouseKey(VirtualKey) ? MouseKeys.Contains(VirtualKey) : bKeyboardFocus;
}

uint32 FDesktopPetInputState::GetVirtualKey(FKey Key)
{
    if (!Key.IsValid() || Key == EKeys::AnyKey || Key.IsGamepadKey() || Key.IsAnalog() || Key.IsTouch() ||
        Key.IsGesture() || Key == EKeys::MouseScrollUp || Key == EKeys::MouseScrollDown)
    {
        return 0;
    }
    if (Key.IsMouseButton() && Key != EKeys::LeftMouseButton && Key != EKeys::RightMouseButton &&
        Key != EKeys::MiddleMouseButton && Key != EKeys::ThumbMouseButton && Key != EKeys::ThumbMouseButton2)
    {
        return 0;
    }
    const uint32* KeyCode = nullptr;
    const uint32* CharCode = nullptr;
    FInputKeyManager::Get().GetCodesFromKey(Key, KeyCode, CharCode);
    if (KeyCode)
    {
        return *KeyCode;
    }
    // 字母、数字和标点在 UE 中可能只有字符映射，按当前键盘布局查找对应虚拟键。
    if (CharCode && *CharCode <= 0xffff)
    {
        const SHORT Mapped = VkKeyScanExW(static_cast<WCHAR>(*CharCode), GetKeyboardLayout(0));
        if (Mapped != -1)
        {
            return LOBYTE(Mapped);
        }
    }
    return 0;
}

uint32 FDesktopPetInputState::GetPhysicalKey(uint32 VirtualKey)
{
    if ((VirtualKey == VK_LBUTTON || VirtualKey == VK_RBUTTON) && GetSystemMetrics(SM_SWAPBUTTON))
    {
        return VirtualKey == VK_LBUTTON ? VK_RBUTTON : VK_LBUTTON;
    }
    return VirtualKey;
}
