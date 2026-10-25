#pragma once
#include <Windows.h>
#include <algorithm>

namespace placement {
struct Saved {
    DWORD version = 1;
    UINT dpi = 96;
    RECT bounds = {};
    POINT workOrigin = {};
};

inline bool valid(const Saved& value) {
    const auto width = static_cast<LONGLONG>(value.bounds.right) - value.bounds.left;
    const auto height = static_cast<LONGLONG>(value.bounds.bottom) - value.bounds.top;
    const auto coordinate = [](LONG n) { return n >= -100000 && n <= 100000; };
    return value.version == 1 && value.dpi >= 48 && value.dpi <= 960 && coordinate(value.bounds.left) &&
           coordinate(value.bounds.top) && coordinate(value.workOrigin.x) && coordinate(value.workOrigin.y) &&
           width >= 100 && height >= 100 && width <= 32768 && height <= 32768;
}

inline RECT fit(const Saved& value, UINT dpi, RECT work) {
    const int width = (std::min<LONG>)(work.right - work.left,
        MulDiv(value.bounds.right - value.bounds.left, dpi, value.dpi));
    const int height = (std::min<LONG>)(work.bottom - work.top,
        MulDiv(value.bounds.bottom - value.bounds.top, dpi, value.dpi));
    const int x = (std::max)(work.left,
        (std::min)(work.right - width,
            work.left + MulDiv(value.bounds.left - value.workOrigin.x, dpi, value.dpi)));
    const int y =
        (std::max)(work.top, (std::min)(work.bottom - height,
                                 work.top + MulDiv(value.bounds.top - value.workOrigin.y, dpi, value.dpi)));
    return {x, y, x + width, y + height};
}

inline void capture(HWND window, Saved& value) {
    if (!IsWindow(window) || IsIconic(window) || IsZoomed(window))
        return;
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    Saved candidate;
    candidate.dpi = GetDpiForWindow(window);
    GetWindowRect(window, &candidate.bounds);
    candidate.workOrigin = {monitor.rcWork.left, monitor.rcWork.top};
    if (valid(candidate))
        value = candidate;
}

inline void restore(HWND window, const Saved& value) {
    if (!valid(value))
        return;
    RECT stored = value.bounds;
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromRect(&stored, MONITOR_DEFAULTTONEAREST), &monitor))
        return;
    // Move onto the destination monitor first so GetDpiForWindow reflects its DPI.
    SetWindowPos(window, nullptr, monitor.rcWork.left, monitor.rcWork.top, 0, 0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    const RECT bounds = fit(value, GetDpiForWindow(window), monitor.rcWork);
    SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER);
}
} // namespace placement
