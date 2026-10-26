#include "../AutoTyper/Settings.h"
#include <cstdio>
#include <string>
#include <stdexcept>

namespace {
void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}

struct TestKey {
    // Never touch the application's actual preferences or another test's values.
    std::wstring path = L"Software\\MK-AutoTyper-Test-" + std::to_wstring(GetCurrentProcessId());

    ~TestKey() {
        RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
};
} // namespace

int main() {
    try {
        TestKey key;
        auto value = settings::load(key.path.c_str());
        check(value.theme == settings::ThemeMode::system && value.keystrokeDelay == 10 &&
                  value.initialDelay == 1000,
            "First-run defaults changed");
        check(value.showCountdown && !value.shiftEnter && !value.unlockForSession &&
                  value.hotkeyModifiers == (MOD_CONTROL | MOD_ALT) && value.hotkeyKey == 'V',
            "New feature defaults wrong");
        check(value.snippetHotkeyModifiers == (MOD_CONTROL | MOD_ALT) && value.snippetHotkeyKey == 'P',
            "Snippet shortcut default wrong");
        for (bool systemDark : {false, true}) {
            check(settings::usesDarkTheme(settings::ThemeMode::system, systemDark) == systemDark &&
                      !settings::usesDarkTheme(settings::ThemeMode::light, systemDark) &&
                      settings::usesDarkTheme(settings::ThemeMode::dark, systemDark),
                "Explicit theme did not override Windows or system theme did not follow it");
        }
        value.theme = settings::ThemeMode::dark;
        value.keystrokeDelay = 27;
        value.initialDelay = 3500;
        check(settings::save(value, key.path.c_str()), "Save failed");
        auto reloaded = settings::load(key.path.c_str());
        check(reloaded.theme == settings::ThemeMode::dark && reloaded.keystrokeDelay == 27 &&
                  reloaded.initialDelay == 3500,
            "Per-user roundtrip failed");
        value.theme = settings::ThemeMode::light;
        value.keystrokeDelay = 2;
        value.initialDelay = 0;
        check(settings::save(value, key.path.c_str()), "Light/minimum save failed");
        reloaded = settings::load(key.path.c_str());
        check(reloaded.theme == settings::ThemeMode::light && reloaded.keystrokeDelay == 2 &&
                  reloaded.initialDelay == 0,
            "Light theme or minimum delays lost");
        value.keystrokeDelay = 60000;
        value.initialDelay = 60000;
        check(settings::save(value, key.path.c_str()), "Maximum save failed");
        reloaded = settings::load(key.path.c_str());
        check(reloaded.keystrokeDelay == 60000 && reloaded.initialDelay == 60000, "Maximum delays lost");
        HKEY raw = nullptr;
        check(RegOpenKeyExW(HKEY_CURRENT_USER, key.path.c_str(), 0, KEY_SET_VALUE, &raw) == ERROR_SUCCESS,
            "Open test key failed");
        DWORD invalid = MAXDWORD;
        for (auto name : {L"ThemeMode", L"DarkTheme", L"KeystrokeDelay", L"InitialDelay"})
            RegSetValueExW(raw, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&invalid), sizeof(invalid));
        RegCloseKey(raw);
        reloaded = settings::load(key.path.c_str());
        check(reloaded.theme == settings::ThemeMode::system && reloaded.keystrokeDelay == 10 &&
                  reloaded.initialDelay == 1000,
            "Corrupt settings did not fall back safely");
        check(RegOpenKeyExW(HKEY_CURRENT_USER, key.path.c_str(), 0, KEY_SET_VALUE, &raw) == ERROR_SUCCESS,
            "Reopen failed");
        const wchar_t wrongType[] = L"123";
        RegSetValueExW(
            raw, L"KeystrokeDelay", 0, REG_SZ, reinterpret_cast<const BYTE*>(wrongType), sizeof(wrongType));
        RegCloseKey(raw);
        check(settings::load(key.path.c_str()).keystrokeDelay == 10, "Wrong registry type accepted");
        check(RegOpenKeyExW(HKEY_CURRENT_USER, key.path.c_str(), 0, KEY_SET_VALUE, &raw) == ERROR_SUCCESS,
            "Open legacy theme fixture failed");
        RegDeleteValueW(raw, L"ThemeMode");
        for (DWORD legacy : {0UL, 1UL}) {
            RegSetValueExW(
                raw, L"DarkTheme", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&legacy), sizeof(legacy));
            check(settings::load(key.path.c_str()).theme ==
                      (legacy ? settings::ThemeMode::dark : settings::ThemeMode::light),
                "Existing explicit theme was not migrated");
        }
        RegCloseKey(raw);
        value.theme = settings::ThemeMode::system;
        check(settings::save(value, key.path.c_str()) &&
                  settings::load(key.path.c_str()).theme == settings::ThemeMode::system,
            "Returning to Windows theme did not persist or was overridden by the legacy value");
        settings::Preferences extended;
        extended.showCountdown = false;
        extended.unlockForSession = true;
        extended.shiftEnter = true;
        extended.hotkeyModifiers = MOD_CONTROL | MOD_SHIFT;
        extended.hotkeyKey = VK_F8;
        extended.snippetHotkeyModifiers = MOD_ALT | MOD_SHIFT;
        extended.snippetHotkeyKey = VK_F9;
        extended.position.bounds = {120, 140, 1120, 840};
        extended.position.workOrigin = {0, 40};
        extended.position.dpi = 144;
        check(settings::save(extended, key.path.c_str()), "Extended settings save failed");
        reloaded = settings::load(key.path.c_str());
        check(reloaded.snippetHotkeyModifiers == (MOD_ALT | MOD_SHIFT) && reloaded.snippetHotkeyKey == VK_F9,
            "Snippet shortcut roundtrip failed");
        check(!reloaded.showCountdown && reloaded.shiftEnter && reloaded.unlockForSession &&
                  reloaded.hotkeyModifiers == (MOD_CONTROL | MOD_SHIFT) && reloaded.hotkeyKey == VK_F8 &&
                  reloaded.position.dpi == 144 && reloaded.position.bounds.left == 120 &&
                  reloaded.position.bounds.bottom == 840,
            "Shortcut/newline/countdown/placement roundtrip failed");
        check(RegOpenKeyExW(HKEY_CURRENT_USER, key.path.c_str(), 0, KEY_SET_VALUE, &raw) == ERROR_SUCCESS,
            "Open corrupt settings fixture failed");
        const DWORD shiftOnly = MOD_SHIFT, letter = 'A', wrongBoolean = 3;
        RegSetValueExW(raw, L"HotkeyModifiers", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&shiftOnly),
            sizeof(shiftOnly));
        RegSetValueExW(
            raw, L"HotkeyKey", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&letter), sizeof(letter));
        RegSetValueExW(raw, L"SnippetHotkeyModifiers", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&shiftOnly), sizeof(shiftOnly));
        RegSetValueExW(
            raw, L"SnippetHotkeyKey", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&letter), sizeof(letter));
        RegSetValueExW(raw, L"ShowCountdown", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wrongBoolean),
            sizeof(wrongBoolean));
        RegSetValueExW(raw, L"UnlockForSession", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wrongBoolean),
            sizeof(wrongBoolean));
        RegSetValueExW(raw, L"WindowPosition", 0, REG_BINARY, reinterpret_cast<const BYTE*>(&wrongBoolean),
            sizeof(wrongBoolean));
        RegCloseKey(raw);
        reloaded = settings::load(key.path.c_str());
        check(reloaded.snippetHotkeyModifiers == (MOD_CONTROL | MOD_ALT) && reloaded.snippetHotkeyKey == 'P',
            "Invalid snippet shortcut did not fall back");
        check(reloaded.hotkeyKey == 'V' && reloaded.hotkeyModifiers == (MOD_CONTROL | MOD_ALT) &&
                  reloaded.showCountdown && !reloaded.unlockForSession &&
                  !placement::valid(reloaded.position),
            "Invalid extended settings did not fall back");
        std::puts(
            "PASS: per-user settings defaults, light/dark roundtrip, delay boundaries, corrupt values and registry types.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
