#include "../AutoTyper/KeyboardInput.h"
#include <cstdio>
#include <set>
#include <stdexcept>

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void verify(wchar_t c, HKL layout, bool unicode, bool caps) {
    const auto inputs = keyboard::character(c, layout, unicode, caps);
    BYTE state[256] = {};
    state[VK_CAPITAL] = caps ? 1 : 0;
    std::wstring result;
    for (const INPUT& input : inputs) {
        require(input.type == INPUT_KEYBOARD, "Non-keyboard input");
        const auto& k = input.ki;
        const bool up = (k.dwFlags & KEYEVENTF_KEYUP) != 0;
        if (k.dwFlags & KEYEVENTF_UNICODE) {
            require(k.wVk == 0, "Unicode event has a virtual key");
            require((k.dwFlags & ~(KEYEVENTF_UNICODE | KEYEVENTF_KEYUP)) == 0, "Invalid Unicode flags");
            if (!up)
                result += static_cast<wchar_t>(k.wScan);
            continue;
        }
        require((k.dwFlags & KEYEVENTF_SCANCODE) != 0 && k.wScan != 0, "Missing physical scan code");
        const UINT scan = k.wScan | ((k.dwFlags & KEYEVENTF_EXTENDEDKEY) ? 0xe000 : 0);
        const UINT vk = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, layout);
        require(vk > 0 && vk < 256, "Invalid virtual key");
        state[vk] = up ? 0 : 0x80;
        state[VK_SHIFT] = state[VK_LSHIFT] | state[VK_RSHIFT];
        state[VK_CONTROL] = state[VK_LCONTROL] | state[VK_RCONTROL];
        state[VK_MENU] = state[VK_LMENU] | state[VK_RMENU];
        if (!up && vk != VK_LSHIFT && vk != VK_RSHIFT && vk != VK_LCONTROL && vk != VK_RCONTROL &&
            vk != VK_LMENU && vk != VK_RMENU) {
            wchar_t buffer[8] = {};
            const int count = ToUnicodeEx(vk, k.wScan, state, buffer, 8, 4, layout);
            require(count == 1, "Physical key is a dead key or not text");
            result += buffer[0];
        }
    }
    if (result != std::wstring(1, c == L'\n' ? L'\r' : c)) {
        std::fprintf(stderr, "char=%04x layout=%p unicode=%d caps=%d actual=%04x\n", unsigned(c), layout,
            unicode, caps, result.empty() ? 0 : unsigned(result[0]));
        throw std::runtime_error("Character roundtrip failed");
    }
    for (int vk = 0; vk < 256; ++vk)
        if (vk != VK_CAPITAL)
            require(state[vk] == 0, "Key left held");
}

int main() {
    try {
        const wchar_t* layouts[] = {L"00000409", L"00000809", L"00000407"};
        for (const auto* name : layouts) {
            const HKL layout = LoadKeyboardLayoutW(name, 0);
            require(layout != nullptr, "Required Windows keyboard layout unavailable");
            require(keyboard::supported(layout), "Layout rejected");
            std::set<wchar_t> chars;
            for (wchar_t c = 32; c < 127; ++c)
                chars.insert(c);
            for (wchar_t c :
                std::wstring(
                    L"\u00e4\u00f6\u00fc\u00c4\u00d6\u00dc\u00df\u1e9e\u20ac\u00a7\u00b0\u00b2\u00b3\u00b5\u00b4\u00a3\u00ac\u00a6\u00e9\u00e8\u00ea\u00e1\u00e0"))
                chars.insert(c);
            // Enumerate every printable output of all physical keys with Shift/AltGr.
            unsigned combinations = 0;
            for (UINT vk = 1; vk < 256; ++vk) {
                if (!MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout))
                    continue;
                for (BYTE modifiers : {BYTE(0), BYTE(1), BYTE(6), BYTE(7)}) {
                    BYTE state[256] = {};
                    state[VK_SHIFT] = (modifiers & 1) ? 0x80 : 0;
                    state[VK_CONTROL] = (modifiers & 2) ? 0x80 : 0;
                    state[VK_MENU] = (modifiers & 4) ? 0x80 : 0;
                    wchar_t output[8] = {};
                    const int count = ToUnicodeEx(
                        vk, MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, layout), state, output, 8, 4, layout);
                    if (count == -1) { // Spacing version of each dead key must type literally.
                        chars.insert(output[0]);
                        ++combinations;
                    } else if (count > 0) {
                        for (int i = 0; i < count; ++i)
                            if (output[i] >= 32) {
                                chars.insert(output[i]);
                                ++combinations;
                            }
                    }
                }
            }
            for (wchar_t c : chars)
                for (bool unicode : {false, true})
                    for (bool caps : {false, true})
                        verify(c, layout, unicode, caps);
            for (wchar_t c : std::wstring(L"\r\n\t\b"))
                for (bool unicode : {false, true})
                    verify(c, layout, unicode, false);
            std::printf(
                "PASS %ls: %u printable key combinations, %zu distinct characters, both modes and Caps Lock states; Enter/Tab/Backspace\n",
                name, combinations, chars.size());
        }
        require(!keyboard::supported(reinterpret_cast<HKL>(0x040c)), "French unexpectedly supported");
        std::atomic_bool stop{true};
        require(keyboard::type(L"must not type", 2, 0, false, stop) == keyboard::Result::cancelled,
            "Cancellation failed");
        std::puts("All keyboard tests passed.");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
