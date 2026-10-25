#pragma once
#include <Windows.h>

namespace typing_target {
struct Destination {
    HWND window = nullptr, focus = nullptr;
    DWORD thread = 0;

    static Destination capture(HWND top = GetForegroundWindow()) {
        Destination value;
        value.window = top;
        value.thread = GetWindowThreadProcessId(value.window, nullptr);
        GUITHREADINFO info{sizeof(info)};
        if (value.thread && GetGUIThreadInfo(value.thread, &info))
            value.focus = info.hwndFocus;
        return value;
    }

    bool valid() const {
        return window && focus && IsWindow(window) && IsWindow(focus) &&
               GetWindowThreadProcessId(window, nullptr) == thread &&
               (focus == window || IsChild(window, focus));
    }

    bool restore() const {
        if (!valid())
            return false;
        if (GetForegroundWindow() != window && !SetForegroundWindow(window))
            return false;
        if (GetForegroundWindow() != window)
            return false;
        GUITHREADINFO info{sizeof(info)};
        if (GetGUIThreadInfo(thread, &info) && info.hwndFocus == focus)
            return true;
        // Only join queues for this brief focus transfer; no typing, waiting or
        // message pumping happens while attached to the destination thread.
        const DWORD current = GetCurrentThreadId();
        const bool attach = current != thread;
        if (attach && !AttachThreadInput(current, thread, TRUE))
            return false;
        SetFocus(focus);
        const bool restored = GetFocus() == focus;
        if (attach)
            AttachThreadInput(current, thread, FALSE);
        return restored && GetForegroundWindow() == window;
    }
};
} // namespace typing_target
