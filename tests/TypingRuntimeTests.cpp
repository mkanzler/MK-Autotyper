// Substitute only the keyboard runner's Win32 calls. No input reaches the desktop.
#define NOMINMAX
#include "../AutoTyper/SecureMemory.h"
#include <atomic>
#include <functional>
#include <vector>
#include <cstdio>
#include <stdexcept>

namespace fixture {
ULONGLONG now = 100, slept = 0;
unsigned batches = 0;
bool partial = false;
HKL layout;

HWND WINAPI foreground() {
    return reinterpret_cast<HWND>(1);
}

DWORD WINAPI thread(HWND, LPDWORD process) {
    if (process)
        *process = GetCurrentProcessId() + 1;
    return 1;
}

HKL WINAPI keyboard(DWORD) {
    return layout;
}

SHORT WINAPI keyState(int) {
    return 0;
}

ULONGLONG WINAPI ticks() {
    return now;
}

void WINAPI sleep(DWORD milliseconds) {
    now += milliseconds;
    slept += milliseconds;
}

UINT WINAPI send(UINT count, LPINPUT, int) {
    ++batches;
    return partial && count > 1 ? 1 : count;
}

void reset() {
    now = 100;
    slept = 0;
    batches = 0;
    partial = false;
}
} // namespace fixture

#define GetForegroundWindow fixture::foreground
#define GetWindowThreadProcessId fixture::thread
#define GetKeyboardLayout fixture::keyboard
#define GetAsyncKeyState fixture::keyState
#define GetKeyState fixture::keyState
#define GetTickCount64 fixture::ticks
#define Sleep fixture::sleep
#define SendInput fixture::send
#include "../AutoTyper/KeyboardInput.h"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetKeyboardLayout
#undef GetAsyncKeyState
#undef GetKeyState
#undef GetTickCount64
#undef Sleep
#undef SendInput

void check(bool value, const char* reason) {
    if (!value)
        throw std::runtime_error(reason);
}

int main() {
    try {
        fixture::layout = LoadKeyboardLayoutW(L"00000409", 0);
        check(fixture::layout != nullptr, "Fixture layout unavailable");
        keyboard::Options options;
        options.initialDelay = 0;
        options.delay = 60000;
        options.unicode = true;
        std::atomic_bool stop{false};
        secure::Text one(L"x", 1);
        check(keyboard::runSecure(one, options, stop) == keyboard::Result::completed && fixture::slept == 0 &&
                  fixture::batches == 1,
            "Final character retained a trailing delay");
        check(one[0] == 0, "Consumed plaintext was not cleared");
        fixture::reset();
        options.delay = 23;
        secure::Text two(L"ab", 2);
        check(keyboard::runSecure(two, options, stop) == keyboard::Result::completed &&
                  fixture::slept == 23 && fixture::batches == 2,
            "Inter-character delay changed or trailing delay remained");
        fixture::reset();
        secure::Text emoji(L"\xd83d\xde00", 2);
        check(keyboard::runSecure(emoji, options, stop) == keyboard::Result::completed &&
                  fixture::batches == 1 && fixture::slept == 0 && emoji[0] == 0 && emoji[1] == 0,
            "Surrogate pair was split or not wiped");
        fixture::reset();
        fixture::partial = true;
        secure::Text blocked(L"x", 1);
        check(keyboard::runSecure(blocked, options, stop) == keyboard::Result::inputBlocked &&
                  fixture::batches == 2,
            "Partial input did not report failure and release its pressed key");
        fixture::reset();
        options.initialDelay = 1000;
        secure::Text cancelled(L"secret", 6);
        check(keyboard::runSecure(cancelled, options, stop,
                  [&](const keyboard::Progress&) { stop = true; }) == keyboard::Result::cancelled &&
                  fixture::batches == 0,
            "Countdown cancellation injected input");
        std::puts(
            "PASS: immediate completion/wiping, inter-character delays, surrogate pairs, partial-input cleanup and countdown cancellation without desktop input.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
