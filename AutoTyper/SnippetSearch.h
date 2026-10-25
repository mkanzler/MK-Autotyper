#pragma once
#include "AppDialogs.h"
#include <algorithm>

namespace snippetsearch {
constexpr int Search = 600, Results = 601;
constexpr int VisibleRows = 4, RowHeight = 48;

inline bool contains(const std::wstring& name, const std::wstring& query) {
    if (query.empty())
        return true;
    if (query.size() > name.size())
        return false;
    for (size_t i = 0; i <= name.size() - query.size(); ++i)
        if (CompareStringOrdinal(name.c_str() + i, static_cast<int>(query.size()), query.c_str(),
                static_cast<int>(query.size()), TRUE) == CSTR_EQUAL)
            return true;
    return false;
}

inline std::vector<size_t> matches(const std::vector<std::wstring>& names, const std::wstring& query) {
    std::vector<size_t> result;
    for (size_t i = 0; i < names.size(); ++i)
        if (contains(names[i], query))
            result.push_back(i);
    return result;
}

struct State {
    dialogs::State ui;
    std::vector<size_t> filtered;
    HWND search = nullptr, list = nullptr, empty = nullptr;
    HWND target = nullptr;
    RECT bounds{};
    bool closing = false;
    bool enterArmed = false;
};

inline void finish(HWND window, State& state, INT_PTR result) {
    if (state.closing)
        return;
    state.closing = true;
    GetWindowRect(window, &state.bounds);
    EndDialog(window, result);
}

inline void filter(State& state) {
    wchar_t query[81] = {};
    GetWindowTextW(state.search, query, 81);
    state.filtered = matches(state.ui.names, query);
    SendMessageW(state.list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(state.list, LB_RESETCONTENT, 0, 0);
    for (size_t i : state.filtered) {
        const auto label =
            dialogs::itemText(state.ui.names[i], i < state.ui.encrypted.size() && state.ui.encrypted[i]);
        SendMessageW(state.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!state.filtered.empty())
        SendMessageW(state.list, LB_SETCURSEL, 0, 0);
    SetWindowTextW(state.empty, state.ui.names.empty() ? L"No saved snippets yet" : L"No matching snippets");
    ShowWindow(state.empty, state.filtered.empty() ? SW_SHOW : SW_HIDE);
    SendMessageW(state.list, WM_SETREDRAW, TRUE, 0);
    ShowWindow(state.list, state.filtered.empty() ? SW_HIDE : SW_SHOW);
    InvalidateRect(state.list, nullptr, TRUE);
}

inline void moveSelection(State& state, int delta) {
    if (state.filtered.empty())
        return;
    const int current = static_cast<int>(SendMessageW(state.list, LB_GETCURSEL, 0, 0));
    const int next = std::max(0, std::min(static_cast<int>(state.filtered.size()) - 1, current + delta));
    SendMessageW(state.list, LB_SETCURSEL, next, 0);
}

inline LRESULT CALLBACK searchProc(
    HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto& state = *reinterpret_cast<State*>(data);
    if (field == state.list && message == WM_CHAR && (wParam >= L' ' || wParam == VK_BACK)) {
        SetFocus(state.search);
        SendMessageW(state.search, message, wParam, lParam);
        return 0;
    }
    if (field == state.search && message == WM_KEYDOWN && wParam == 'A' &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(field, EM_SETSEL, 0, -1);
        return 0;
    }
    if (field == state.search && message == WM_CHAR && wParam == 1)
        return 0;
    if (field == state.search && message == WM_MOUSEWHEEL)
        return SendMessageW(state.list, message, wParam, lParam);
    if (field == state.list && message == WM_LBUTTONUP) {
        const LRESULT result = DefSubclassProc(field, message, wParam, lParam);
        SetFocus(state.search);
        return result;
    }
    const auto handled = [](WPARAM key) {
        return key == VK_RETURN || key == VK_ESCAPE || key == VK_UP || key == VK_DOWN || key == VK_PRIOR ||
               key == VK_NEXT;
    };
    if (message == WM_GETDLGCODE && lParam && handled(reinterpret_cast<MSG*>(lParam)->wParam))
        return DefSubclassProc(field, message, wParam, lParam) | DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN && handled(wParam)) {
        if (wParam == VK_RETURN) {
            if (!(lParam & 0x40000000))
                state.enterArmed = true;
        } else if (wParam == VK_ESCAPE)
            SendMessageW(GetParent(field), WM_COMMAND, IDCANCEL, 0);
        else if (wParam == VK_UP || wParam == VK_DOWN)
            moveSelection(state, wParam == VK_UP ? -1 : 1);
        else if (wParam == VK_PRIOR || wParam == VK_NEXT)
            moveSelection(state, wParam == VK_PRIOR ? -VisibleRows : VisibleRows);
        return 0;
    }
    // Launch on release so a held Enter cannot repeat into the destination app.
    if (message == WM_KEYUP && wParam == VK_RETURN) {
        const bool accept = state.enterArmed;
        state.enterArmed = false;
        if (accept)
            SendMessageW(GetParent(field), WM_COMMAND, IDOK, 0);
        return 0;
    }
    if (message == WM_KILLFOCUS)
        state.enterArmed = false;
    if (message == WM_CHAR && (wParam == L'\r' || wParam == VK_ESCAPE))
        return 0;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(field, searchProc, 1);
    return DefSubclassProc(field, message, wParam, lParam);
}

inline INT_PTR CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        SetPropW(window, dialogs::ThemeProperty, reinterpret_cast<HANDLE>(1));
        state->closing = false;
        state->enterArmed = false;
        auto& ui = state->ui;
        ui.background = CreateSolidBrush(ui.style.background);
        ui.field = CreateSolidBrush(ui.style.field);
        SetWindowTextW(window, L"Type a saved snippet");
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(state->target, MONITOR_DEFAULTTOPRIMARY), &monitor);
        const int width = dialogs::px(ui, 500), height = dialogs::px(ui, 352);
        SetWindowPos(window, HWND_TOP,
            monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2,
            monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 3, width, height, 0);
        const BOOL dark = ui.style.dark;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        HWND heading = dialogs::add(window, ui, L"STATIC", L"Type a saved snippet", 0, 0, 24, 18, 452, 26);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(ui.style.heading), TRUE);
        dialogs::add(window, ui, L"STATIC", L"Search snippet names", 0, 0, 24, 51, 452, 22);
        state->search =
            dialogs::add(window, ui, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Search, 24, 78, 452, 26);
        SendMessageW(state->search, EM_SETLIMITTEXT, 80, 0);
        SendMessageW(
            state->search, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Start typing to search..."));
        state->list = dialogs::add(window, ui, L"LISTBOX", L"",
            WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
            Results, 20, 116, 460, VisibleRows * RowHeight);
        SendMessageW(state->list, LB_SETITEMHEIGHT, 0, dialogs::px(ui, RowHeight));
        state->empty = dialogs::add(window, ui, L"STATIC", L"", SS_CENTER, 0, 24, 194, 452, 24);
        dialogs::add(window, ui, L"STATIC", L"Arrow keys to select   /   Enter to type   /   Esc to close", 0,
            0, 24, 320, 452, 20);
        SetWindowSubclass(state->search, searchProc, 1, reinterpret_cast<DWORD_PTR>(state));
        SetWindowSubclass(state->list, searchProc, 1, reinterpret_cast<DWORD_PTR>(state));
        filter(*state);
        // Global-hotkey activation originates in another application. Activate
        // the visible overlay explicitly before assigning its search focus.
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        SetFocus(state->search);
        return FALSE;
    }
    if (!state)
        return FALSE;
    if (message == WM_NCDESTROY) {
        RemovePropW(window, dialogs::ThemeProperty);
        return FALSE;
    }
    if (message == dialogs::ThemeChanged) {
        dialogs::refreshStyle(window, state->ui, *reinterpret_cast<const dialogs::Style*>(lParam));
        return TRUE;
    }
    auto& ui = state->ui;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        FillRect(dc, &paint.rcPaint, ui.background);
        RECT bounds;
        GetClientRect(window, &bounds);
        HBRUSH border = CreateSolidBrush(ui.style.border);
        FrameRect(dc, &bounds, border);
        DeleteObject(border);
        HBRUSH brush = CreateSolidBrush(ui.style.field);
        HPEN pen = CreatePen(PS_SOLID, 1, ui.style.border);
        const HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        RoundRect(dc, dialogs::px(ui, 20), dialogs::px(ui, 74), dialogs::px(ui, 480), dialogs::px(ui, 108),
            dialogs::px(ui, 8), dialogs::px(ui, 8));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
        EndPaint(window, &paint);
        return TRUE;
    }
    if (message == WM_MOUSEWHEEL) {
        SendMessageW(state->list, message, wParam, lParam);
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORLISTBOX) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const bool field =
            reinterpret_cast<HWND>(lParam) == state->search || reinterpret_cast<HWND>(lParam) == state->list;
        SetTextColor(dc, ui.style.text);
        SetBkColor(dc, field ? ui.style.field : ui.style.background);
        return reinterpret_cast<INT_PTR>(field ? ui.field : ui.background);
    }
    if (message == WM_DRAWITEM) {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item.CtlID == Results && item.itemID < state->filtered.size())
            dialogs::drawSnippetRow(ui.style, item, ui.names[state->filtered[item.itemID]], false,
                state->filtered[item.itemID] < ui.encrypted.size() &&
                    ui.encrypted[state->filtered[item.itemID]]);
        return TRUE;
    }
    if (message == WM_CLOSE) {
        finish(window, *state, IDCANCEL);
        return TRUE;
    }
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        finish(window, *state, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam), notification = HIWORD(wParam);
        if (id == Search && notification == EN_CHANGE) {
            filter(*state);
            return TRUE;
        }
        if (id == IDCANCEL) {
            finish(window, *state, IDCANCEL);
            return TRUE;
        }
        if (id == IDOK || (id == Results && notification == LBN_DBLCLK)) {
            const LRESULT selected = SendMessageW(state->list, LB_GETCURSEL, 0, 0);
            if (selected != LB_ERR && static_cast<size_t>(selected) < state->filtered.size()) {
                ui.name = ui.names[state->filtered[static_cast<size_t>(selected)]];
                ui.encrypt = state->filtered[static_cast<size_t>(selected)] < ui.encrypted.size() &&
                             ui.encrypted[state->filtered[static_cast<size_t>(selected)]];
                finish(window, *state, IDOK);
            }
            return TRUE;
        }
    }
    return FALSE;
}

inline INT_PTR show(HWND owner, State& state) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, windowClass = 0, title = 0;
    } value{};

    value.dialog.style = WS_POPUP | WS_CLIPCHILDREN;
    value.dialog.dwExtendedStyle = WS_EX_TOOLWINDOW;
    value.dialog.cx = 300;
    value.dialog.cy = 220;
    return DialogBoxIndirectParamW(
        state.ui.style.instance, &value.dialog, owner, proc, reinterpret_cast<LPARAM>(&state));
}
} // namespace snippetsearch
