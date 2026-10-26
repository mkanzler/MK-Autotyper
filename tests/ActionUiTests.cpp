// Exercise native controls through their own messages; no desktop input is sent.
#include "../AutoTyper/AutoTyper.cpp"
#include <cstdio>
#include <stdexcept>

namespace {
std::function<void(HWND)> inspect;
std::string failure;

void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

void CALLBACK probe(HWND, UINT, UINT_PTR timer, DWORD) {
    HWND dialog = GetLastActivePopup(window);
    if (dialog == window || GetWindow(dialog, GW_OWNER) != window)
        return;
    KillTimer(nullptr, timer);
    try {
        inspect(dialog);
    } catch (const std::exception& error) {
        failure = error.what();
        EndDialog(dialog, IDCANCEL);
    }
}

void expect(std::function<void(HWND)> action) {
    failure.clear();
    inspect = std::move(action);
    check(SetTimer(nullptr, 0, 20, probe) != 0, "Probe timer failed");
}

void done() {
    if (!failure.empty())
        throw std::runtime_error(failure);
}

void flushFlow() {
    MSG message{};
    int count = 0;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        check(++count < 1000, "Flow layout did not settle");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    inlineEditor.refresh();
}

void chooseStep(size_t index) {
    check(inlineEditor.activate(index, false), "Could not select flow card");
    if (!inlineEditor.expanded(index))
        check(inlineEditor.activate(index, true), "Could not expand flow card");
    flushFlow();
}

void click(int id) {
    if (id >= actioneditor::AddText && id <= actioneditor::Down) {
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
        flushFlow();
        return;
    }
    SendMessageW(id >= 900 ? inlineEditor.control(id) : GetDlgItem(window, id), BM_CLICK, 0, 0);
    flushFlow();
}

BOOL WINAPI chooseKeyStep(HMENU menu, UINT flags, int, int, HWND owner, LPTPMPARAMS) {
    check(owner == window && (flags & TPM_RETURNCMD) && GetMenuItemCount(menu) == 3,
        "Add step menu did not expose the three action types");
    for (int i = 0; i < 3; ++i) {
        MENUITEMINFOW item{sizeof(item)};
        wchar_t label[32]{};
        item.dwTypeData = label;
        item.cch = 32;
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
        check(GetMenuItemInfoW(menu, i, TRUE, &item) && (item.fType & MFT_OWNERDRAW) &&
                  item.wID == static_cast<UINT>(actioneditor::AddText + i),
            "Add step menu lost theme drawing or the action ordering");
        check(label[0] != 0, "The themed menu lost its accessible label");
    }
    return actioneditor::AddKey;
}

bool inspectedManageMenu = false;

BOOL WINAPI inspectManageMenu(HMENU menu, UINT flags, int, int, HWND owner, LPTPMPARAMS) {
    check(owner == window && (flags & TPM_RETURNCMD) && GetMenuItemCount(menu) == 2 &&
              GetMenuItemID(menu, 0) == RenameSnippet && GetMenuItemID(menu, 1) == DeleteSnippet,
        "Manage did not preserve Rename and Delete");
    inspectedManageMenu = true;
    return 0; // Inspect only; never operate on saved snippets.
}

RECT bounds(HWND control) {
    RECT result{};
    GetWindowRect(control, &result);
    MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&result), 2);
    return result;
}

