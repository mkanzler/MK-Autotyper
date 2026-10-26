#pragma once
#include <Windows.h>
#include <dwmapi.h>
#include <functional>
#include <string>
#include "TypingSession.h"

namespace sessionview {
inline std::wstring duration(ULONGLONG milliseconds) {
    const ULONGLONG seconds = (milliseconds + 999) / 1000;
    if (seconds >= 3600)
        return std::to_wstring(seconds / 3600) + L"h " + std::to_wstring((seconds / 60) % 60) + L"m";
    if (seconds >= 60)
        return std::to_wstring(seconds / 60) + L"m " + std::to_wstring(seconds % 60) + L"s";
    return std::to_wstring(seconds) + L"s";
}

inline std::wstring summary(const typing_session::Snapshot& value) {
    const auto& p = value.progress;
    if (p.stage == keyboard::Stage::countdown)
        return L"Starting in " + duration(p.remaining);
    if (p.stage == keyboard::Stage::waitingForKeys)
        return L"Release shortcut keys to continue";
    if (p.stage == keyboard::Stage::pause)
        return L"Pause: " + duration(p.remaining) + L" left";
    if (p.sequence)
        return L"Step " + std::to_wstring(std::min(p.actionIndex + 1, p.actionCount)) + L" / " +
               std::to_wstring(p.actionCount);
    std::wstring result = std::to_wstring(p.completed) + L" / " + std::to_wstring(p.total) + L" characters";
    if (p.completed && p.completed < p.total)
        result +=
            L"  \u00b7  About " + duration(p.elapsed * (p.total - p.completed) / p.completed) + L" left";
    return result;
}

struct Style {
    HINSTANCE instance;
    UINT dpi;
    HFONT body, heading;
    COLORREF panel, text, muted, border, accent;
    bool dark;
};

class Panel {
    HWND window_ = nullptr, cancel_ = nullptr;
    Style style_{};
    typing_session::Snapshot value_;
    bool stopping_ = false;
    std::wstring shortcut_;
    std::function<void()> cancelAction_;

    int px(int n) const {
        return MulDiv(n, style_.dpi, 96);
    }

    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
            return DefWindowProcW(window, message, wParam, lParam);
        if (message == WM_MOUSEACTIVATE)
            return MA_NOACTIVATE;
        if (message == WM_COMMAND && LOWORD(wParam) == 1) {
            if (self->cancelAction_)
                self->cancelAction_();
            return 0;
        }
        if (message == WM_CLOSE) {
            if (self->cancelAction_)
                self->cancelAction_();
            return 0;
        }
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(window, &ps);
            RECT rect;
            GetClientRect(window, &rect);
            HBRUSH background = CreateSolidBrush(self->style_.panel);
            FillRect(dc, &rect, background);
            DeleteObject(background);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, self->style_.text);
            HGDIOBJ oldFont = SelectObject(dc, self->style_.heading);
            RECT title{self->px(18), self->px(14), self->px(402), self->px(40)};
            const auto titleText = self->stopping_ ? L"Stopping..."
                                   : self->value_.progress.stage == keyboard::Stage::countdown
                                       ? L"Get ready to type"
                                       : L"AutoTyper is typing";
            DrawTextW(dc, titleText, -1, &title, DT_SINGLELINE | DT_VCENTER);
            SelectObject(dc, self->style_.body);
            SetTextColor(dc, self->style_.muted);
            RECT details{self->px(18), self->px(45), self->px(402), self->px(72)};
            const std::wstring text = summary(self->value_);
            DrawTextW(dc, text.c_str(), -1, &details, DT_SINGLELINE | DT_VCENTER);
            RECT track{self->px(18), self->px(80), self->px(402), self->px(85)};
            HBRUSH border = CreateSolidBrush(self->style_.border);
            FillRect(dc, &track, border);
            DeleteObject(border);
            if (self->value_.progress.total) {
                track.right = track.left +
                              static_cast<LONG>((track.right - track.left) * self->value_.progress.completed /
                                                self->value_.progress.total);
                HBRUSH accent = CreateSolidBrush(self->style_.accent);
                FillRect(dc, &track, accent);
                DeleteObject(accent);
            }
            RECT hint{self->px(18), self->px(100), self->px(290), self->px(128)};
            const auto help = self->shortcut_ + L" to stop";
            DrawTextW(dc, help.c_str(), -1, &hint, DT_SINGLELINE | DT_VCENTER);
            SelectObject(dc, oldFont);
            EndPaint(window, &ps);
            return 0;
        }
        if (message == WM_DRAWITEM) {
            const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            HBRUSH brush = CreateSolidBrush(self->style_.accent);
            HGDIOBJ oldBrush = SelectObject(item->hDC, brush),
                    oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN)),
                    oldFont = SelectObject(item->hDC, self->style_.body);
            RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom,
                self->px(8), self->px(8));
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(255, 255, 255));
            const wchar_t* action =
                self->value_.progress.stage == keyboard::Stage::countdown ? L"Cancel" : L"Stop";
            RECT rect = item->rcItem;
            DrawTextW(item->hDC, action, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item->hDC, oldBrush);
            SelectObject(item->hDC, oldPen);
            SelectObject(item->hDC, oldFont);
            DeleteObject(brush);
            return TRUE;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

public:
    Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    ~Panel() {
        close();
    }

    void close() {
        if (IsWindow(window_))
            DestroyWindow(window_);
        window_ = nullptr;
        cancel_ = nullptr;
    }

    void update(HWND destination, const Style& style, const typing_session::Snapshot& value,
        bool showCountdown, bool stopping, std::wstring shortcut, std::function<void()> cancel) {
        style_ = style;
        value_ = value;
        stopping_ = stopping;
        shortcut_ = std::move(shortcut);
        cancelAction_ = std::move(cancel);
        if (!showCountdown && value.progress.stage == keyboard::Stage::countdown)
            return;
        if (!window_) {
            WNDCLASSW wc = {};
            wc.lpfnWndProc = proc;
            wc.hInstance = style.instance;
            wc.lpszClassName = L"MK-AutoTyper-Session";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            RegisterClassW(&wc);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromWindow(destination, MONITOR_DEFAULTTONEAREST), &monitor);
            window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, wc.lpszClassName,
                L"AutoTyper progress", WS_POPUP | WS_BORDER, monitor.rcWork.right - px(444),
                monitor.rcWork.bottom - px(164), px(420), px(144), nullptr, nullptr, style.instance, this);
            if (!window_)
                return;
            cancel_ = CreateWindowW(L"BUTTON", L"Stop", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, px(312), px(98),
                px(90), px(32), window_, reinterpret_cast<HMENU>(1), style.instance, nullptr);
            const BOOL dark = style.dark;
            DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
            ShowWindow(window_, SW_SHOWNOACTIVATE);
        }
        const std::wstring caption = L"AutoTyper progress - " + summary(value_);
        SetWindowTextW(window_, caption.c_str());
        SetWindowTextW(cancel_, value_.progress.stage == keyboard::Stage::countdown ? L"Cancel" : L"Stop");
        InvalidateRect(window_, nullptr, FALSE);
    }
};
} // namespace sessionview
