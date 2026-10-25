#pragma once
#include <Windows.h>
#include "WindowPlacement.h"
#include "Hotkey.h"

namespace settings {
constexpr wchar_t RegistryPath[] = L"Software\\MK-AutoTyper";

enum class ThemeMode : DWORD {
    system,
    light,
    dark
};

struct Preferences {
    ThemeMode theme = ThemeMode::system;
    DWORD keystrokeDelay = 10;
    DWORD initialDelay = 1000;
    bool showCountdown = true;
    bool shiftEnter = false;
    UINT hotkeyModifiers = MOD_CONTROL | MOD_ALT;
    UINT hotkeyKey = 'V';
    UINT snippetHotkeyModifiers = MOD_CONTROL | MOD_ALT;
    UINT snippetHotkeyKey = 'P';
    placement::Saved position;
};

inline DWORD readDword(
    const wchar_t* path, const wchar_t* name, DWORD fallback, DWORD minimum, DWORD maximum) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, path, name, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
            ERROR_SUCCESS ||
        value < minimum || value > maximum)
        return fallback;
    return value;
}

inline ThemeMode loadTheme(const wchar_t* path) {
    DWORD mode = 0, size = sizeof(mode);
    const LSTATUS result =
        RegGetValueW(HKEY_CURRENT_USER, path, L"ThemeMode", RRF_RT_REG_DWORD, nullptr, &mode, &size);
    if (result == ERROR_SUCCESS && mode <= static_cast<DWORD>(ThemeMode::dark))
        return static_cast<ThemeMode>(mode);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
        // Preserve an explicitly saved choice from versions with a two-state toggle.
        const DWORD legacy = readDword(path, L"DarkTheme", 2, 0, 1);
        if (legacy <= 1)
            return legacy ? ThemeMode::dark : ThemeMode::light;
    }
    return ThemeMode::system;
}

inline bool systemUsesDarkTheme() {
    return readDword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               L"AppsUseLightTheme", 1, 0, 1) == 0;
}

inline bool usesDarkTheme(ThemeMode mode, bool systemDark) {
    return mode == ThemeMode::dark || (mode == ThemeMode::system && systemDark);
}

inline bool usesDarkTheme(ThemeMode mode) {
    return usesDarkTheme(mode, mode == ThemeMode::system && systemUsesDarkTheme());
}

inline const wchar_t* themeLabel(ThemeMode mode) {
    switch (mode) {
    case ThemeMode::light:
        return L"Theme: Light \u25be";
    case ThemeMode::dark:
        return L"Theme: Dark \u25be";
    default:
        return L"Theme: Windows \u25be";
    }
}

inline Preferences load(const wchar_t* path = RegistryPath) {
    Preferences value;
    value.theme = loadTheme(path);
    value.keystrokeDelay = readDword(path, L"KeystrokeDelay", 10, 2, 60000);
    value.initialDelay = readDword(path, L"InitialDelay", 1000, 0, 60000);
    value.showCountdown = readDword(path, L"ShowCountdown", 1, 0, 1) != 0;
    value.shiftEnter = readDword(path, L"ShiftEnter", 0, 0, 1) != 0;
    const UINT modifiers = readDword(path, L"HotkeyModifiers", MOD_CONTROL | MOD_ALT, 0, 7);
    const UINT key = readDword(path, L"HotkeyKey", 'V', 1, 254);
    if (hotkey::valid(modifiers, key)) {
        value.hotkeyModifiers = modifiers;
        value.hotkeyKey = key;
    }
    const UINT snippetModifiers = readDword(path, L"SnippetHotkeyModifiers", MOD_CONTROL | MOD_ALT, 0, 7);
    const UINT snippetKey = readDword(path, L"SnippetHotkeyKey", 'P', 1, 254);
    if (hotkey::valid(snippetModifiers, snippetKey)) {
        value.snippetHotkeyModifiers = snippetModifiers;
        value.snippetHotkeyKey = snippetKey;
    }
    DWORD size = sizeof(value.position);
    if (RegGetValueW(HKEY_CURRENT_USER, path, L"WindowPosition", RRF_RT_REG_BINARY, nullptr, &value.position,
            &size) != ERROR_SUCCESS ||
        size != sizeof(value.position) || !placement::valid(value.position))
        value.position = placement::Saved{};
    return value;
}

inline bool save(const Preferences& value, const wchar_t* path = RegistryPath) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;
    const auto write = [key](const wchar_t* name, DWORD data) {
        return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&data), sizeof(data)) ==
               ERROR_SUCCESS;
    };
    const bool success =
        write(L"ThemeMode", static_cast<DWORD>(value.theme)) &&
        write(L"KeystrokeDelay", value.keystrokeDelay) && write(L"InitialDelay", value.initialDelay) &&
        write(L"ShowCountdown", value.showCountdown ? 1 : 0) &&
        write(L"ShiftEnter", value.shiftEnter ? 1 : 0) && write(L"HotkeyModifiers", value.hotkeyModifiers) &&
        write(L"HotkeyKey", value.hotkeyKey) &&
        write(L"SnippetHotkeyModifiers", value.snippetHotkeyModifiers) &&
        write(L"SnippetHotkeyKey", value.snippetHotkeyKey);
    const bool positionSaved =
        !placement::valid(value.position) ||
        RegSetValueExW(key, L"WindowPosition", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&value.position),
            sizeof(value.position)) == ERROR_SUCCESS;
    RegCloseKey(key);
    return success && positionSaved;
}
} // namespace settings
