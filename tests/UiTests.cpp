// Exercise real native controls without sending input to any desktop application.
#include "../AutoTyper/AutoTyper.cpp"
#include <cstdio>
#include <stdexcept>

void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

enum class Probe {
    picker,
    empty,
    shortcutCancel,
    shortcutApply,
    search,
    searchEmpty,
    searchMouse,
    password,
    nameDialog,
    nameCancel,
    protectedSave,
    protectedLoad,
    convert,
    cancelConvert
};
Probe probe;
bool probePassed;
HWND messageOwner;
int messageAction = VK_RETURN;
bool messagePassed = false, messageLong = false, messageRefresh = false;
settings::ThemeMode pickedTheme = settings::ThemeMode::system;
bool acceptTheme = true, themePassed = false;
RECT selectorBounds{};
int protectedStage = 0, unlockPrompts = 0;

void CALLBACK inspectMessage(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = nullptr;
    // GetLastActivePopup does not find nested dialogs when its argument is owned.
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND candidate, LPARAM data) -> BOOL {
            if (GetWindow(candidate, GW_OWNER) == messageOwner && IsWindowVisible(candidate) &&
                GetPropW(candidate, dialogs::ThemeProperty)) {
                *reinterpret_cast<HWND*>(data) = candidate;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&dialog));
    if (!dialog)
        return;
    KillTimer(nullptr, timer);
    const auto& state = *reinterpret_cast<dialogs::MessageState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    const HWND body = FindWindowExW(dialog, nullptr, L"EDIT", nullptr);
    const UINT type = state.flags & MB_TYPEMASK;
    LOGBRUSH brush{};
    GetObjectW(state.ui.background, sizeof(brush), &brush);
    messagePassed = dialogOpen && !IsWindowEnabled(messageOwner) && body && textOf(body) == state.text &&
                    (GetWindowLongPtrW(body, GWL_STYLE) & ES_READONLY) && brush.lbColor == colors.panel &&
                    state.ui.style.dark == darkTheme &&
                    (LOWORD(SendMessageW(dialog, DM_GETDEFID, 0, 0)) == state.primary) &&
                    (GetFocus() == GetDlgItem(dialog, state.primary)) &&
                    (type == MB_OK ? GetDlgItem(dialog, IDYES) == nullptr
                                   : GetDlgItem(dialog, IDYES) && GetDlgItem(dialog, IDNO)) &&
                    (type == MB_YESNOCANCEL ? GetDlgItem(dialog, IDCANCEL) != nullptr
                                            : GetDlgItem(dialog, IDCANCEL) == nullptr);
    if (messageLong)
        messagePassed = messagePassed && (GetWindowLongPtrW(body, GWL_STYLE) & WS_VSCROLL);
    RECT dialogBounds{}, buttonBounds{};
    GetClientRect(dialog, &dialogBounds);
    GetWindowRect(GetDlgItem(dialog, state.primary), &buttonBounds);
    MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&buttonBounds), 2);
    messagePassed = messagePassed && buttonBounds.bottom <= dialogBounds.bottom &&
                    buttonBounds.right <= dialogBounds.right;
    if (messageRefresh) {
        preferences.theme = darkTheme ? settings::ThemeMode::light : settings::ThemeMode::dark;
        applyTheme();
        GetObjectW(state.ui.background, sizeof(brush), &brush);
        messagePassed = messagePassed && brush.lbColor == colors.panel && state.ui.style.dark == darkTheme &&
                        textOf(body) == state.text;
    }
    toggleTypingShortcut(); // A modal message must not trigger clipboard typing.
    messagePassed = messagePassed && !typing;
    if (messageAction == VK_RETURN || messageAction == VK_ESCAPE) {
        MSG key{};
        key.hwnd = GetFocus();
        key.message = WM_KEYDOWN;
        key.wParam = messageAction;
        IsDialogMessageW(dialog, &key);
    } else if (!messageAction)
        SendMessageW(dialog, WM_CLOSE, 0, 0);
    else
        SendMessageW(GetDlgItem(dialog, messageAction), BM_CLICK, 0, 0);
}

