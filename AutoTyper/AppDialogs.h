#pragma once
#include <Windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <oleacc.h>
#include <cwchar>
#include <functional>
#include <algorithm>
#include <string>
#include <vector>
#include "Hotkey.h"
#include "Settings.h"
#include "SnippetStore.h"

namespace dialogs {
struct Style {
    HINSTANCE instance;
    UINT dpi;
    HFONT font, heading;
    COLORREF background, field, text, muted, border, accent;
    bool dark;
};

struct State {
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;

    Style style;
    bool options = false;
    bool picker = false;
    bool encrypt = false;
    std::wstring title, name;
    std::vector<std::wstring> names;
    std::vector<bool> encrypted;
    settings::Preferences draft;
    std::function<bool(settings::Preferences&)> apply;
    std::function<void(HWND)> decorate;
    std::function<void(HWND)> changePassword;
    HBRUSH background = nullptr, field = nullptr;
    HWND edit = nullptr, snippetHotkey = nullptr, countdown = nullptr, enter = nullptr, shiftEnter = nullptr;
    HWND nameFeedback = nullptr, nameCounter = nullptr;
    snippets::NameIssue nameProblem = snippets::NameIssue::empty;

    ~State() {
        if (background)
            DeleteObject(background);
        if (field)
            DeleteObject(field);
    }
};

inline int px(const State& state, int value) {
    return MulDiv(value, state.style.dpi, 96);
}

inline HWND add(HWND owner, State& state, const wchar_t* kind, const wchar_t* text, DWORD style, int id,
    int x, int y, int width, int height) {
    HWND control = CreateWindowExW(0, kind, text, WS_CHILD | WS_VISIBLE | style, px(state, x), px(state, y),
        px(state, width), px(state, height), owner, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        state.style.instance, nullptr);
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.style.font), TRUE);
    return control;
}

constexpr int NameFeedback = 508, NameCounter = 509;

inline LRESULT CALLBACK nameEditProc(
    HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR) {
    if (message == WM_KEYDOWN && wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(field, EM_SETSEL, 0, -1);
        return 0;
    }
    // TranslateMessage also produces this control character for Ctrl+A.
    if (message == WM_CHAR && wParam == 1)
        return 0;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(field, nameEditProc, id);
    return DefSubclassProc(field, message, wParam, lParam);
}

inline std::wstring readName(HWND edit) {
    const int length = GetWindowTextLengthW(edit);
    std::wstring value(length + 1, L'\0');
    value.resize(GetWindowTextW(edit, &value[0], length + 1));
    return value;
}

inline const wchar_t* nameIssueText(snippets::NameIssue issue) {
    switch (issue) {
    case snippets::NameIssue::empty:
        return L"Enter a name of 1-80 characters.";
    case snippets::NameIssue::tooLong:
        return L"This name is too long. Shorten it to 80 characters or fewer.";
    case snippets::NameIssue::invalidCharacter:
        return L"Names cannot contain control characters or invalid Unicode.";
    default:
        return L"Up to 80 characters. Symbols and Unicode are supported.";
    }
}

inline bool updateNameValidation(HWND window, State& state) {
    if (!state.edit || !state.nameFeedback || !state.nameCounter) {
        EnableWindow(GetDlgItem(window, IDOK), FALSE);
        return false;
    }
    const auto value = readName(state.edit);
    state.nameProblem = snippets::nameIssue(value);
    SetWindowTextW(state.nameFeedback, nameIssueText(state.nameProblem));
    const auto count =
        std::to_wstring(value.size()) + L" / " + std::to_wstring(snippets::MaximumNameCharacters);
    SetWindowTextW(state.nameCounter, count.c_str());
    const bool valid = state.nameProblem == snippets::NameIssue::none;
    EnableWindow(GetDlgItem(window, IDOK), valid);
    InvalidateRect(state.nameFeedback, nullptr, TRUE);
    InvalidateRect(state.nameCounter, nullptr, TRUE);
    return valid;
}

inline WORD hotkeyValue(UINT modifiers, UINT key) {
    return MAKEWORD(key, ((modifiers & MOD_CONTROL) ? HOTKEYF_CONTROL : 0) |
                             ((modifiers & MOD_ALT) ? HOTKEYF_ALT : 0) |
                             ((modifiers & MOD_SHIFT) ? HOTKEYF_SHIFT : 0));
}

