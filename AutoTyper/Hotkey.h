#pragma once
#include <Windows.h>
#include <string>

namespace hotkey {
inline bool valid(UINT modifiers, UINT key) {
    if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU || key == VK_LSHIFT || key == VK_RSHIFT ||
        key == VK_LCONTROL || key == VK_RCONTROL || key == VK_LMENU || key == VK_RMENU || key == VK_LWIN ||
        key == VK_RWIN || key == VK_PACKET || key == VK_PROCESSKEY)
        return false;
    return key > 0 && key < 255 && (modifiers & ~(MOD_CONTROL | MOD_ALT | MOD_SHIFT)) == 0 &&
           ((modifiers & (MOD_CONTROL | MOD_ALT)) || (key >= VK_F1 && key <= VK_F24) || key == VK_PAUSE);
}

inline std::wstring name(UINT modifiers, UINT key) {
    std::wstring value;
    if (modifiers & MOD_CONTROL)
        value += L"Ctrl+";
    if (modifiers & MOD_ALT)
        value += L"Alt+";
    if (modifiers & MOD_SHIFT)
        value += L"Shift+";
    wchar_t buffer[80] = {};
    const UINT scan = MapVirtualKeyExW(key, MAPVK_VK_TO_VSC_EX, GetKeyboardLayout(0));
    const LONG keyInfo = static_cast<LONG>((scan & 0xff) << 16) | ((scan & 0xff00) == 0xe000 ? (1 << 24) : 0);
    if (key == VK_PAUSE)
        value += L"Pause";
    else if (GetKeyNameTextW(keyInfo, buffer, 80))
        value += buffer;
    else
        value += L"Key " + std::to_wstring(key);
    return value;
}

class Registration {
    HWND window_ = nullptr;
    int id_ = 0;
    int firstId_;
    UINT modifiers_ = 0, key_ = 0;
    decltype(&RegisterHotKey) register_;
    decltype(&UnregisterHotKey) unregister_;

public:
    explicit Registration(decltype(&RegisterHotKey) add = &RegisterHotKey,
        decltype(&UnregisterHotKey) remove = &UnregisterHotKey, int firstId = 1)
        : firstId_(firstId), register_(add), unregister_(remove) {}

    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;

    ~Registration() {
        clear();
    }

    bool replace(HWND window, UINT modifiers, UINT key) {
        if (!valid(modifiers, key))
            return false;
        if (id_ && window_ == window && modifiers_ == modifiers && key_ == key)
            return true;
        const int next = id_ == firstId_ ? firstId_ + 1 : firstId_;
        // Acquire the new combination before releasing the working shortcut.
        if (!register_(window, next, modifiers | MOD_NOREPEAT, key))
            return false;
        if (id_)
            unregister_(window_, id_);
        window_ = window;
        id_ = next;
        modifiers_ = modifiers;
        key_ = key;
        return true;
    }

    void clear() {
        if (id_)
            unregister_(window_, id_);
        id_ = 0;
    }

    bool matches(WPARAM id) const {
        return id_ != 0 && id == static_cast<WPARAM>(id_);
    }

    bool registered() const {
        return id_ != 0;
    }
};
} // namespace hotkey