void testEditorChangeConfirmation() {
    const auto checkPrompt = [&](const std::function<void()>& action) {
        messageOwner = window;
        messageAction = IDCANCEL;
        messageLong = messageRefresh = messagePassed = false;
        const UINT_PTR timer = SetTimer(nullptr, 0, 20, inspectMessage);
        require(timer != 0, "Save prompt inspection timer failed");
        action();
        KillTimer(nullptr, timer);
    };
    for (bool encrypted : {false, true}) {
        selectedSnippet = L"Saved fixture";
        selectedEncrypted = encrypted;
        SendMessageW(encryptSnippetCheck, BM_SETCHECK, encrypted ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(contentEdit, L"Text removed by the user");
        SendMessageW(contentEdit, EM_SETSEL, 0, -1);
        SendMessageW(contentEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L""));
        require(GetWindowTextLengthW(contentEdit) == 0 && !editorModified() &&
                    textOf(window) == L"MK AutoTyper - Saved fixture",
            "Empty modified editor fixture failed");
        checkPrompt([&] { require(confirmEditorChange(), "Empty editor requested a save"); });
        require(!messagePassed, "Empty modified editor displayed a save prompt");
        SendMessageW(encryptSnippetCheck, BM_CLICK, 0, 0);
        require(!editorModified() && textOf(window) == L"MK AutoTyper - Saved fixture",
            "Empty editor encryption toggle displayed an unsaved title marker");
        checkPrompt([&] { newSnippet(); });
        require(!messagePassed && selectedSnippet.empty() && !selectedEncrypted && !encryptionChecked() &&
                    !editorModified(),
            "New prompted for empty text or failed to reset the editor");
    }
    for (const auto* text : {L"Unsaved text", L" \t\r\n"}) {
        SetWindowTextW(contentEdit, text);
        SendMessageW(contentEdit, EM_SETMODIFY, TRUE, 0);
        updateEditorActions();
        const auto before = textOf(contentEdit);
        require(textOf(window) == L"MK AutoTyper *", "Nonempty edits lost the title marker");
        checkPrompt([&] { require(!confirmEditorChange(), "Save prompt cancellation was ignored"); });
        require(messagePassed && textOf(contentEdit) == before && editorModified(),
            "Nonempty edited text lost its save prompt or contents");
    }
    SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
    checkPrompt([&] { require(confirmEditorChange(), "Unmodified text requested a save"); });
    require(!messagePassed, "Unmodified text displayed a save prompt");
    SetWindowTextW(contentEdit, L"");
    selectedSnippet = L"Protected close fixture";
    selectedEncrypted = true;
    SendMessageW(encryptSnippetCheck, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(contentEdit, EM_SETMODIFY, TRUE, 0);
    const bool visible = IsWindowVisible(window) != FALSE;
    checkPrompt([&] { SendMessageW(window, WM_CLOSE, 0, 0); });
    require(!messagePassed && selectedSnippet.empty() && !selectedEncrypted && !encryptionChecked() &&
                !editorModified(),
        "Closing an empty protected editor prompted or skipped cleanup");
    if (visible)
        ShowWindow(window, SW_SHOWNOACTIVATE);
    SendMessageW(contentEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
}

BOOL WINAPI inspectThemeMenu(HMENU menu, UINT flags, int x, int y, HWND owner, LPTPMPARAMS position) {
    MENUINFO info{sizeof(info)};
    info.fMask = MIM_BACKGROUND;
    LOGBRUSH brush{};
    RECT anchor{};
    GetWindowRect(themeButton, &anchor);
    themePassed = dialogOpen && owner == window && activeThemeMenu == menu && IsWindowEnabled(window) &&
                  GetMenuItemCount(menu) == 3 && GetMenuInfo(menu, &info) &&
                  GetObjectW(info.hbrBack, sizeof(brush), &brush) && brush.lbColor == colors.panel &&
                  x == anchor.right && y == anchor.bottom + scale(4) && position &&
                  EqualRect(&position->rcExclude, &anchor) && (flags & TPM_RETURNCMD) &&
                  (flags & TPM_RIGHTALIGN);
    for (UINT i = 0; i < 3; ++i) {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_ID | MIIM_DATA | MIIM_STATE | MIIM_FTYPE;
        const bool found = GetMenuItemInfoW(menu, i, TRUE, &item) != FALSE;
        themePassed = themePassed && found && item.wID == dialogs::ThemeSystem + i &&
                      (item.fType & MFT_OWNERDRAW) &&
                      ((item.fState & MFS_CHECKED) != 0) == (static_cast<UINT>(preferences.theme) == i);
        const auto* name = reinterpret_cast<const MSAAMENUINFO*>(item.dwItemData);
        themePassed = themePassed && name && name->dwMSAASignature == MSAA_MENU_SIG &&
                      wcscmp(name->pszWText, dialogs::themeChoiceLabel(dialogs::ThemeSystem + i)) == 0;
        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemID = dialogs::ThemeSystem + i;
        SendMessageW(window, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure));
        themePassed = themePassed && measure.itemWidth == static_cast<UINT>(scale(192)) &&
                      measure.itemHeight == static_cast<UINT>(scale(36));
    }
    return acceptTheme ? dialogs::ThemeSystem + static_cast<int>(pickedTheme) : 0;
}

void CALLBACK inspectInvalidShortcut(HWND, UINT, UINT_PTR timer, DWORD) {
    const HWND dialog = GetLastActivePopup(window);
    if (dialog == window || GetWindow(dialog, GW_OWNER) != window)
        return;
    KillTimer(nullptr, timer);
    SendMessageW(GetDlgItem(dialog, 500), HKM_SETHOTKEY, 0, 0);
    messageOwner = dialog;
    messageAction = VK_RETURN;
    messageLong = messageRefresh = false;
    messagePassed = false;
    SetTimer(nullptr, 0, 20, inspectMessage);
    SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    messagePassed = messagePassed && IsWindow(dialog) && IsWindowEnabled(dialog);
    SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
}

void testThemedMessages() {
    const UINT originalDpi = dpi;
    for (UINT testDpi : {96U, 144U}) {
        dpi = testDpi;
        createFonts();
        for (bool dark : {false, true}) {
            preferences.theme = dark ? settings::ThemeMode::dark : settings::ThemeMode::light;
            applyTheme();
            const auto checkMessage = [&](UINT flags, int action, int expected) {
                messageOwner = window;
                messageAction = action;
                messagePassed = false;
                SetTimer(nullptr, 0, 20, inspectMessage);
                const int result = showMessage(window,
                    L"A themed message with German \u00e4\u00f6\u00fc\u00df & symbols.\nSecond line.",
                    L"Message fixture", flags);
                require(result == expected && messagePassed && !dialogOpen,
                    "Themed message buttons, focus, theme or modal routing failed");
            };
            messageLong = messageRefresh = false;
            checkMessage(MB_OK | MB_ICONERROR, VK_RETURN, IDOK);
            checkMessage(MB_YESNO | MB_DEFBUTTON2, VK_RETURN, IDNO);
            checkMessage(MB_YESNO, IDYES, IDYES);
            checkMessage(MB_YESNO, VK_ESCAPE, IDNO);
            checkMessage(MB_YESNOCANCEL, VK_ESCAPE, IDCANCEL);
            checkMessage(MB_YESNOCANCEL, VK_RETURN, IDYES);
            checkMessage(MB_YESNOCANCEL, 0, IDCANCEL);
            messageRefresh = true;
            checkMessage(MB_OK, VK_RETURN, IDOK);
            messageRefresh = false;
            messageLong = true;
            std::wstring longText;
            for (int i = 0; i < 150; ++i)
                longText += L"Long wrapped message for scrolling. ";
            messageOwner = window;
            messageAction = VK_RETURN;
            SetTimer(nullptr, 0, 20, inspectMessage);
            require(showMessage(window, longText.c_str(), L"Long message", MB_OK) == IDOK && messagePassed,
                "Long themed message clipped its buttons or did not scroll");
            messageLong = false;
        }
    }
    dpi = originalDpi;
    createFonts();
    applyTheme();
    layout();
    for (UINT testDpi : {96U, 144U}) {
        dpi = testDpi;
        createFonts();
        layout();
        for (bool dark : {false, true}) {
            preferences.theme = dark ? settings::ThemeMode::dark : settings::ThemeMode::light;
            applyTheme();
            for (auto mode :
                {settings::ThemeMode::light, settings::ThemeMode::dark, settings::ThemeMode::system}) {
                pickedTheme = mode;
                acceptTheme = true;
                chooseTheme(inspectThemeMenu);
                require(themePassed && !activeThemeMenu && !dialogOpen && preferences.theme == mode &&
                            textOf(themeButton) == settings::themeLabel(mode) &&
                            darkTheme == settings::usesDarkTheme(mode),
                    "Theme dropdown layout, accessibility, checkmark or selection failed");
            }
            acceptTheme = false;
            pickedTheme = settings::ThemeMode::dark;
            chooseTheme(inspectThemeMenu);
            require(themePassed && preferences.theme == settings::ThemeMode::system,
                "Dismissing the theme dropdown changed the preference");
        }
    }
    dpi = originalDpi;
    createFonts();
    layout();
    applyTheme();
    darkTheme = !settings::systemUsesDarkTheme();
    SendMessageW(window, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
    require(darkTheme == settings::systemUsesDarkTheme(),
        "Windows theme notification did not refresh system mode");
    preferences.theme = settings::ThemeMode::dark;
    applyTheme();
    SendMessageW(window, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
    require(darkTheme, "Windows theme notification overrode the explicit theme");
}

bool caretMatches(HWND field) {
    SetFocus(field);
    positionHotkeyCaret(field);
    POINT caret{};
    if (!GetCaretPos(&caret))
        return false;
    HDC dc = GetDC(field);
    HGDIOBJ old = SelectObject(dc, bodyFont);
    const auto text = hotkeyDisplayText(field);
    SIZE size{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size);
    SelectObject(dc, old);
    ReleaseDC(field, dc);
    return caret.x == scale(10) + size.cx;
}

void CALLBACK inspectDialog(HWND, UINT, UINT_PTR timer, DWORD) {
    const HWND dialog = GetLastActivePopup(window);
    if (dialog == window || GetWindow(dialog, GW_OWNER) != window)
        return;
    KillTimer(nullptr, timer);
    const HWND field = GetDlgItem(dialog, 500);
    if (probe == Probe::picker) {
        probePassed =
            SendMessageW(field, LB_GETCOUNT, 0, 0) == 3 && SendMessageW(field, LB_GETCURSEL, 0, 0) == 1;
        SendMessageW(field, WM_KEYDOWN, VK_DOWN, 0);
        probePassed = probePassed && SendMessageW(field, LB_GETCURSEL, 0, 0) == 2;
        SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(500, LBN_DBLCLK), reinterpret_cast<LPARAM>(field));
    } else if (probe == Probe::empty) {
        probePassed = !IsWindowEnabled(GetDlgItem(dialog, IDOK));
        SendMessageW(dialog, WM_COMMAND, IDOK, 0); // Empty selection must not close the dialog.
        probePassed = probePassed && IsWindow(dialog);
        SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
    } else if (probe == Probe::search || probe == Probe::searchEmpty || probe == Probe::searchMouse) {
        auto& search = *reinterpret_cast<snippetsearch::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        GetWindowRect(dialog, &selectorBounds);
        SendMessageW(search.search, WM_KEYDOWN, VK_RETURN, 0x40000000);
        SendMessageW(search.search, WM_KEYUP, VK_RETURN, 0);
        RECT bounds;
        GetClientRect(search.list, &bounds);
        probePassed =
            !search.closing && bounds.bottom == 4 * SendMessageW(search.list, LB_GETITEMHEIGHT, 0, 0);
        if (probe == Probe::searchEmpty) {
            SetWindowTextW(search.search, L"no such snippet");
            probePassed = probePassed && search.filtered.empty() &&
                          SendMessageW(search.list, LB_GETCURSEL, 0, 0) == LB_ERR;
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
            probePassed = probePassed && IsWindow(dialog);
            SetWindowTextW(search.search, L"");
            probePassed = probePassed && search.filtered.size() == 7 &&
                          SendMessageW(search.list, LB_GETCURSEL, 0, 0) == 0;
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        } else {
            probePassed = probePassed && SendMessageW(search.list, LB_GETCOUNT, 0, 0) == 7 &&
                          SendMessageW(search.list, LB_GETCURSEL, 0, 0) == 0;
            SendMessageW(search.search, WM_KEYDOWN, VK_NEXT, 0);
            probePassed = probePassed && SendMessageW(search.list, LB_GETCURSEL, 0, 0) == 4 &&
                          SendMessageW(search.list, LB_GETTOPINDEX, 0, 0) > 0;
            SetWindowTextW(search.search, L"\u00c4L");
            const auto previousTheme = preferences.theme;
            preferences.theme = darkTheme ? settings::ThemeMode::light : settings::ThemeMode::dark;
            applyTheme();
            probePassed =
                probePassed && textOf(search.search) == L"\u00c4L" && search.ui.style.dark == darkTheme;
            preferences.theme = previousTheme;
            applyTheme();
            probePassed = probePassed && search.filtered == std::vector<size_t>({1, 3}) &&
                          SendMessageW(search.list, LB_GETCURSEL, 0, 0) == 0;
            if (probe == Probe::searchMouse) {
                SendMessageW(search.list, LB_SETCURSEL, 1, 0);
                SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(snippetsearch::Results, LBN_DBLCLK),
                    reinterpret_cast<LPARAM>(search.list));
            } else {
                SendMessageW(search.search, WM_KEYDOWN, VK_DOWN, 0);
                SendMessageW(search.search, WM_KEYDOWN, VK_RETURN, 0);
                SendMessageW(search.search, WM_KEYUP, VK_RETURN, 0);
            }
        }
    } else if (probe == Probe::protectedSave || probe == Probe::protectedLoad) {
        if (protectedStage++ == 0) {
            probePassed = true;
            if (probe == Probe::protectedSave) {
                SetWindowTextW(field, L"Protected fixture");
                probePassed = GetDlgItem(dialog, 506) == nullptr;
            } else {
                const auto& picker = *reinterpret_cast<dialogs::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
                probePassed = picker.names.size() == 1 && picker.encrypted.size() == 1 && picker.encrypted[0];
                SendMessageW(field, LB_SETCURSEL, 0, 0);
            }
            SetTimer(nullptr, 0, 20, inspectDialog);
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        } else {
            probePassed = probePassed && !shortcut.registered() && !snippetShortcut.registered();
            SetWindowTextW(GetDlgItem(dialog, 700), L"native fixture password 123");
            if (probe == Probe::protectedSave)
                SetWindowTextW(GetDlgItem(dialog, 701), L"native fixture password 123");
            else
                ++unlockPrompts;
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        }
    } else if (probe == Probe::convert || probe == Probe::cancelConvert) {
        const auto& state = *reinterpret_cast<passwords::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        probePassed =
            state.mode == passwords::Mode::unlock && !shortcut.registered() && !snippetShortcut.registered();
        if (probe == Probe::cancelConvert)
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        else {
            SetWindowTextW(state.oldPassword, L"native fixture password 123");
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        }
    } else if (probe == Probe::password) {
        auto& state = *reinterpret_cast<passwords::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        SendMessageW(
            state.oldPassword ? state.oldPassword : state.newPassword, WM_KEYDOWN, VK_RETURN, 0x40000000);
        SendMessageW(state.oldPassword ? state.oldPassword : state.newPassword, WM_KEYUP, VK_RETURN, 0);
        RECT actual{}, owner{};
        GetWindowRect(dialog, &actual);
        GetWindowRect(window, &owner);
        const LONG expectedX =
            state.placement ? state.placement->left
                            : owner.left + (owner.right - owner.left - (actual.right - actual.left)) / 2;
        const LONG expectedY =
            state.placement ? state.placement->top
                            : owner.top + (owner.bottom - owner.top - (actual.bottom - actual.top)) / 2;
        probePassed =
            GetWindowTextLengthW(state.error) == 0 && actual.left == expectedX && actual.top == expectedY;
        const auto fill = [&] {
            for (HWND edit : {state.oldPassword, state.newPassword, state.confirmation})
                if (edit) {
                    probePassed = probePassed && (GetWindowLongPtrW(edit, GWL_STYLE) & ES_PASSWORD);
                    SetWindowTextW(edit, L"fixture master password");
                    probePassed = probePassed && !SendMessageW(edit, EM_CANUNDO, 0, 0);
                }
        };
        fill();
        const auto previousTheme = preferences.theme;
        preferences.theme = darkTheme ? settings::ThemeMode::light : settings::ThemeMode::dark;
        applyTheme();
        probePassed = probePassed && state.ui.style.dark == darkTheme;
        for (HWND edit : {state.oldPassword, state.newPassword, state.confirmation})
            if (edit)
                probePassed = probePassed && GetWindowTextLengthW(edit) == 23;
        preferences.theme = previousTheme;
        applyTheme();
        SendMessageW(GetDlgItem(dialog, IDOK), WM_KEYDOWN, VK_RETURN, 0);
        SendMessageW(GetDlgItem(dialog, IDOK), WM_KEYUP, VK_RETURN, 0);
        if (state.mode ==
            passwords::Mode::unlock) { // Deliberately fails once, then retries without cached input.
            for (HWND edit : {state.oldPassword, state.newPassword, state.confirmation})
                if (edit)
                    probePassed = probePassed && GetWindowTextLengthW(edit) == 0;
            fill();
            SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        }
    } else if (probe == Probe::nameDialog || probe == Probe::nameCancel) {
        const auto& state = *reinterpret_cast<dialogs::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        probePassed =
            GetDlgItem(dialog, 506) == nullptr &&
            (IsWindowEnabled(GetDlgItem(dialog, IDOK)) != FALSE) == snippets::validName(state.name) &&
            textOf(state.nameCounter) == std::to_wstring(state.name.size()) + L" / 80";
        SetWindowTextW(field, L"\u00c4pfel & Gr\u00fc\u00dfe");
        SendMessageW(field, EM_SETSEL, 3, 3);
        BYTE before[256], keys[256];
        GetKeyboardState(before);
        CopyMemory(keys, before, sizeof(keys));
        keys[VK_CONTROL] = keys[VK_LCONTROL] = keys[VK_RCONTROL] = 0;
        keys[VK_MENU] = keys[VK_LMENU] = keys[VK_RMENU] = 0;
        SetKeyboardState(keys);
        SendMessageW(field, WM_KEYDOWN, 'A', 0);
        DWORD start = 0, end = 0;
        SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        probePassed = probePassed && start == 3 && end == 3;
        keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
        SetKeyboardState(keys);
        SendMessageW(field, WM_KEYDOWN, 'A', 0);
        SendMessageW(field, WM_CHAR, 1, 0);
        SendMessageW(field, WM_KEYUP, 'A', 0);
        SetKeyboardState(before);
        SendMessageW(field, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        probePassed =
            probePassed && start == 0 && end == 13 && textOf(field) == L"\u00c4pfel & Gr\u00fc\u00dfe";
        SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Replacement"));
        probePassed = probePassed && textOf(field) == L"Replacement" &&
                      textOf(state.nameCounter) == L"11 / 80" && IsWindowEnabled(GetDlgItem(dialog, IDOK));
        for (const auto* name :
            {L"<>:\"/\\|?*", L"NUL.txt", L"LPT\u00b2", L"Name.", L"Name ", L"\xd83d\xde00 \u4e2d\u6587"}) {
            SetWindowTextW(field, name);
            probePassed = probePassed && IsWindowEnabled(GetDlgItem(dialog, IDOK)) &&
                          state.nameProblem == snippets::NameIssue::none;
        }
        const std::vector<std::wstring> invalid = {L"", std::wstring(1, 0xd800), std::wstring(81, L'x')};
        for (const auto& name : invalid) {
            SetWindowTextW(field, name.c_str());
            probePassed = probePassed && !IsWindowEnabled(GetDlgItem(dialog, IDOK)) &&
                          state.nameProblem == snippets::nameIssue(name) &&
                          GetWindowTextLengthW(state.nameFeedback) > 0;
            SendMessageW(dialog, WM_COMMAND, IDOK, 0); // Even direct dispatch cannot bypass validation.
            probePassed = probePassed && IsWindow(dialog);
        }
        SetWindowTextW(field, std::wstring(80, L'x').c_str());
        probePassed = probePassed && IsWindowEnabled(GetDlgItem(dialog, IDOK));
        SendMessageW(field, EM_SETSEL, 80, 80);
        SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"x"));
        probePassed = probePassed && GetWindowTextLengthW(field) == 81 &&
                      textOf(state.nameCounter) == L"81 / 80" && !IsWindowEnabled(GetDlgItem(dialog, IDOK)) &&
                      state.nameProblem == snippets::NameIssue::tooLong;
        HDC dc = GetDC(dialog);
        SendMessageW(dialog, WM_CTLCOLORSTATIC, reinterpret_cast<WPARAM>(dc),
            reinterpret_cast<LPARAM>(state.nameFeedback));
        probePassed =
            probePassed && GetTextColor(dc) == (state.style.dark ? RGB(255, 160, 160) : RGB(176, 32, 48));
        ReleaseDC(dialog, dc);
        if (probe == Probe::nameCancel) {
            SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            return;
        }
        SetWindowTextW(field, L"\u00c4pfel & Gr\u00fc\u00dfe");
        probePassed = probePassed && IsWindowEnabled(GetDlgItem(dialog, IDOK)) &&
                      state.nameProblem == snippets::NameIssue::none;
        SetWindowTextW(field, L"Protected fixture");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    } else {
        probePassed = !shortcut.registered() && !snippetShortcut.registered();
        BYTE before[256], keys[256];
        GetKeyboardState(before);
        CopyMemory(keys, before, sizeof(keys));
        keys[VK_CONTROL] = keys[VK_LCONTROL] = keys[VK_MENU] = keys[VK_LMENU] = 0x80;
        SetKeyboardState(keys);
        SendMessageW(field, WM_KEYDOWN, 'V', 0);
        SendMessageW(field, WM_KEYUP, 'V', 0);
        SetKeyboardState(before);
        probePassed = probePassed && SendMessageW(field, HKM_GETHOTKEY, 0, 0) ==
                                         MAKEWORD('V', HOTKEYF_CONTROL | HOTKEYF_ALT);
        // Apply the fixture's own shortcut so a running app cannot conflict with this test.
        SendMessageW(
            field, HKM_SETHOTKEY, MAKEWORD(VK_F24, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT), 0);
        const HWND snippetField = GetDlgItem(dialog, 504);
        SendMessageW(snippetField, WM_KEYDOWN, 'P', 0);
        SendMessageW(snippetField, WM_KEYUP, 'P', 0);
        probePassed = probePassed && SendMessageW(snippetField, HKM_GETHOTKEY, 0, 0) == MAKEWORD('P', 0);
        SendMessageW(
            snippetField, HKM_SETHOTKEY, MAKEWORD(VK_F23, HOTKEYF_CONTROL | HOTKEYF_ALT | HOTKEYF_SHIFT), 0);
        probePassed = probePassed && caretMatches(field) && caretMatches(snippetField);
        SendMessageW(dialog, WM_COMMAND, probe == Probe::shortcutApply ? IDOK : IDCANCEL, 0);
    }
}