inline void readHotkey(HWND field, UINT& modifiers, UINT& key) {
    const WORD value = static_cast<WORD>(SendMessageW(field, HKM_GETHOTKEY, 0, 0));
    key = LOBYTE(value);
    modifiers = ((HIBYTE(value) & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
                ((HIBYTE(value) & HOTKEYF_ALT) ? MOD_ALT : 0) |
                ((HIBYTE(value) & HOTKEYF_SHIFT) ? MOD_SHIFT : 0);
}

inline std::wstring itemText(const std::wstring& name, bool encrypted) {
    return encrypted ? L"[Encrypted] " + name : name;
}

inline void drawSnippetRow(const Style& style, const DRAWITEMSTRUCT& item, const std::wstring& name,
    bool current = false, bool encrypted = false) {
    const auto px = [&](int value) { return MulDiv(value, style.dpi, 96); };
    const bool selected = (item.itemState & ODS_SELECTED) != 0;
    RECT row = item.rcItem;
    InflateRect(&row, -px(4), -px(3));
    HBRUSH brush = CreateSolidBrush(style.field);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    if (selected) {
        brush = CreateSolidBrush(style.accent);
        const HGDIOBJ oldBrush = SelectObject(item.hDC, brush),
                      oldPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        RoundRect(item.hDC, row.left, row.top, row.right, row.bottom, px(8), px(8));
        SelectObject(item.hDC, oldBrush);
        SelectObject(item.hDC, oldPen);
        DeleteObject(brush);
    }
    const HGDIOBJ oldFont = SelectObject(item.hDC, style.font);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, selected ? RGB(255, 255, 255) : style.text);
    RECT text = row;
    text.left += px(14);
    text.right -= px(14);
    if (current || encrypted) {
        const std::wstring label = encrypted ? current ? L"Encrypted / Current" : L"Encrypted" : L"Current";
        RECT badge = text;
        badge.left = badge.right - px(encrypted && current ? 150 : encrypted ? 90 : 65);
        DrawTextW(item.hDC, label.c_str(), -1, &badge, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        text.right = badge.left - px(12);
    }
    DrawTextW(item.hDC, name.c_str(), -1, &text,
        DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (item.itemState & ODS_FOCUS) {
        InflateRect(&row, -px(2), -px(2));
        DrawFocusRect(item.hDC, &row);
    }
    SelectObject(item.hDC, oldFont);
}

inline void drawButton(const Style& style, const DRAWITEMSTRUCT& item, int primaryButton = IDOK) {
    const bool primary = item.CtlID == static_cast<UINT>(primaryButton),
               enabled = !(item.itemState & ODS_DISABLED);
    const int radius = MulDiv(8, style.dpi, 96);
    HBRUSH brush = CreateSolidBrush(primary && enabled ? style.accent : style.field);
    HPEN pen = CreatePen(PS_SOLID, 1, primary ? style.accent : style.border);
    const HGDIOBJ oldBrush = SelectObject(item.hDC, brush), oldPen = SelectObject(item.hDC, pen),
                  oldFont = SelectObject(item.hDC, style.font);
    RoundRect(
        item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, radius, radius);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, !enabled ? style.muted : primary ? RGB(255, 255, 255) : style.text);
    wchar_t text[80];
    GetWindowTextW(item.hwndItem, text, 80);
    RECT rect = item.rcItem;
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    if (item.itemState & ODS_FOCUS) {
        InflateRect(&rect, -3, -3);
        DrawFocusRect(item.hDC, &rect);
    }
    SelectObject(item.hDC, oldBrush);
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldFont);
    DeleteObject(brush);
    DeleteObject(pen);
}

constexpr UINT ThemeChanged = WM_APP + 32;
constexpr wchar_t ThemeProperty[] = L"MK-AutoTyper.ThemedDialog";
constexpr int ThemeSystem = 510, ThemeLight = 511, ThemeDark = 512;

inline bool isThemeCommand(UINT id) {
    return id >= ThemeSystem && id <= ThemeDark;
}

inline const wchar_t* themeChoiceLabel(UINT id) {
    return id == ThemeLight ? L"Light" : id == ThemeDark ? L"Dark" : L"Windows setting";
}

// Native menu tracking supplies keyboard navigation, mouse selection and outside dismissal.
// The item data exposes names to accessibility tools even though we draw the rows.
struct ThemeMenu {
    HMENU handle = CreatePopupMenu();
    HBRUSH background;
    MSAAMENUINFO names[3]{};
    bool ready = false;

    ThemeMenu(const Style& style, settings::ThemeMode selected)
        : background(CreateSolidBrush(style.background)) {
        if (!handle || !background)
            return;
        MENUINFO info{sizeof(info)};
        info.fMask = MIM_BACKGROUND | MIM_STYLE;
        info.hbrBack = background;
        info.dwStyle = MNS_NOCHECK;
        if (!SetMenuInfo(handle, &info))
            return;
        for (UINT i = 0; i < 3; ++i) {
            const UINT id = ThemeSystem + i;
            const auto label = themeChoiceLabel(id);
            names[i] = {MSAA_MENU_SIG, static_cast<DWORD>(wcslen(label)), const_cast<wchar_t*>(label)};
            MENUITEMINFOW item{sizeof(item)};
            item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_DATA;
            item.wID = id;
            item.fType = MFT_OWNERDRAW;
            item.fState = static_cast<UINT>(selected) == i ? MFS_CHECKED : MFS_UNCHECKED;
            item.dwItemData = reinterpret_cast<ULONG_PTR>(&names[i]);
            if (!InsertMenuItemW(handle, i, TRUE, &item))
                return;
        }
        ready = true;
    }

    ThemeMenu(const ThemeMenu&) = delete;
    ThemeMenu& operator=(const ThemeMenu&) = delete;

    ~ThemeMenu() {
        if (handle)
            DestroyMenu(handle);
        if (background)
            DeleteObject(background);
    }
};

inline void drawThemeChoice(const Style& style, const DRAWITEMSTRUCT& item) {
    const auto px = [&](int n) { return MulDiv(n, style.dpi, 96); };
    const bool highlighted = (item.itemState & ODS_SELECTED) != 0;
    HBRUSH brush = CreateSolidBrush(style.background);
    FillRect(item.hDC, &item.rcItem, brush);
    DeleteObject(brush);
    RECT row = item.rcItem;
    InflateRect(&row, -px(4), -px(2));
    if (highlighted) {
        brush = CreateSolidBrush(style.accent);
        const HGDIOBJ oldBrush = SelectObject(item.hDC, brush);
        const HGDIOBJ oldPen = SelectObject(item.hDC, GetStockObject(NULL_PEN));
        RoundRect(item.hDC, row.left, row.top, row.right, row.bottom, px(8), px(8));
        SelectObject(item.hDC, oldPen);
        SelectObject(item.hDC, oldBrush);
        DeleteObject(brush);
    }
    const COLORREF foreground = highlighted ? RGB(255, 255, 255) : style.text;
    if (item.itemState & ODS_CHECKED) {
        const int center = (row.top + row.bottom) / 2;
        POINT tick[] = {{row.left + px(10), center}, {row.left + px(14), center + px(4)},
            {row.left + px(21), center - px(4)}};
        HPEN pen = CreatePen(PS_SOLID, px(2), highlighted ? foreground : style.accent);
        const HGDIOBJ oldPen = SelectObject(item.hDC, pen);
        Polyline(item.hDC, tick, 3);
        SelectObject(item.hDC, oldPen);
        DeleteObject(pen);
    }
    row.left += px(34);
    row.right -= px(12);
    const HGDIOBJ oldFont = SelectObject(item.hDC, style.font);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, foreground);
    DrawTextW(item.hDC, themeChoiceLabel(item.itemID), -1, &row, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    SelectObject(item.hDC, oldFont);
}

inline void refreshStyle(HWND window, State& state, const Style& style) {
    SetPropW(window, ThemeProperty, reinterpret_cast<HANDLE>(1));
    state.style = style;
    if (state.background)
        DeleteObject(state.background);
    if (state.field)
        DeleteObject(state.field);
    state.background = CreateSolidBrush(style.background);
    state.field = CreateSolidBrush(style.field);
    const BOOL dark = style.dark;
    DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

struct MessageState {
    State ui;
    std::wstring text;
    const wchar_t* title;
    UINT flags;
    int primary = IDOK, dismiss = IDOK;
};

inline INT_PTR CALLBACK messageProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<MessageState*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<MessageState*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        auto& ui = state->ui;
        refreshStyle(window, ui, ui.style);
        SetWindowTextW(window, state->title);
        const auto scaled = [&](int n) { return px(ui, n); };
        RECT owner{};
        GetWindowRect(GetParent(window), &owner);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(GetParent(window), MONITOR_DEFAULTTONEAREST), &monitor);
        const int width = scaled(520), padding = scaled(24), textWidth = width - 2 * padding;
        RECT measured{0, 0, textWidth, 0};
        HDC dc = GetDC(window);
        const HGDIOBJ oldFont = SelectObject(dc, ui.style.font);
        DrawTextW(dc, state->text.c_str(), -1, &measured,
            DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(dc, oldFont);
        ReleaseDC(window, dc);
        // Long messages scroll inside the dialog instead of extending below the screen.
        const int maximumText =
            std::max<LONG>(scaled(48), monitor.rcWork.bottom - monitor.rcWork.top - scaled(220));
        const int textHeight =
            std::max<LONG>(scaled(40), std::min<LONG>(measured.bottom + scaled(8), maximumText));
        const int buttonsY = scaled(88) + textHeight, height = buttonsY + scaled(58);
        RECT bounds{0, 0, width, height};
        AdjustWindowRectExForDpi(&bounds, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)), ui.style.dpi);
        const int outerWidth = bounds.right - bounds.left, outerHeight = bounds.bottom - bounds.top;
        const int left = (std::max)(monitor.rcWork.left,
            (std::min)(owner.left + (owner.right - owner.left - outerWidth) / 2,
                monitor.rcWork.right - outerWidth));
        const int top = (std::max)(monitor.rcWork.top,
            (std::min)(owner.top + (owner.bottom - owner.top - outerHeight) / 2,
                monitor.rcWork.bottom - outerHeight));
        SetWindowPos(window, nullptr, left, top, outerWidth, outerHeight, SWP_NOZORDER);
        HWND heading = add(window, ui, L"STATIC", state->title, SS_NOPREFIX, 0, 24, 20, 472, 28);
        SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(ui.style.heading), TRUE);
        const DWORD scroll = measured.bottom > maximumText ? WS_VSCROLL : 0;
        HWND body = CreateWindowExW(0, L"EDIT", state->text.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | scroll, padding, scaled(64),
            textWidth, textHeight, window, nullptr, ui.style.instance, nullptr);
        SendMessageW(body, WM_SETFONT, reinterpret_cast<WPARAM>(ui.style.font), TRUE);
        const UINT type = state->flags & MB_TYPEMASK;
        const int count = type == MB_YESNOCANCEL ? 3 : type == MB_YESNO ? 2 : 1;
        state->primary = type == MB_OK ? IDOK : (state->flags & MB_DEFMASK) == MB_DEFBUTTON2 ? IDNO : IDYES;
        state->dismiss = type == MB_OK ? IDOK : type == MB_YESNO ? IDNO : IDCANCEL;
        int x = 520 - 24 - count * 96 - (count - 1) * 12;
        const auto button = [&](const wchar_t* label, int id) {
            add(window, ui, L"BUTTON", label, WS_TABSTOP | BS_OWNERDRAW, id, x,
                MulDiv(buttonsY, 96, ui.style.dpi), 96, 34);
            x += 108;
        };
        if (type == MB_OK)
            button(L"OK", IDOK);
        else {
            button(L"&Yes", IDYES);
            button(L"&No", IDNO);
            if (type == MB_YESNOCANCEL)
                button(L"Cancel", IDCANCEL);
        }
        SendMessageW(window, DM_SETDEFID, state->primary, 0);
        SetFocus(GetDlgItem(window, state->primary));
        return FALSE;
    }
    if (!state)
        return FALSE;
    if (message == WM_NCDESTROY) {
        RemovePropW(window, ThemeProperty);
        return FALSE;
    }
    if (message == ThemeChanged) {
        refreshStyle(window, state->ui, *reinterpret_cast<const Style*>(lParam));
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT ||
        message == WM_CTLCOLORBTN) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, state->ui.style.text);
        SetBkColor(dc, state->ui.style.background);
        return reinterpret_cast<INT_PTR>(state->ui.background);
    }
    if (message == WM_DRAWITEM) {
        const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        FillRect(item.hDC, &item.rcItem, state->ui.background);
        drawButton(state->ui.style, item, state->primary);
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        EndDialog(window, state->dismiss);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == IDOK || ((state->flags & MB_TYPEMASK) != MB_OK && (id == IDYES || id == IDNO))) {
            EndDialog(window, id == IDOK ? state->primary : id);
            return TRUE;
        }
    }
    return FALSE;
}

