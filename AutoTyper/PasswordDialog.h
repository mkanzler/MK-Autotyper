#pragma once
#include "AppDialogs.h"
#include "EncryptedSnippets.h"

namespace passwords {
enum class Mode {
    unlock,
    create,
    change
};

struct State {
    dialogs::State ui;
    Mode mode = Mode::unlock;
    const RECT* placement = nullptr;
    std::function<encrypted::Result(const secure::Text&, const secure::Text&)> action;
    HWND oldPassword = nullptr, newPassword = nullptr, confirmation = nullptr, error = nullptr;
    bool sessionUnlock = false;
    bool enterArmed = false;
};

inline const wchar_t* errorText(encrypted::Result result) {
    switch (result) {
    case encrypted::Result::authentication:
        return L"Incorrect master password, or the encrypted data was modified.";
    case encrypted::Result::exists:
        return L"A snippet with that name already exists.";
    case encrypted::Result::invalidName:
        return L"Choose a valid snippet name.";
    case encrypted::Result::tooLarge:
        return L"The encrypted snippet collection is too large.";
    case encrypted::Result::corrupt:
        return L"The encrypted file is damaged or unsupported. It was not changed.";
    case encrypted::Result::missing:
        return L"The encrypted snippet or collection no longer exists.";
    case encrypted::Result::weakPassword:
        return L"Use a master password of 12 to 1024 characters.";
    case encrypted::Result::invalidText:
        return L"The snippet contains an embedded null character and cannot be saved.";
    case encrypted::Result::invalidDocument:
        return L"The snippet document is damaged or invalid. It was not changed.";
    case encrypted::Result::unsupportedFormat:
        return L"This snippet uses an unsupported document version or action. It was not changed.";
    case encrypted::Result::unsupportedActions:
        return L"This snippet contains steps this version cannot edit or run. It was not changed.";
    case encrypted::Result::rollbackFailed:
        return L"The format change could not finish. Both saved formats may remain; check the snippet list before retrying.";
    default:
        return L"The encrypted file could not be read or updated. Nothing was changed.";
    }
}

inline void clearFields(State& state) {
    for (HWND edit : {state.oldPassword, state.newPassword, state.confirmation})
        if (edit) {
            SetWindowTextW(edit, L"");
            SendMessageW(edit, EM_EMPTYUNDOBUFFER, 0, 0);
        }
}

inline LRESULT CALLBACK inputProc(
    HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
    auto& state = *reinterpret_cast<State*>(data);
    if (message == WM_GETDLGCODE && lParam && reinterpret_cast<MSG*>(lParam)->wParam == VK_RETURN)
        return DefSubclassProc(window, message, wParam, lParam) | DLGC_WANTALLKEYS;
    if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        if (!(lParam & 0x40000000))
            state.enterArmed = true;
        return 0;
    }
    if (message == WM_CHAR && wParam == L'\r')
        return 0;
    if (message == WM_KEYUP && wParam == VK_RETURN) {
        const bool accept = state.enterArmed;
        state.enterArmed = false;
        if (accept)
            SendMessageW(
                GetParent(window), WM_COMMAND, GetDlgCtrlID(window) == IDCANCEL ? IDCANCEL : IDOK, 0);
        return 0;
    }
    if (message == WM_KILLFOCUS)
        state.enterArmed = false;
    if (message == WM_KEYDOWN && wParam == 'A' && GetDlgCtrlID(window) >= 700 &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
    }
    if (message == WM_CHAR && wParam == 1)
        return 0;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(window, inputProc, 1);
    return DefSubclassProc(window, message, wParam, lParam);
}

