#pragma once
#include "AppDialogs.h"
#include "SnippetVariables.h"
#include <array>

namespace variabledialog {
struct State {
    dialogs::State ui;
    std::vector<snippetvariables::Field>* fields = nullptr;
    std::array<HWND, 4> edits{}, labels{};
    const RECT* placement = nullptr;
    bool protectedContent = false;
    size_t page = 0;
};

inline void clear(State& state) {
    for (HWND edit : state.edits) {
        SetWindowTextW(edit, L"");
        SendMessageW(edit, EM_EMPTYUNDOBUFFER, 0, 0);
    }
}

inline void store(State& state) {
    for (size_t i = 0; i < state.edits.size(); ++i) {
        const size_t index = state.page * 4 + i;
        if (index < state.fields->size())
            (*state.fields)[index].value = secure::Text::read(state.edits[i]);
    }
}

inline void refresh(HWND window, State& state) {
    clear(state);
    const auto title = L"Fill in snippet fields (" + std::to_wstring(state.page + 1) + L" / " +
                       std::to_wstring((state.fields->size() + 3) / 4) + L")";
    SetWindowTextW(window, title.c_str());
    for (size_t i = 0; i < state.edits.size(); ++i) {
        const size_t index = state.page * 4 + i;
        const bool visible = index < state.fields->size();
        ShowWindow(state.labels[i], visible ? SW_SHOW : SW_HIDE);
        ShowWindow(state.edits[i], visible ? SW_SHOW : SW_HIDE);
        SetWindowTextW(state.labels[i], visible ? (*state.fields)[index].name.c_str() : L"");
        if (visible)
            SetWindowTextW(state.edits[i], (*state.fields)[index].value.c_str());
    }
    const bool last = (state.page + 1) * 4 >= state.fields->size();
    SetWindowTextW(GetDlgItem(window, IDOK), last ? L"Start typing" : L"Next");
    EnableWindow(GetDlgItem(window, 950), state.page > 0);
    SetFocus(state.edits[0]);
}

inline INT_PTR CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        SetPropW(window, dialogs::ThemeProperty, reinterpret_cast<HANDLE>(1));
        state->ui.background = CreateSolidBrush(state->ui.style.background);
        state->ui.field = CreateSolidBrush(state->ui.style.field);
        SetWindowTextW(window, L"Fill in snippet fields");
        RECT owner;
        GetWindowRect(GetParent(window), &owner);
        const int width = dialogs::px(state->ui, 520), height = dialogs::px(state->ui, 510);
        SetWindowPos(window, nullptr,
            state->placement ? state->placement->left : owner.left + (owner.right - owner.left - width) / 2,
            state->placement ? state->placement->top : owner.top + (owner.bottom - owner.top - height) / 2,
            width, height, SWP_NOZORDER);
        BOOL dark = state->ui.style.dark;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        dialogs::add(window, state->ui, L"STATIC",
            state->protectedContent
                ? L"Values are hidden for this encrypted snippet.\nThey are cleared after this typing job."
                : L"Enter each value once. Repeated fields share the same value.\nValues are used only for this typing job.",
            0, 0, 24, 18, 472, 44);
        for (size_t i = 0; i < 4; ++i) {
            const int y = 76 + static_cast<int>(i) * 82;
            state->labels[i] = dialogs::add(
                window, state->ui, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS, 0, 24, y, 472, 22);
            state->edits[i] = dialogs::add(window, state->ui, L"EDIT", L"",
                WS_TABSTOP | ES_AUTOHSCROLL | (state->protectedContent ? ES_PASSWORD : 0),
                940 + static_cast<int>(i), 24, y + 26, 472, 30);
            SendMessageW(state->edits[i], EM_SETLIMITTEXT, snippetvariables::MaximumValue, 0);
            SetWindowSubclass(state->edits[i], dialogs::nameEditProc, 1, 0);
        }
        dialogs::add(window, state->ui, L"BUTTON", L"Back", WS_TABSTOP | BS_OWNERDRAW, 950, 24, 426, 100, 34);
        dialogs::add(
            window, state->ui, L"BUTTON", L"Cancel", WS_TABSTOP | BS_OWNERDRAW, IDCANCEL, 244, 426, 110, 34);
        dialogs::add(window, state->ui, L"BUTTON", L"", WS_TABSTOP | BS_OWNERDRAW, IDOK, 366, 426, 130, 34);
        refresh(window, *state);
        return FALSE;
    }
    if (!state)
        return FALSE;
    if (message == dialogs::ThemeChanged) {
        dialogs::refreshStyle(window, state->ui, *reinterpret_cast<const dialogs::Style*>(lParam));
        return TRUE;
    }
    if (message == WM_DRAWITEM) {
        dialogs::drawButton(state->ui.style, *reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    }
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLORBTN ||
        message == WM_CTLCOLOREDIT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, state->ui.style.text);
        const bool field = message == WM_CTLCOLOREDIT;
        SetBkColor(dc, field ? state->ui.style.field : state->ui.style.background);
        return reinterpret_cast<INT_PTR>(field ? state->ui.field : state->ui.background);
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        clear(*state);
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE) {
        SendMessageW(reinterpret_cast<HWND>(lParam), EM_EMPTYUNDOBUFFER, 0, 0);
        return TRUE;
    }
    if (message == WM_COMMAND && (LOWORD(wParam) == IDOK || LOWORD(wParam) == 950)) {
        store(*state);
        if (LOWORD(wParam) == 950) {
            if (state->page)
                --state->page;
        } else if ((state->page + 1) * 4 >= state->fields->size()) {
            clear(*state);
            EndDialog(window, IDOK);
            return TRUE;
        } else
            ++state->page;
        refresh(window, *state);
        return TRUE;
    }
    if (message == WM_DESTROY) {
        clear(*state);
        RemovePropW(window, dialogs::ThemeProperty);
    }
    return FALSE;
}

inline bool show(HWND owner, State& state) {
    if (!state.fields || state.fields->empty())
        return true;

    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, windowClass = 0, title = 0;
    } value{};

    value.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    value.dialog.cx = 300;
    value.dialog.cy = 260;
    return DialogBoxIndirectParamW(
               state.ui.style.instance, &value.dialog, owner, proc, reinterpret_cast<LPARAM>(&state)) == IDOK;
}
} // namespace variabledialog
