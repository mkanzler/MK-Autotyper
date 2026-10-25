#include "../AutoTyper/StartupShortcut.h"
#include <cstdio>
#include <stdexcept>

void check(bool condition, const char* reason) {
    if (!condition)
        throw std::runtime_error(reason);
}

int main() {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // Create a disposable shortcut beside this test binary, never in Startup.
    const std::wstring executable = startup::executablePath();
    const std::wstring path = executable + L".lnk";
    try {
        check(SUCCEEDED(apartment), "COM initialization failed");
        check(startup::writeShortcut(path, executable), "Shortcut creation failed");
        startup::Shortcut value;
        check(startup::readShortcut(path, value), "Shortcut readback failed");
        check(_wcsicmp(value.target.c_str(), executable.c_str()) == 0, "Wrong startup executable");
        check(value.arguments == L"/tray", "Startup would not be tray-only");
        check(value.show == SW_SHOWMINNOACTIVE, "Shortcut would activate a normal window");
        check(startup::removeShortcut(path), "Shortcut removal failed");
        check(startup::removeShortcut(path), "Removing an absent shortcut failed");
        check(!startup::writeShortcut(L"", executable), "Empty shortcut path accepted");
        CoUninitialize();
        std::puts(
            "PASS: native startup shortcut target, /tray argument, minimized/no-activation flag, readback and removal; actual Startup folder untouched.");
        return 0;
    } catch (const std::exception& error) {
        DeleteFileW(path.c_str());
        if (SUCCEEDED(apartment))
            CoUninitialize();
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