void editor() {
    flushFlow();
    check(inlineEditor.count() == 1 && inlineEditor.expanded(0) && inlineEditor.textSelected() &&
              !editorModified(),
        "New snippets did not start with one expanded text card");
    check(GetParent(contentEdit) == inlineEditor.viewport() && inlineEditor.header(0),
        "The native editor is not hosted in the card flow");
    const RECT shortCard = inlineEditor.card(0);
    std::wstring manyLines;
    for (int i = 0; i < 24; ++i)
        manyLines += L"A line of text that grows the editor.\r\n";
    SetWindowTextW(contentEdit, manyLines.c_str());
    SendMessageW(contentEdit, EM_REQUESTRESIZE, 0, 0);
    flushFlow();
    check(inlineEditor.card(0).bottom - inlineEditor.card(0).top > shortCard.bottom - shortCard.top,
        "Text card did not grow with its contents");
    auto initial = snippetdocument::Document::fromText(secure::Text(std::wstring(L"Hello {name}")));
    setEditorDocument(std::move(initial), false);
    flushFlow();
    check(inlineEditor.activate(0, true) && !inlineEditor.expanded(0) &&
              inlineEditor.characterCount() == 12 && !editorModified(),
        "Folding text changed its count or marked it unsaved");
    chooseStep(0);
    check(inlineEditor.card(0).bottom - inlineEditor.card(0).top < static_cast<LONG>(manyLines.size()),
        "Text card did not shrink again");
    preferences.hotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    preferences.hotkeyKey = VK_F24;
    preferences.snippetHotkeyModifiers = MOD_CONTROL | MOD_ALT | MOD_SHIFT;
    preferences.snippetHotkeyKey = VK_F23;
    check(
        shortcut.replace(window, preferences.hotkeyModifiers, preferences.hotkeyKey) &&
            snippetShortcut.replace(window, preferences.snippetHotkeyModifiers, preferences.snippetHotkeyKey),
        "Fixture shortcut registration failed");
    inlineEditor.addMenu(dialogStyle(), chooseKeyStep);
    flushFlow();
    check(inlineEditor.count() == 2 && !inlineEditor.expanded(1) && !inlineEditor.recording() &&
              inlineEditor.header(0) && inlineEditor.header(1),
        "New key step was hidden or unexpectedly editing");
    check(inlineEditor.card(0).bottom < inlineEditor.card(1).top, "Step cards overlap");
    chooseStep(1);
    click(keyselector::Record);
    check(inlineEditor.recording() && inputCaptureActive && !shortcut.registered(),
        "Recording did not suspend shortcuts");
    BYTE before[256], state[256]{};
    GetKeyboardState(before);
    state[VK_CONTROL] = state[VK_LCONTROL] = state[VK_MENU] = state[VK_LMENU] = 0x80;
    SetKeyboardState(state);
    SendMessageW(inlineEditor.control(keyselector::Capture), WM_KEYDOWN, 'V', 0);
    check(inputCaptureActive && !shortcut.registered(),
        "Capture restored hotkeys before the recorded key was released");
    SendMessageW(inlineEditor.control(keyselector::Capture), WM_KEYUP, 'V', 0);
    SetKeyboardState(before);
    check(!inputCaptureActive && !inlineEditor.recording() && shortcut.registered() &&
              snippetShortcut.registered() && editorDocument.actions[1].key.code == 'V' &&
              editorDocument.actions[1].key.modifiers == (snippetdocument::control | snippetdocument::alt),
        "Ctrl+Alt+V capture failed or hotkeys were not restored");
    click(keyselector::Record);
    SendMessageW(inlineEditor.control(keyselector::Capture), WM_KEYDOWN, VK_ESCAPE, 0);
    check(!inputCaptureActive && editorDocument.actions[1].key.code == 'V',
        "Escape did not cancel capture without changing the chord");
    click(keyselector::Record);
    SendMessageW(inlineEditor.control(keyselector::Capture), WM_KILLFOCUS,
        reinterpret_cast<WPARAM>(inlineEditor.control(keyselector::Record)), 0);
    click(keyselector::Record);
    check(!inputCaptureActive && !inlineEditor.recording() && shortcut.registered(),
        "Record/Cancel button restarted capture after losing focus");
    click(keyselector::Record);
    BYTE neutral[256]{};
    SetKeyboardState(neutral);
    SendMessageW(inlineEditor.control(keyselector::Capture), WM_KEYDOWN, VK_RETURN, 1 << 24);
    SendMessageW(inlineEditor.control(keyselector::Capture), WM_KEYUP, VK_RETURN, 1 << 24);
    SetKeyboardState(before);
    check(editorDocument.actions[1].key.encoding == snippetdocument::KeyEncoding::scanCode &&
              editorDocument.actions[1].key.extended && editorDocument.actions[1].key.code == 0x1c,
        "Numpad Enter capture lost its extended scan code");
    SetWindowTextW(inlineEditor.control(keyselector::Search), L"Enter");
    HWND results = inlineEditor.control(keyselector::Results);
    check(SendMessageW(results, LB_GETCOUNT, 0, 0) > 0, "Friendly Enter search returned no keys");
    SendMessageW(inlineEditor.control(keyselector::Search), WM_KEYDOWN, VK_RETURN, 0);
    check(editorDocument.actions[1].key.encoding == snippetdocument::KeyEncoding::virtualKey &&
              editorDocument.actions[1].key.code == VK_RETURN,
        "Key search selected the wrong key");
    // Restore the Ctrl/Alt switches after the modifier-free numpad capture.
    click(keyselector::Ctrl);
    click(keyselector::Alt);
    click(keyselector::Ctrl);
    click(keyselector::Alt);
    click(keyselector::Win);
    check(editorDocument.actions[1].key.modifiers == snippetdocument::windows,
        "Modifier switches lost the selected key");
    SetWindowTextW(inlineEditor.control(keyselector::Search), L"no such key anywhere");
    check(SendMessageW(results, LB_GETCOUNT, 0, 0) == 0 && editorDocument.actions[1].key.code == VK_RETURN,
        "Empty search changed the saved chord");
    click(actioneditor::AddPause);
    check(!inlineEditor.expanded(2), "Pause step was not added as a compact card");
    chooseStep(2);
    SetWindowTextW(inlineEditor.control(actioneditor::PauseValue), L"250");
    check(inlineEditor.activate(2, true) && !inlineEditor.expanded(2) &&
              editorDocument.actions[2].milliseconds == 250,
        "Collapsing a pause lost its edited value");
    click(actioneditor::AddText);
    SetWindowTextW(contentEdit, L"Again {name}");
    SendMessageW(contentEdit, EM_SETMODIFY, TRUE, 0);
    flushFlow();
    click(actioneditor::Variables);
    const DWORD globalDelay = preferences.keystrokeDelay, globalInitial = preferences.initialDelay;
    const bool globalMode = globalUnicode;
    click(SnippetScope);
    SetWindowTextW(delayEdit, L"17");
    SetWindowTextW(initialDelayEdit, L"300");
    click(UnicodeMode);
    snippetdocument::Document saved;
    check(readEditorDocument(saved) && saved.actions.size() == 4 && saved.expandVariables &&
              saved.typing.overrideGlobal && saved.typing.unicode && saved.typing.keystrokeDelay == 17 &&
              saved.typing.initialDelay == 300 &&
              saved.actions[1].key.modifiers == snippetdocument::windows &&
              saved.actions[2].milliseconds == 250,
        "Card editor flattened steps or lost variables/settings");
    check(preferences.keystrokeDelay == globalDelay && preferences.initialDelay == globalInitial &&
              globalUnicode == globalMode,
        "Snippet controls overwrote global settings");
    click(SnippetScope);
    click(SnippetScope);
    check(textOf(delayEdit) == L"17" && textOf(initialDelayEdit) == L"300",
        "Scope switch lost custom settings");
    auto sequence = snippetdocument::clone(saved);
    check(inlineEditor.reorder(2, 1) && editorDocument.actions[1].type == snippetdocument::ActionType::delay,
        "Reordering cards failed");
    check(inlineEditor.reorder(1, 2), "Restoring card order failed");
    inlineEditor.scrollTo(0);
    flushFlow();
    HWND grip = inlineEditor.header(1);
    check(grip != nullptr, "Visible key card has no native header");
    SendMessageW(grip, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 20));
    SendMessageW(inlineEditor.viewport(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(12, 0));
    check(inputCaptureActive && inlineEditor.dragging(), "Drag did not capture input safely");
    SendMessageW(inlineEditor.viewport(), WM_LBUTTONUP, 0, MAKELPARAM(12, 0));
    flushFlow();
    check(editorDocument.actions[0].type == snippetdocument::ActionType::key && !inputCaptureActive &&
              shortcut.registered(),
        "Dropping a card did not reorder it or restore shortcuts");
    check(inlineEditor.reorder(0, 1), "Restoring drag order failed");
    inlineEditor.scrollTo(0);
    flushFlow();
    SendMessageW(inlineEditor.header(1), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 20));
    SendMessageW(inlineEditor.viewport(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(12, 0));
    SendMessageW(inlineEditor.viewport(), WM_KEYDOWN, VK_ESCAPE, 0);
    flushFlow();
    check(!inputCaptureActive && !inlineEditor.dragging() &&
              editorDocument.actions[0].type == snippetdocument::ActionType::text,
        "Escape changed order or retained capture");
    SendMessageW(inlineEditor.header(1), WM_SYSKEYDOWN, VK_DOWN, 0);
    flushFlow();
    check(editorDocument.actions[2].type == snippetdocument::ActionType::key, "Keyboard move down failed");
    SendMessageW(inlineEditor.header(2), WM_SYSKEYDOWN, VK_UP, 0);
    flushFlow();
    chooseStep(2);
    SetWindowTextW(inlineEditor.control(actioneditor::PauseValue), L"86400001");
    check(!readEditorDocument(saved), "Invalid pause saved");
    click(actioneditor::Remove);
    check(inlineEditor.count() == 3, "Invalid draft could not be removed");
    setEditorDocument(snippetdocument::clone(sequence), true);
    flushFlow();
    selectedSnippet = L"isolated menu fixture";
    updateEditorActions();
    chooseSnippetAction(inspectManageMenu);
    check(inspectedManageMenu, "Manage menu was not shown");
    selectedSnippet.clear();
    updateEditorActions();
    RECT originalWindow{};
    GetWindowRect(window, &originalWindow);
    const UINT originalDpi = dpi;
    const LONG_PTR originalStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, originalStyle | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(window, nullptr, -20000, -20000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    for (UINT value : {96u, 144u, 192u}) {
        dpi = value;
        createFonts();
        for (const POINT size : {POINT{900, 690}, POINT{1366, 900}, POINT{1000, 720}, POINT{1560, 1000}}) {
            chooseStep(0);
            SendMessageW(contentEdit, EM_SETSEL, 1, 4);
            RedrawWindow(window, nullptr, nullptr, RDW_VALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
            SetWindowPos(window, nullptr, 0, 0, scale(size.x), scale(size.y), SWP_NOMOVE | SWP_NOZORDER);
            for (HWND child : {themeButton, newSnippetButton, scanRadio, manageSnippetButton}) {
                RECT dirty{}, current{};
                GetClientRect(child, &current);
                check(GetUpdateRect(child, &dirty, FALSE) && EqualRect(&dirty, &current),
                    "Resize left stale child pixels");
            }
            DWORD start = 0, end = 0;
            SendMessageW(
                contentEdit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            check(start == 1 && end == 4 && textOf(contentEdit) == L"Hello {name}",
                "Resize changed text or selection");
            for (size_t i = 1; i < inlineEditor.count(); ++i)
                check(inlineEditor.card(i - 1).bottom < inlineEditor.card(i).top, "Resize overlapped cards");
        }
    }
    ShowWindow(window, SW_HIDE);
    SetWindowLongPtrW(window, GWL_EXSTYLE, originalStyle);
    dpi = originalDpi;
    createFonts();
    SetWindowPos(window, nullptr, originalWindow.left, originalWindow.top,
        originalWindow.right - originalWindow.left, originalWindow.bottom - originalWindow.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    layout();
    snippetdocument::Document many;
    for (size_t i = 0; i < snippetdocument::MaximumActions; ++i)
        many.actions.push_back(snippetdocument::Action::delay(1000));
    SetFocus(nullptr);
    setEditorDocument(std::move(many), false);
    inlineEditor.scrollTo(100000000);
    flushFlow();
    int children = 0;
    EnumChildWindows(
        inlineEditor.viewport(),
        [](HWND, LPARAM data) -> BOOL {
            ++*reinterpret_cast<int*>(data);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&children));
    check(children < 80 && inlineEditor.header(snippetdocument::MaximumActions - 1) &&
              inlineEditor.scrollPosition() > 65535,
        "Long sequences created one control per action or truncated their scroll range");
    snippetdocument::Document onlyKey;
    onlyKey.actions.push_back(
        snippetdocument::Action::pressKey({snippetdocument::KeyEncoding::virtualKey, VK_TAB, 0, false}));
    setEditorDocument(std::move(onlyKey), false);
    click(actioneditor::Remove);
    check(inlineEditor.count() == 1 && inlineEditor.textSelected() && inlineEditor.expanded(0) &&
              !editorModified(),
        "Removing the last card did not restore clean empty text");
    setEditorDocument(std::move(sequence), true);
    chooseStep(3);
}

void cardRegressions() {
    snippetdocument::Document original;
    check(readEditorDocument(original), "Could not preserve fixture sequence");
    RECT previousWindow{};
    GetWindowRect(window, &previousWindow);
    auto pauses = [](size_t count) {
        snippetdocument::Document document;
        for (size_t i = 0; i < count; ++i)
            document.actions.push_back(snippetdocument::Action::delay(static_cast<DWORD>((i + 1) * 101)));
        return document;
    };
    auto beginDrag = [&](size_t index, int y) {
        inlineEditor.scrollTo(0);
        flushFlow();
        check(inlineEditor.header(index) != nullptr, "Drag fixture header missing");
        SendMessageW(inlineEditor.header(index), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 20));
        SendMessageW(inlineEditor.viewport(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(12, y));
        check(inputCaptureActive && inlineEditor.dragging(), "Drag fixture did not start");
    };
    setEditorDocument(pauses(2), false);
    chooseStep(0);
    SetWindowTextW(inlineEditor.control(actioneditor::PauseValue), L"86400001");
    check(inlineEditor.activate(0, false), "Invalid pause prevented opening its current card menu");
    check(!inlineEditor.activate(1, false) && !inlineEditor.reorder(0, 1),
        "Invalid pause was committed when changing or moving cards");
    click(actioneditor::Remove);
    check(inlineEditor.count() == 1 && editorDocument.actions[0].milliseconds == 202,
        "Invalid pause could not be removed without committing it");

    auto boundedText = snippetdocument::Document::fromText(secure::Text(std::wstring(L"a")));
    boundedText.actions.push_back(snippetdocument::Action::typeText(
        secure::Text(std::wstring(snippetdocument::MaximumTextCharacters - 1, L'b'))));
    setEditorDocument(std::move(boundedText), false);
    check(inlineEditor.activate(1, true), "Could not fold the large text fixture");
    chooseStep(0);
    SetWindowTextW(contentEdit, L"ab");
    check(!inlineEditor.commit() && editorDocument.actions[0].text.size() == 1,
        "Committing text bypassed the aggregate limit or partially changed the document");
    check(!inlineEditor.activate(1, false), "Card switching retained text beyond the document limit");
    snippetdocument::Document output;
    check(!readEditorDocument(output) && output.actions.empty(), "Oversized text was accepted for saving");
    SetWindowTextW(contentEdit, L"a");
    check(
        readEditorDocument(output) && inlineEditor.characterCount() == snippetdocument::MaximumTextCharacters,
        "Correcting text to the exact document limit did not permit saving");
    output.clear();

    setEditorDocument(snippetdocument::Document::fromText(secure::Text(std::wstring(L"before"))), false);
    SendMessageW(contentEdit, EM_SETSEL, 6, 6);
    SendMessageW(contentEdit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" after"));
    flushFlow();
    check(SendMessageW(contentEdit, EM_CANUNDO, 0, 0) != 0, "Undo fixture did not create an undo record");
    check(inlineEditor.activate(0, true) && inlineEditor.activate(0, true), "Text folding failed");
    flushFlow();
    check(SendMessageW(contentEdit, EM_CANUNDO, 0, 0) != 0, "Folding a text card erased undo history");
    SendMessageW(contentEdit, EM_UNDO, 0, 0);
    flushFlow();
    check(textOf(contentEdit) == L"before", "Undo after folding restored the wrong text");
    setEditorDocument(pauses(4), false);
    flushFlow();
    beginDrag(1, 0);
    SendMessageW(inlineEditor.viewport(), WM_LBUTTONUP, 0, MAKELPARAM(-20, 0));
    flushFlow();
    check(editorDocument.actions[0].milliseconds == 101 && editorDocument.actions[1].milliseconds == 202 &&
              !inputCaptureActive && !editorModified(),
        "Dropping outside the flow committed an unintended move");

    beginDrag(1, 0);
    const int release = inlineEditor.card(3).bottom - 1;
    SendMessageW(inlineEditor.viewport(), WM_LBUTTONUP, 0, MAKELPARAM(12, release));
    flushFlow();
    check(editorDocument.actions[3].milliseconds == 202 && editorDocument.actions[0].milliseconds == 101,
        "Drop used a stale mouse-move position instead of the release position");

    setEditorDocument(pauses(4), false);
    flushFlow();
    beginDrag(1, 0);
    SendMessageW(inlineEditor.header(1), WM_SYSKEYDOWN, VK_DOWN, 0);
    check(editorDocument.actions[1].milliseconds == 202, "Keyboard movement corrupted an active drag");
    SendMessageW(inlineEditor.viewport(), WM_CANCELMODE, 0, 0);
    flushFlow();
    check(!inputCaptureActive && !inlineEditor.dragging() && !editorModified(),
        "Capture cancellation committed data");
    beginDrag(1, 0);
    SendMessageW(inlineEditor.viewport(), WM_CAPTURECHANGED, 0, 0);
    flushFlow();
    check(!inputCaptureActive && !inlineEditor.dragging() && editorDocument.actions[1].milliseconds == 202,
        "Losing mouse capture committed a move or left shortcuts suspended");

    setEditorDocument(pauses(100), false);
    flushFlow();
    beginDrag(1, 200);
    RECT viewport{};
    GetClientRect(inlineEditor.viewport(), &viewport);
    SendMessageW(inlineEditor.viewport(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(12, viewport.bottom - 2));
    for (int i = 0; i < 8; ++i)
        SendMessageW(inlineEditor.viewport(), WM_TIMER, 1, 0);
    check(inlineEditor.scrollPosition() > 0, "Dragging at the lower edge did not auto-scroll");
    const int beforeOutside = inlineEditor.scrollPosition();
    SendMessageW(inlineEditor.viewport(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(-20, viewport.bottom - 2));
    SendMessageW(inlineEditor.viewport(), WM_TIMER, 1, 0);
    check(inlineEditor.scrollPosition() == beforeOutside,
        "Dragging outside horizontally continued auto-scrolling");
    SendMessageW(inlineEditor.viewport(), WM_KEYDOWN, VK_ESCAPE, 0);
    flushFlow();
    check(!inputCaptureActive && !inlineEditor.dragging() && editorDocument.actions[0].milliseconds == 101,
        "Cancelling after auto-scroll changed the document");

    inlineEditor.scrollTo(200);
    flushFlow();
    const int start = inlineEditor.scrollPosition();
    SendMessageW(inlineEditor.viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-60)), 0);
    check(inlineEditor.scrollPosition() == start, "Partial wheel input scrolled too early");
    UINT lines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    SendMessageW(inlineEditor.viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-60)), 0);
    check(lines ? inlineEditor.scrollPosition() > start : inlineEditor.scrollPosition() == start,
        "High-resolution wheel accumulation ignored the Windows setting");
    const int wheelPosition = inlineEditor.scrollPosition();
    SendMessageW(inlineEditor.viewport(), WM_MOUSEWHEEL, MAKEWPARAM(MK_CONTROL, static_cast<WORD>(-120)), 0);
    check(inlineEditor.scrollPosition() == wheelPosition, "Ctrl+wheel unexpectedly moved the sequence");
    SendMessageW(inlineEditor.viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-60)), 0);
    setEditorDocument(pauses(100), false);
    inlineEditor.scrollTo(200);
    flushFlow();
    const int freshPosition = inlineEditor.scrollPosition();
    SendMessageW(inlineEditor.viewport(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-60)), 0);
    check(inlineEditor.scrollPosition() == freshPosition, "A new snippet inherited partial wheel input");

    SetWindowPos(window, nullptr, 0, 0, 1840, 760, SWP_NOMOVE | SWP_NOZORDER);
    auto anchored = pauses(9);
    std::wstring wrapping;
    for (int i = 0; i < 7; ++i)
        wrapping += L"These words wrap when the editor becomes narrower. ";
    anchored.actions.insert(
        anchored.actions.begin(), snippetdocument::Action::typeText(secure::Text(wrapping)));
    setEditorDocument(std::move(anchored), false);
    flushFlow();
    check(inlineEditor.activate(7, false), "Could not select anchor card");
    inlineEditor.scrollTo(inlineEditor.scrollPosition() + inlineEditor.card(7).top - 20);
    flushFlow();
    const int anchorTop = inlineEditor.card(7).top;
    SetWindowPos(window, nullptr, 0, 0, 900, 760, SWP_NOMOVE | SWP_NOZORDER);
    flushFlow();
    check(
        abs(inlineEditor.card(7).top - anchorTop) <= 1, "Width changes lost the visible-card scroll anchor");

    std::wstring longText;
    for (int i = 0; i < 100; ++i)
        longText += L"A long editable line.\r\n";
    setEditorDocument(snippetdocument::Document::fromText(secure::Text(longText)), false);
    flushFlow();
    RECT field{};
    GetClientRect(contentEdit, &field);
    const int shortHeight = field.bottom;
    GetClientRect(inlineEditor.viewport(), &viewport);
    check(
        field.bottom + scale(60) <= viewport.bottom, "Long text card exceeded the available viewport height");
    SetWindowPos(window, nullptr, 0, 0, 900, 980, SWP_NOMOVE | SWP_NOZORDER);
    flushFlow();
    GetClientRect(contentEdit, &field);
    check(field.bottom > shortHeight, "Text field remained stuck at its old height after enlarging");
    SetWindowPos(window, nullptr, 0, 0, 900, 690, SWP_NOMOVE | SWP_NOZORDER);
    flushFlow();
    GetClientRect(contentEdit, &field);
    GetClientRect(inlineEditor.viewport(), &viewport);
    check(field.bottom + scale(60) <= viewport.bottom && textOf(contentEdit) == longText,
        "Shrinking the window clipped the editor or changed its contents");

    SetWindowPos(window, nullptr, previousWindow.left, previousWindow.top,
        previousWindow.right - previousWindow.left, previousWindow.bottom - previousWindow.top,
        SWP_NOZORDER | SWP_NOACTIVATE);
    setEditorDocument(std::move(original), true);
    chooseStep(3);
}