inline INT_PTR CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<State*>(lParam);
        SetWindowLongPtrW(window, DWLP_USER, lParam);
        SetPropW(window, dialogs::ThemeProperty, reinterpret_cast<HANDLE>(1));
        auto& ui = state->ui;
        ui.background = CreateSolidBrush(ui.style.background);
        ui.field = CreateSolidBrush(ui.style.field);
        const wchar_t* title = state->mode == Mode::create   ? L"Set master password"
                               : state->mode == Mode::change ? L"Change master password"
                                                             : L"Unlock encrypted snippet";
        SetWindowTextW(window, title);
        const int fields = state->mode == Mode::unlock ? 1 : state->mode == Mode::create ? 2 : 3;
        const int height = 225 + fields * 70;
        RECT owner;
        GetWindowRect(GetParent(window), &owner);
        const int x = state->placement ? state->placement->left
                                       : owner.left + (owner.right - owner.left - dialogs::px(ui, 500)) / 2;
        const int yTop = state->placement
                             ? state->placement->top
                             : owner.top + (owner.bottom - owner.top - dialogs::px(ui, height)) / 2;
        SetWindowPos(window, nullptr, x, yTop, dialogs::px(ui, 500), dialogs::px(ui, height), SWP_NOZORDER);
        const BOOL dark = ui.style.dark;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        dialogs::add(window, ui, L"STATIC",
            state->mode == Mode::create
                ? L"One master password protects all encrypted snippets.\nUse at least 12 characters. It cannot be recovered if lost."
            : state->mode == Mode::change
                ? L"All encrypted snippets will be updated together.\nThe previous password stays valid if the update fails."
            : state->sessionUnlock
                ? L"Enter your master password to unlock encrypted snippets\nfor this session. The password is not remembered."
                : L"Enter your master password. It is used for this action only.",
            0, 0, 24, 18, 440, 44);
        int y = 78, id = 700;
        const auto field = [&](const wchar_t* label) {
            dialogs::add(window, ui, L"STATIC", label, 0, 0, 24, y, 440, 22);
            HWND edit = dialogs::add(window, ui, L"EDIT", L"", WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
                id++, 24, y + 26, 440, 28);
            SendMessageW(edit, EM_SETLIMITTEXT, 1024, 0);
            SetWindowSubclass(edit, inputProc, 1, reinterpret_cast<DWORD_PTR>(state));
            y += 70;
            return edit;
        };
        if (state->mode != Mode::create)
            state->oldPassword =
                field(state->mode == Mode::change ? L"Current master password" : L"Master password");
        if (state->mode != Mode::unlock) {
            state->newPassword = field(L"New master password");
            state->confirmation = field(L"Confirm master password");
        }
        state->error = dialogs::add(window, ui, L"STATIC", L"", 0, 0, 24, y + 4, 440, 40);
        HWND cancel = dialogs::add(
            window, ui, L"BUTTON", L"Cancel", BS_OWNERDRAW | WS_TABSTOP, IDCANCEL, 244, y + 52, 100, 34);
        HWND accept = dialogs::add(window, ui, L"BUTTON", state->mode == Mode::unlock ? L"Unlock" : L"Save",
            BS_OWNERDRAW | WS_TABSTOP, IDOK, 356, y + 52, 108, 34);
        for (HWND button : {cancel, accept})
            SetWindowSubclass(button, inputProc, 1, reinterpret_cast<DWORD_PTR>(state));
        SendMessageW(window, DM_SETDEFID, IDOK, 0);
        SetFocus(state->oldPassword ? state->oldPassword : state->newPassword);
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
    if (message == WM_CTLCOLORDLG || message == WM_CTLCOLORSTATIC || message == WM_CTLCOLOREDIT) {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const bool field = message == WM_CTLCOLOREDIT;
        SetTextColor(dc, ui.style.text);
        SetBkColor(dc, field ? ui.style.field : ui.style.background);
        return reinterpret_cast<INT_PTR>(field ? ui.field : ui.background);
    }
    if (message == WM_DRAWITEM) {
        dialogs::drawButton(ui.style, *reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    }
    if (message == WM_CLOSE || (message == WM_COMMAND && LOWORD(wParam) == IDCANCEL)) {
        clearFields(*state);
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE) {
        SendMessageW(reinterpret_cast<HWND>(lParam), EM_EMPTYUNDOBUFFER, 0, 0);
        return TRUE;
    }
    if (message == WM_COMMAND && LOWORD(wParam) == IDOK) {
        encrypted::Result result = encrypted::Result::cryptoError;
        {
            secure::Text old = state->oldPassword ? secure::Text::read(state->oldPassword) : secure::Text{};
            secure::Text next = state->newPassword ? secure::Text::read(state->newPassword) : secure::Text{};
            secure::Text confirm =
                state->confirmation ? secure::Text::read(state->confirmation) : secure::Text{};
            clearFields(*state);
            if ((state->mode != Mode::create && old.empty()) ||
                (state->mode != Mode::unlock && (next.size() < 12 || !secure::equal(next, confirm)))) {
                SetWindowTextW(state->error,
                    state->mode != Mode::create && old.empty() ? L"Enter your current master password."
                    : next.size() < 12 ? L"Use a master password of at least 12 characters."
                                       : L"The new passwords do not match. Please enter them again.");
                SetFocus(state->oldPassword ? state->oldPassword : state->newPassword);
                return TRUE;
            }
            confirm.clear(); // Confirmation is no longer needed during key derivation or file updates.
            HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
            try {
                if (state->action)
                    result = state->action(old, next);
            } catch (...) {
                result = encrypted::Result::cryptoError;
            }
            SetCursor(previous);
        } // Password buffers are wiped before closing or displaying an error.
        if (result == encrypted::Result::ok)
            EndDialog(window, IDOK);
        else {
            SetWindowTextW(state->error, errorText(result));
            SetFocus(state->oldPassword ? state->oldPassword : state->newPassword);
        }
        return TRUE;
    }
    if (message == WM_DESTROY)
        clearFields(*state);
    return FALSE;
}

inline bool show(HWND owner, State& state) {
    struct alignas(DWORD) Template {
        DLGTEMPLATE dialog;
        WORD menu = 0, windowClass = 0, title = 0;
    } value{};

    value.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    value.dialog.cx = 300;
    value.dialog.cy = 220;
    return DialogBoxIndirectParamW(
               state.ui.style.instance, &value.dialog, owner, proc, reinterpret_cast<LPARAM>(&state)) == IDOK;
}
} // namespace passwords