void startProbe(Probe value) {
    probe = value;
    probePassed = false;
    protectedStage = 0;
    require(SetTimer(nullptr, 0, 20, inspectDialog) != 0, "Dialog probe timer failed");
}

int main() {
    HMODULE rich = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    try {
        require(rich != nullptr, "System Rich Edit unavailable");
        instance = GetModuleHandleW(nullptr);
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
        InitCommonControlsEx(&common);
        preferences = settings::Preferences{};
        WNDCLASSW wc = {};
        wc.lpfnWndProc = windowProc;
        wc.hInstance = instance;
        wc.lpszClassName = WindowClass;
        require(RegisterClassW(&wc) != 0, "Register failed");
        window = CreateWindowW(WindowClass, L"AutoTyper UI tests", WS_OVERLAPPEDWINDOW, 0, 0, 1040, 760,
            nullptr, nullptr, instance, nullptr);
        require(window != nullptr, "Window creation failed");
        secure::Text clipboard;
        const wchar_t unterminated[] = {L'a', L'b'};
        require(!copyClipboardText(unterminated, 2, clipboard) && clipboard.empty(),
            "Unterminated clipboard text accepted");
        require(copyClipboardText(L"fixture", 8, clipboard) && clipboard.size() == 7,
            "Bounded clipboard copy failed");
        require(!copyClipboardText(nullptr, 0, clipboard) && clipboard.empty(),
            "Failed clipboard read retained old plaintext");
        std::wstring excessiveClipboard(1024 * 1024 + 1, L'x');
        require(!copyClipboardText(excessiveClipboard.c_str(), excessiveClipboard.size() + 1, clipboard),
            "Oversized clipboard text accepted");
        require(!encryptionChecked() &&
                    (GetWindowLongPtrW(encryptSnippetCheck, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX,
            "Encryption checkbox missing or checked by default");
        require(SendMessageW(scanRadio, BM_GETCHECK, 0, 0) == BST_CHECKED, "Scan-code default changed");
        require(SendMessageW(unicodeRadio, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "Unicode default changed");
        require(textOf(scanRadio) == L"&Scan-code input" && textOf(unicodeRadio) == L"&Unicode input",
            "Mode names wrong");
        const LRESULT mode = SendMessageW(contentEdit, EM_GETTEXTMODE, 0, 0);
        require((mode & TM_PLAINTEXT) && (mode & TM_MULTILEVELUNDO), "Plain text or undo mode missing");
        SetWindowTextW(contentEdit, L"Hello world");
        BYTE before[256], state[256];
        GetKeyboardState(before);
        CopyMemory(state, before, sizeof(state));
        state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
        SetKeyboardState(state);
        SendMessageW(contentEdit, WM_KEYDOWN, 'A', 0);
        SetKeyboardState(before);
        CHARRANGE selection;
        SendMessageW(contentEdit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        require(selection.cpMin == 0 && selection.cpMax == 11, "Ctrl+A did not select all");
        SendMessageW(contentEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Replacement"));
        require(textOf(contentEdit) == L"Replacement", "Selection replacement failed");
        SendMessageW(contentEdit, EM_UNDO, 0, 0);
        require(textOf(contentEdit) == L"Hello world", "Undo failed");
        SendMessageW(contentEdit, EM_REDO, 0, 0);
        require(textOf(contentEdit) == L"Replacement", "Redo failed");
        SetWindowTextW(contentEdit, L"First\rSecond \u00e4\u00f6\u00fc\u00df");
        require((GetWindowLongPtrW(contentEdit, GWL_STYLE) & ES_MULTILINE) != 0 &&
                    textOf(contentEdit).find_first_of(L"\r\n") != std::wstring::npos,
            "Multiline text lost");
        require(
            textOf(contentEdit).find(L"\u00e4\u00f6\u00fc\u00df") != std::wstring::npos, "German text lost");
        for (bool dark : {true, false}) {
            preferences.theme = dark ? settings::ThemeMode::dark : settings::ThemeMode::light;
            applyTheme();
            require(
                textOf(themeButton) == settings::themeLabel(preferences.theme), "Theme toggle label wrong");
            require(textOf(contentEdit).find(L"\u00e4\u00f6\u00fc\u00df") != std::wstring::npos,
                "Theme change altered text");
            CHARFORMAT2W format = {};
            format.cbSize = sizeof(format);
            SendMessageW(contentEdit, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
            require(format.crTextColor == colors.text, "Editor theme foreground wrong");
        }
        const std::wstring original = textOf(contentEdit);
        SendMessageW(contentEdit, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(contentEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" appended"));
        preferences.theme = settings::ThemeMode::dark;
        applyTheme();
        SendMessageW(contentEdit, EM_UNDO, 0, 0);
        require(textOf(contentEdit) == original, "Theme switch interfered with undo");
        SetWindowTextW(delayEdit, L"1");
        require(delayOf(delayEdit, 10, 2) == 2, "Delay minimum not enforced");
        SetWindowTextW(delayEdit, L"oops");
        require(delayOf(delayEdit, 10, 2) == 10, "Invalid delay not handled");
        SetWindowTextW(initialDelayEdit, L"0");
        require(delayOf(initialDelayEdit, 1000, 0) == 0, "Zero initial delay rejected");
        SetWindowPos(window, nullptr, 0, 0, scale(900), scale(690), SWP_NOMOVE | SWP_NOZORDER);
        RECT field;
        GetWindowRect(initialDelayEdit, &field);
        POINT bottom{field.right, field.bottom};
        ScreenToClient(window, &bottom);
        require(bottom.y < client().bottom - scale(96), "Settings clipped at minimum size");
        initializing = true; // No real user preferences or clipboard are touched.
        testEditorChangeConfirmation();
        SetFocus(contentEdit);
        const auto destination = typing_target::Destination::capture(window);
        require(destination.valid() && destination.window == window && destination.focus == contentEdit,
            "Typing destination capture failed");
        const HWND temporary =
            CreateWindowW(L"EDIT", L"", WS_CHILD, 0, 0, 20, 20, window, nullptr, instance, nullptr);
        const typing_target::Destination destroyed{window, temporary, GetCurrentThreadId()};
        DestroyWindow(temporary);
        require(!destroyed.valid(), "Destroyed destination field accepted");
        require(!typing_target::Destination{}.restore(), "Missing destination accepted");
        require(snippetsearch::contains(L"Hello World", L"LO wo") &&
                    snippetsearch::contains(L"\u00c4pfel & \u00d6l", L"\u00e4PF") &&
                    !snippetsearch::contains(L"Hello", L"longer query") &&
                    snippetsearch::contains(L"Hello", L""),
            "Case-insensitive substring matching failed");
        for (bool dark : {true, false}) {
            preferences.theme = dark ? settings::ThemeMode::dark : settings::ThemeMode::light;
            applyTheme();
            dialogs::State picker;
            picker.style = dialogStyle();
            picker.picker = true;
            picker.title = L"Snippet picker tests";
            picker.names = {L"First", L"Current", L"German & \u00e4"};
            picker.name = L"Current";
            picker.encrypted = {false, false, true};
            startProbe(Probe::picker);
            require(dialogs::show(window, picker) == IDOK && probePassed &&
                        picker.name == L"German & \u00e4" && picker.encrypt,
                "Encrypted snippet selection or keyboard navigation failed");
            picker.names.clear();
            picker.name.clear();
            startProbe(Probe::empty);
            require(dialogs::show(window, picker) == IDCANCEL && probePassed,
                "Empty snippet picker accepted a selection");
            for (Probe value : {Probe::search, Probe::searchEmpty, Probe::searchMouse}) {
                snippetsearch::State search;
                search.ui.style = dialogStyle();
                search.target = window;
                search.ui.names = {
                    L"Alpha", L"\u00c4ltere", L"Bravo", L"Test \u00e4ltere", L"Charlie", L"Delta", L"Echo"};
                search.ui.encrypted = {false, false, false, true, false, false, false};
                startProbe(value);
                const INT_PTR result = snippetsearch::show(window, search);
                require(
                    EqualRect(&search.bounds, &selectorBounds), "Search did not retain its screen position");
                require(probePassed && (value == Probe::searchEmpty
                                               ? result == IDCANCEL
                                               : result == IDOK && search.ui.name == L"Test \u00e4ltere" &&
                                                     search.ui.encrypt),
                    "Search filtering, encrypted selection, scrolling or empty state failed");
            }
            dialogs::State save;
            save.style = dialogStyle();
            startProbe(Probe::nameDialog);
            require(dialogs::show(window, save) == IDOK && probePassed && save.name == L"Protected fixture",
                "Snippet naming dialog failed");
            save.title = L"Rename snippet";
            save.name = L"Original";
            startProbe(Probe::nameCancel);
            require(dialogs::show(window, save) == IDCANCEL && probePassed && save.name == L"Original",
                "Rename validation or cancellation changed the original name");
            for (auto dialogMode :
                {passwords::Mode::unlock, passwords::Mode::create, passwords::Mode::change}) {
                passwords::State password;
                password.ui.style = dialogStyle();
                password.mode = dialogMode;
                int calls = 0;
                if (dialogMode == passwords::Mode::unlock)
                    password.placement = &selectorBounds;
                password.action = [&](const secure::Text& old, const secure::Text& next) {
                    ++calls;
                    for (HWND edit : {password.oldPassword, password.newPassword, password.confirmation})
                        if (edit)
                            probePassed = probePassed && GetWindowTextLengthW(edit) == 0;
                    probePassed = probePassed &&
                                  (dialogMode == passwords::Mode::create || old.size() == 23) &&
                                  (dialogMode == passwords::Mode::unlock || next.size() == 23);
                    return dialogMode == passwords::Mode::unlock && calls == 1
                               ? encrypted::Result::authentication
                               : encrypted::Result::ok;
                };
                startProbe(Probe::password);
                require(passwords::show(window, password) && probePassed &&
                            calls == (dialogMode == passwords::Mode::unlock ? 2 : 1),
                    "Password dialog did not clear fields or retry safely");
            }
        }
        std::puts("Checking themed messages and appearance...");
        fflush(stdout);
        testThemedMessages();
        std::puts("Themed messages and appearance passed.");
        fflush(stdout);
        shortcut.clear();
        preferences.hotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
        preferences.hotkeyKey = VK_F24;
        preferences.snippetHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
        preferences.snippetHotkeyKey = VK_F23;
        require(shortcut.replace(window, preferences.hotkeyModifiers, preferences.hotkeyKey),
            "Fixture shortcut unavailable");
        require(
            snippetShortcut.replace(window, preferences.snippetHotkeyModifiers, preferences.snippetHotkeyKey),
            "Snippet fixture shortcut unavailable");
        for (Probe value : {Probe::shortcutCancel, Probe::shortcutApply}) {
            startProbe(value);
            openSettings();
            require(probePassed && shortcut.registered() && snippetShortcut.registered(),
                "Shortcut capture failed or registrations were not restored");
            require(preferences.hotkeyKey == VK_F24, "Cancelling shortcut capture changed preferences");
        }
        std::puts("Checking nested validation...");
        fflush(stdout);
        SetTimer(nullptr, 0, 20, inspectInvalidShortcut);
        openSettings();
        require(messagePassed && shortcut.registered() && snippetShortcut.registered(),
            "Shortcut validation did not use a nested themed message or lost registrations");
        std::puts("Nested validation passed; checking encrypted editor...");
        fflush(stdout);
        wchar_t executable[32768];
        GetModuleFileNameW(nullptr, executable, 32768);
        const std::wstring encryptedRoot =
            std::wstring(executable) + L"." + std::to_wstring(GetCurrentProcessId()) + L".vault-fixture";
        snippetStore = snippets::Store(encryptedRoot);
        encryptedStore = encrypted::Store(encryptedRoot);
        selectedSnippet.clear();
        selectedEncrypted = false;
        const std::wstring privateFixture = L"PRIVATE FIXTURE \u00e4\u00f6\u00fc \u00df \u20ac";
        SetWindowTextW(contentEdit, privateFixture.c_str());
        SendMessageW(encryptSnippetCheck, BM_CLICK, 0, 0);
        require(encryptionChecked() && editorModified(),
            "New encrypted snippet choice did not mark editor modified");
        startProbe(Probe::protectedSave);
        require(saveSnippet() && probePassed && selectedEncrypted && !editorModified() &&
                    shortcut.registered() && snippetShortcut.registered(),
            "Encrypted save UI or shortcut restoration failed");
        std::vector<std::wstring> plainNames;
        require(
            snippetStore.list(plainNames) && plainNames.empty(), "Encrypted save wrote a plain-text snippet");
        SetWindowTextW(contentEdit, L"");
        SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
        selectedSnippet.clear();
        selectedEncrypted = false;
        SendMessageW(encryptSnippetCheck, BM_SETCHECK, BST_UNCHECKED, 0);
        for (int attempt = 0; attempt < 2; ++attempt) {
            startProbe(Probe::protectedLoad);
            chooseSnippet();
            require(probePassed && selectedEncrypted && encryptionChecked() &&
                        textOf(contentEdit) == privateFixture,
                "Encrypted load UI failed");
        }
        require(unlockPrompts == 2, "Encrypted editor loads reused a cached unlock");
        SendMessageW(encryptSnippetCheck, BM_CLICK, 0, 0);
        require(
            !encryptionChecked() && selectedEncrypted && editorModified() && textOf(window).back() == L'*',
            "Pending format change was not marked unsaved");
        startProbe(Probe::cancelConvert);
        require(!saveSnippet() && probePassed && selectedEncrypted && editorModified(),
            "Cancelled conversion changed saved format or lost pending change");
        startProbe(Probe::convert);
        require(
            saveSnippet() && probePassed && !selectedEncrypted && !encryptionChecked() && !editorModified(),
            "Encrypted-to-plain save failed");
        std::wstring plainText;
        std::vector<std::wstring> encryptedNames;
        require(snippetStore.load(selectedSnippet, plainText) == snippets::Result::ok &&
                    plainText == privateFixture &&
                    encryptedStore.list(encryptedNames) == encrypted::Result::ok && encryptedNames.empty(),
            "Decrypted save left an encrypted duplicate or changed text");
        SendMessageW(encryptSnippetCheck, BM_CLICK, 0, 0);
        startProbe(Probe::convert);
        require(saveSnippet() && probePassed && selectedEncrypted && encryptionChecked() && !editorModified(),
            "Plain-to-encrypted save failed");
        require(snippetStore.list(plainNames) && plainNames.empty(),
            "Encrypting an existing snippet left a plaintext original");
        SendMessageW(encryptSnippetCheck, BM_CLICK, 0, 0);
        SendMessageW(encryptSnippetCheck, BM_CLICK, 0, 0);
        require(!editorModified(), "Reverting checkbox created a spurious unsaved change");
        preferences.initialDelay = 60000;
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(Start, BN_CLICKED), reinterpret_cast<LPARAM>(typeButton));
        require(typing, "Start button dispatch did not start a session");
        openSnippetSearch();
        require(typing && !dialogOpen, "Snippet search interrupted an active typing session");
        require(textOf(typeButton) == L"Stop t&yping", "Start button did not become Stop");
        toggleTypingShortcut();
        require(session.stopping(), "Shared shortcut action did not stop the active session");
        session.join();
        restoreAfterTyping = false;
        updateSession();
        require(!typing && session.snapshot().result == keyboard::Result::cancelled,
            "Shared shortcut cancellation did not finish");
        require(GetWindowTextLengthW(contentEdit) == 0 && !SendMessageW(contentEdit, EM_CANUNDO, 0, 0) &&
                    !selectedEncrypted && !encryptionChecked(),
            "Decrypted editor or undo history survived typing cancellation");
        require(DeleteFileW((encryptedRoot + L"\\Encrypted.dat").c_str()) &&
                    DeleteFileW(snippetStore.path().c_str()) && RemoveDirectoryW(encryptedRoot.c_str()),
            "Encrypted UI fixture cleanup failed");
        // No test preference is ever written to the user's actual application key.
        initializing = true;
        DestroyWindow(window);
        for (HFONT f : {bodyFont, smallFont, titleFont, headingFont, editorFont})
            DeleteObject(f);
        for (HBRUSH b : {backgroundBrush, panelBrush, fieldBrush})
            DeleteObject(b);
        UnregisterClassW(WindowClass, instance);
        FreeLibrary(rich);
        std::puts(
            "PASS: native editor/themes, shortcut capture/caret alignment, encrypted picker/search, masked password retries/clearing, encrypted save choice and plaintext/undo cleanup after typing cancellation.");
        return 0;
    } catch (const std::exception& error) {
        initializing = true;
        if (IsWindow(window))
            DestroyWindow(window);
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        if (rich)
            FreeLibrary(rich);
        return 1;
    }
}