void fieldDialog() {
    std::vector<snippetvariables::Field> fields;
    for (int i = 0; i < 5; ++i) {
        snippetvariables::Field field;
        field.name = secure::Text(std::wstring(L"field") + std::to_wstring(i));
        fields.push_back(std::move(field));
    }
    variabledialog::State state;
    state.ui.style = dialogStyle();
    state.fields = &fields;
    state.protectedContent = true;
    expect([](HWND dialog) {
        auto& current = *reinterpret_cast<variabledialog::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        for (HWND edit : current.edits) {
            check((GetWindowLongPtrW(edit, GWL_STYLE) & ES_PASSWORD) != 0,
                "Encrypted variable value was visible");
            SetWindowTextW(edit, L"fixture value");
            check(!SendMessageW(edit, EM_CANUNDO, 0, 0), "Variable input kept an undo copy");
        }
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        check(current.page == 1 && textOf(current.labels[0]) == L"field4", "Next variable page failed");
        SetWindowTextW(current.edits[0], L"last value");
        SendMessageW(dialog, WM_COMMAND, 950, 0);
        check(current.page == 0 && textOf(current.edits[0]) == L"fixture value", "Back lost entered values");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
        check(textOf(current.edits[0]) == L"last value", "Returning to the last page lost its value");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    check(withInputDialog([&] { return variabledialog::show(window, state); }),
        "Field dialog cancelled unexpectedly");
    done();
    check(snippetvariables::equal(fields[0].value, L"fixture value") &&
              snippetvariables::equal(fields[4].value, L"last value"),
        "Field values were not returned");
    fields.clear();
    snippetdocument::Document document;
    check(readEditorDocument(document), "Read inline editor failed");
    expect([](HWND dialog) {
        auto& current = *reinterpret_cast<variabledialog::State*>(GetWindowLongPtrW(dialog, DWLP_USER));
        check(current.fields->size() == 1, "Repeated variable created multiple inputs");
        SetWindowTextW(current.edits[0], L"World");
        SendMessageW(dialog, WM_COMMAND, IDOK, 0);
    });
    check(prepareDocument(document, true), "Variable preparation failed");
    done();
    check(snippetvariables::equal(document.actions[0].text, L"Hello World") &&
              snippetvariables::equal(document.actions[3].text, L"Again World") &&
              !document.expandVariables && editorDocument.expandVariables,
        "Preparation mutated the saved editor or failed to share field values");
    check(readEditorDocument(document), "Read inline editor failed");
    expect([](HWND dialog) { SendMessageW(dialog, WM_COMMAND, IDCANCEL, 0); });
    check(!prepareDocument(document, true) && document.expandVariables,
        "Cancelled fields expanded or started a sequence");
    done();
}
} // namespace

int main() {
    HMODULE rich = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    wchar_t executable[32768];
    GetModuleFileNameW(nullptr, executable, 32768);
    const std::wstring root =
        std::wstring(executable) + L"." + std::to_wstring(GetCurrentProcessId()) + L".data";
    try {
        check(rich != nullptr, "Rich Edit unavailable");
        instance = GetModuleHandleW(nullptr);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
        InitCommonControlsEx(&controls);
        WNDCLASSW type{};
        type.hInstance = instance;
        type.lpfnWndProc = windowProc;
        type.lpszClassName = WindowClass;
        check(RegisterClassW(&type) != 0, "Window class registration failed");
        HWND owner = CreateWindowW(WindowClass, L"Action UI tests", WS_OVERLAPPEDWINDOW, 100, 100, 1040, 740,
            nullptr, nullptr, instance, nullptr);
        check(owner != nullptr, "Test window creation failed");
        initializing = true;
        layout();
        snippetStore = snippets::Store(root);
        encryptedStore = encrypted::Store(root);
        for (bool dark : {true, false}) {
            preferences.theme = dark ? settings::ThemeMode::dark : settings::ThemeMode::light;
            applyTheme();
            newSnippet();
            editor();
            cardRegressions();
            fieldDialog();
            selectedSnippet = L"Sequence";
            check(saveSnippetContents(selectedSnippet, dark, false), "Structured plain save failed");
            snippetdocument::Document plain;
            check(loadSnippetDocument(selectedSnippet, false, plain) && plain.actions.size() == 4 &&
                      plain.typing.initialDelay == 300 && plain.expandVariables,
                "Saved sequence or settings were flattened");
            secure::Text password(std::wstring(L"Action fixture password 123"));
            check(encryptedStore.save(L"Protected", plain, password, !dark) == encrypted::Result::ok,
                "Encrypted sequence save failed");
            snippetdocument::Document protectedDocument;
            check(encryptedStore.load(L"Protected", password, protectedDocument) == encrypted::Result::ok &&
                      protectedDocument.actions.size() == 4 && protectedDocument.expandVariables,
                "Encrypted sequence lost document data");
            selectedEncrypted = true;
            SendMessageW(encryptSnippetCheck, BM_SETCHECK, BST_CHECKED, 0);
            setEditorDocument(std::move(protectedDocument), false);
            chooseStep(1);
            clearProtectedEditor();
            check(GetWindowTextLengthW(inlineEditor.control(keyselector::Capture)) == 0 &&
                      SendMessageW(inlineEditor.control(keyselector::Results), LB_GETCOUNT, 0, 0) == 0,
                "Protected captured key data or selected result survived clearing");
            check(editorDocument.actions.size() == 1 && editorDocument.actions[0].text.empty() &&
                      !editorDocument.expandVariables && snippetdocument::textOnly(editorDocument) &&
                      !editorStructureModified && GetWindowTextLengthW(contentEdit) == 0 &&
                      !SendMessageW(contentEdit, EM_CANUNDO, 0, 0),
                "Protected sequence or undo survived clearing");
        }
        check(DeleteFileW((root + L"\\Encrypted.dat").c_str()) && DeleteFileW(snippetStore.path().c_str()) &&
                  RemoveDirectoryW(root.c_str()),
            "Fixture cleanup failed");
        DestroyWindow(window);
        for (HFONT font : {bodyFont, smallFont, titleFont, headingFont, editorFont})
            if (font)
                DeleteObject(font);
        for (HBRUSH brush : {backgroundBrush, panelBrush, fieldBrush})
            if (brush)
                DeleteObject(brush);
        UnregisterClassW(WindowClass, instance);
        FreeLibrary(rich);
        std::puts(
            "PASS: aggregate text bounds, invalid-card removal, card growth/folding/undo, drop boundaries/release/cancellation/auto-scroll, wheel/anchoring, virtualization, resizing, shortcut capture, variables and encrypted cleanup in both themes.");
        return 0;
    } catch (const std::exception& error) {
        initializing = true;
        if (IsWindow(window))
            DestroyWindow(window);
        if (rich)
            FreeLibrary(rich);
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