// The app uses only OK, Yes/No and Yes/No/Cancel; keep their established result IDs.
inline int message(
    HWND owner, const Style& style, const wchar_t* text, const wchar_t* title, UINT flags = MB_OK) {
    MessageState state;
    state.ui.style = style;
    // A native multiline edit requires CRLF; callers may use ordinary newlines.
    for (const wchar_t* cursor = text; *cursor; ++cursor) {
        if (*cursor == L'\r' || *cursor == L'\n') {
            state.text += L"\r\n";
            if (*cursor == L'\r' && cursor[1] == L'\n')
                ++cursor;
        } else
            state.text += *cursor;
    }
    state.title = title;
    state.flags = flags;

    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, windowClass = 0, title = 0;
    } value{};

    value.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    value.dialog.cx = 300;
    value.dialog.cy = 140;
    return static_cast<int>(DialogBoxIndirectParamW(
        style.instance, &value.dialog, owner, messageProc, reinterpret_cast<LPARAM>(&state)));
}

inline INT_PTR CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        state->edit = state->nameFeedback = state->nameCounter = nullptr;
        state->nameProblem = snippets::NameIssue::empty;
        SetPropW(window, ThemeProperty, reinterpret_cast<HANDLE>(1));
        if (state->background)
            DeleteObject(state->background);
        if (state->field)
            DeleteObject(state->field);
        state->background = CreateSolidBrush(state->style.background);
        state->field = CreateSolidBrush(state->style.field);
        SetWindowTextW(window, state->title.c_str());
        const int listHeight = state->names.empty()
                                   ? 120
                                   : 48 * static_cast<int>(state->names.size() < 5 ? state->names.size() : 5);
        const int height = state->picker ? listHeight + 220 : state->options ? 544 : 248;
        RECT owner;
        GetWindowRect(GetParent(window), &owner);
        SetWindowPos(window, nullptr, owner.left + (owner.right - owner.left - px(*state, 500)) / 2,
            owner.top + (owner.bottom - owner.top - px(*state, height)) / 2, px(*state, 500),
            px(*state, height), SWP_NOZORDER);
        const BOOL dark = state->style.dark;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        if (state->picker) {
            HWND heading = add(window, *state, L"STATIC", L"Your snippets", 0, 0, 24, 20, 440, 30);
            SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(state->style.heading), TRUE);
            add(window, *state, L"STATIC",
                state->names.empty() ? L"Save text from the editor to build your collection."
                                     : L"Choose a snippet to load into the editor.",
                0, 0, 24, 57, 440, 24);
            state->edit = add(window, *state, L"LISTBOX", L"",
                WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED |
                    LBS_NOINTEGRALHEIGHT,
                500, 24, 98, 440, listHeight);
            SendMessageW(state->edit, LB_SETITEMHEIGHT, 0, px(*state, 48));
            int selected = 0;
            for (size_t i = 0; i < state->names.size(); ++i) {
                const auto label =
                    itemText(state->names[i], i < state->encrypted.size() && state->encrypted[i]);
                SendMessageW(state->edit, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
                if (state->names[i] == state->name)
                    selected = static_cast<int>(i);
            }
            if (state->names.empty()) {
                ShowWindow(state->edit, SW_HIDE);
                add(window, *state, L"STATIC", L"No saved snippets yet", SS_CENTER, 0, 24, 143, 440, 30);
            } else
                SendMessageW(state->edit, LB_SETCURSEL, selected, 0);
        } else if (state->options) {
            add(window, *state, L"STATIC", L"Start / stop shortcut", 0, 0, 24, 22, 440, 24);
            state->edit = add(window, *state, HOTKEY_CLASSW, L"", WS_TABSTOP, 500, 24, 53, 440, 30);
            SendMessageW(state->edit, HKM_SETHOTKEY,
                hotkeyValue(state->draft.hotkeyModifiers, state->draft.hotkeyKey), 0);
            add(window, *state, L"STATIC",
                L"Idle: type clipboard. Active: stop typing.\nUse Ctrl/Alt with a key, or a function key / Pause.",
                0, 0, 24, 95, 440, 44);
            add(window, *state, L"STATIC", L"Search and type saved snippets", 0, 0, 24, 153, 440, 24);
            state->snippetHotkey = add(window, *state, HOTKEY_CLASSW, L"", WS_TABSTOP, 504, 24, 184, 440, 30);
            SendMessageW(state->snippetHotkey, HKM_SETHOTKEY,
                hotkeyValue(state->draft.snippetHotkeyModifiers, state->draft.snippetHotkeyKey), 0);
            add(window, *state, L"STATIC", L"Insert line breaks with", 0, 0, 24, 237, 440, 24);
            state->enter = add(window, *state, L"BUTTON", L"&Enter",
                BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 501, 24, 267, 200, 28);
            state->shiftEnter =
                add(window, *state, L"BUTTON", L"&Shift+Enter", BS_AUTORADIOBUTTON, 502, 244, 267, 220, 28);
            SendMessageW(
                state->draft.shiftEnter ? state->shiftEnter : state->enter, BM_SETCHECK, BST_CHECKED, 0);
            state->countdown = add(window, *state, L"BUTTON", L"Show &countdown before typing",
                BS_AUTOCHECKBOX | WS_TABSTOP | WS_GROUP, 503, 24, 317, 440, 30);
            SendMessageW(
                state->countdown, BM_SETCHECK, state->draft.showCountdown ? BST_CHECKED : BST_UNCHECKED, 0);
            add(window, *state, L"BUTTON", L"Change master password...", WS_TABSTOP | BS_OWNERDRAW, 505, 24,
                366, 440, 34);
            if (state->decorate)
                for (HWND child :
                    {state->edit, state->snippetHotkey, state->enter, state->shiftEnter, state->countdown})
                    state->decorate(child);
        } else {
            add(window, *state, L"STATIC", L"Snippet name", 0, 0, 24, 18, 440, 24);
            state->nameCounter =
                add(window, *state, L"STATIC", L"", SS_RIGHT | SS_NOPREFIX, NameCounter, 344, 18, 120, 24);
            state->edit = add(window, *state, L"EDIT", state->name.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 500,
                24, 49, 440, 30);
            SetWindowSubclass(state->edit, nameEditProc, 1, 0);
            // Keep overlong input visible and invalid rather than silently truncating a paste.
            SendMessageW(state->edit, EM_SETLIMITTEXT, 32767, 0);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            state->nameFeedback =
                add(window, *state, L"STATIC", L"", SS_NOPREFIX, NameFeedback, 24, 90, 440, 48);
        }
        const int buttonsY = state->picker ? listHeight + 120 : state->options ? 445 : 154;
        add(window, *state, L"BUTTON", L"Cancel", WS_TABSTOP | BS_OWNERDRAW, IDCANCEL, 244, buttonsY, 100,
            34);
        HWND accept = add(window, *state, L"BUTTON",
            state->picker    ? L"Open snippet"
            : state->options ? L"Apply"
                             : L"Save",
            WS_TABSTOP | BS_OWNERDRAW, IDOK, 356, buttonsY, 108, 34);
        EnableWindow(accept, !state->picker || !state->names.empty());
        if (!state->picker && !state->options)
            updateNameValidation(window, *state);
        SendMessageW(window, DM_SETDEFID, IDOK, 0);
        SetFocus(state->picker && state->names.empty() ? GetDlgItem(window, IDCANCEL) : state->edit);
        return FALSE;
    }
    if (!state)
        return FALSE;
    if (message == WM_NCDESTROY) {
        RemovePropW(window, ThemeProperty);
        return FALSE;
    }
    if (message == ThemeChanged) {
        refreshStyle(window, *state, *reinterpret_cast<const Style*>(lParam));
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN ||
        message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND control = reinterpret_cast<HWND>(lParam);
        const bool edit = control == state->edit;
        const bool nameStatus = (state->nameFeedback && control == state->nameFeedback) ||
                                (state->nameCounter && control == state->nameCounter);
        const bool invalid = state->nameProblem != snippets::NameIssue::none &&
                             state->nameProblem != snippets::NameIssue::empty;
        const COLORREF error = state->style.dark ? RGB(255, 160, 160) : RGB(176, 32, 48);
        SetTextColor(dc, nameStatus ? (invalid ? error : state->style.muted) : state->style.text);
        SetBkColor(dc, edit ? state->style.field : state->style.background);
        return reinterpret_cast<INT_PTR>(edit ? state->field : state->background);
    }
    if (message == WM_DRAWITEM) {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlType == ODT_LISTBOX) {
            FillRect(item->hDC, &item->rcItem, state->field);
            if (item->itemID == static_cast<UINT>(-1) || item->itemID >= state->names.size())
                return TRUE;
            drawSnippetRow(state->style, *item, state->names[item->itemID],
                state->names[item->itemID] == state->name,
                item->itemID < state->encrypted.size() && state->encrypted[item->itemID]);
            return TRUE;
        }
        drawButton(state->style, *item);
        return TRUE;
    }
    if (message == WM_CLOSE) {
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND) {
        if (!state->picker && !state->options && LOWORD(wParam) == 500 && HIWORD(wParam) == EN_CHANGE &&
            state->nameFeedback) {
            updateNameValidation(window, *state);
            return TRUE;
        }
        if (LOWORD(wParam) == 505 && state->changePassword) {
            state->changePassword(window);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(window, IDCANCEL);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK ||
            (state->picker && LOWORD(wParam) == 500 && HIWORD(wParam) == LBN_DBLCLK)) {
            if (state->picker) {
                const LRESULT selected = SendMessageW(state->edit, LB_GETCURSEL, 0, 0);
                if (selected == LB_ERR || static_cast<size_t>(selected) >= state->names.size())
                    return TRUE;
                state->name = state->names[static_cast<size_t>(selected)];
                state->encrypt = static_cast<size_t>(selected) < state->encrypted.size() &&
                                 state->encrypted[static_cast<size_t>(selected)];
            } else if (state->options) {
                readHotkey(state->edit, state->draft.hotkeyModifiers, state->draft.hotkeyKey);
                readHotkey(
                    state->snippetHotkey, state->draft.snippetHotkeyModifiers, state->draft.snippetHotkeyKey);
                if (!hotkey::valid(state->draft.hotkeyModifiers, state->draft.hotkeyKey) ||
                    !hotkey::valid(state->draft.snippetHotkeyModifiers, state->draft.snippetHotkeyKey)) {
                    dialogs::message(window, state->style,
                        L"Choose Ctrl/Alt with a key, a function key, or Pause.", L"Shortcut",
                        MB_OK | MB_ICONINFORMATION);
                    return TRUE;
                }
                if (state->draft.hotkeyModifiers == state->draft.snippetHotkeyModifiers &&
                    state->draft.hotkeyKey == state->draft.snippetHotkeyKey) {
                    dialogs::message(window, state->style, L"Choose a different shortcut for each action.",
                        L"Shortcuts", MB_OK | MB_ICONINFORMATION);
                    return TRUE;
                }
                state->draft.shiftEnter = SendMessageW(state->shiftEnter, BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->draft.showCountdown = SendMessageW(state->countdown, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (state->apply && !state->apply(state->draft)) {
                    dialogs::message(window, state->style,
                        L"That shortcut could not be registered. It may be reserved or used by another application. Choose another combination, or cancel to restore your previous shortcut.",
                        L"Shortcut unavailable", MB_OK | MB_ICONINFORMATION);
                    return TRUE;
                }
            } else {
                if (!updateNameValidation(window, *state)) {
                    SetFocus(state->edit);
                    return TRUE;
                }
                state->name = readName(state->edit);
            }
            EndDialog(window, IDOK);
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

    value.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    value.dialog.cx = 300;
    value.dialog.cy = 200;
    return DialogBoxIndirectParamW(
        state.style.instance, &value.dialog, owner, proc, reinterpret_cast<LPARAM>(&state));
}
} // namespace dialogs
