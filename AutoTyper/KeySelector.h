#pragma once
#include "AppDialogs.h"
#include "ActionPresentation.h"
#include "ControlLayout.h"
#include <array>

namespace keyselector {
constexpr UINT NumpadEnter = 256; // UI choice, stored as the E0 1C scan code.

enum Control {
    Capture = 900,
    Record,
    Ctrl,
    Alt,
    Shift,
    Win,
    Search,
    Results,
    Hint,
    CaptureLabel,
    SearchLabel
};

inline bool modifier(UINT vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_MENU || vk == VK_LMENU ||
           vk == VK_RMENU || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LWIN ||
           vk == VK_RWIN;
}

inline unsigned modifiers() {
    return ((GetKeyState(VK_CONTROL) & 0x8000) ? snippetdocument::control : 0) |
           ((GetKeyState(VK_MENU) & 0x8000) ? snippetdocument::alt : 0) |
           ((GetKeyState(VK_SHIFT) & 0x8000) ? snippetdocument::shift : 0) |
           (((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) ? snippetdocument::windows : 0);
}

inline std::wstring name(UINT vk) {
    if (vk == NumpadEnter)
        return L"Numpad Enter";
    switch (vk) {
    case VK_TAB:
        return L"Tab";
    case VK_RETURN:
        return L"Enter";
    case VK_ESCAPE:
        return L"Escape";
    case VK_BACK:
        return L"Backspace";
    case VK_SPACE:
        return L"Space";
    case VK_DELETE:
        return L"Delete";
    case VK_INSERT:
        return L"Insert";
    case VK_PRIOR:
        return L"Page Up";
    case VK_NEXT:
        return L"Page Down";
    case VK_HOME:
        return L"Home";
    case VK_END:
        return L"End";
    case VK_LEFT:
        return L"Left arrow";
    case VK_RIGHT:
        return L"Right arrow";
    case VK_UP:
        return L"Up arrow";
    case VK_DOWN:
        return L"Down arrow";
    case VK_LWIN:
        return L"Left Windows";
    case VK_RWIN:
        return L"Right Windows";
    case VK_LCONTROL:
        return L"Left Ctrl";
    case VK_RCONTROL:
        return L"Right Ctrl";
    case VK_CONTROL:
        return L"Ctrl";
    case VK_LMENU:
        return L"Left Alt";
    case VK_RMENU:
        return L"Right Alt / AltGr";
    case VK_MENU:
        return L"Alt";
    case VK_LSHIFT:
        return L"Left Shift";
    case VK_RSHIFT:
        return L"Right Shift";
    case VK_SHIFT:
        return L"Shift";
    case VK_PAUSE:
        return L"Pause / Break";
    case VK_SNAPSHOT:
        return L"Print Screen";
    case VK_CAPITAL:
        return L"Caps Lock";
    case VK_SCROLL:
        return L"Scroll Lock";
    case VK_NUMLOCK:
        return L"Num Lock";
    case VK_MULTIPLY:
        return L"Numpad multiply (*)";
    case VK_ADD:
        return L"Numpad add (+)";
    case VK_SUBTRACT:
        return L"Numpad subtract (-)";
    case VK_DECIMAL:
        return L"Numpad decimal";
    case VK_DIVIDE:
        return L"Numpad divide (/)";
    case VK_VOLUME_MUTE:
        return L"Volume mute";
    case VK_VOLUME_DOWN:
        return L"Volume down";
    case VK_VOLUME_UP:
        return L"Volume up";
    case VK_MEDIA_NEXT_TRACK:
        return L"Media next track";
    case VK_MEDIA_PREV_TRACK:
        return L"Media previous track";
    case VK_MEDIA_STOP:
        return L"Media stop";
    case VK_MEDIA_PLAY_PAUSE:
        return L"Media play / pause";
    case VK_BROWSER_BACK:
        return L"Browser back";
    case VK_BROWSER_FORWARD:
        return L"Browser forward";
    case VK_BROWSER_REFRESH:
        return L"Browser refresh";
    case VK_BROWSER_STOP:
        return L"Browser stop";
    case VK_BROWSER_SEARCH:
        return L"Browser search";
    case VK_BROWSER_FAVORITES:
        return L"Browser favorites";
    case VK_BROWSER_HOME:
        return L"Browser home";
    case VK_LAUNCH_MAIL:
        return L"Open mail";
    case VK_LAUNCH_MEDIA_SELECT:
        return L"Open media player";
    case VK_LAUNCH_APP1:
        return L"Launch app 1";
    case VK_LAUNCH_APP2:
        return L"Launch app 2";
    }
    if (vk >= VK_F1 && vk <= VK_F24)
        return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
        return L"Numpad " + std::to_wstring(vk - VK_NUMPAD0);
    return hotkey::name(0, vk);
}

inline const std::vector<UINT>& catalog() {
    static const auto values = [] {
        std::vector<UINT> keys{VK_TAB, VK_RETURN, NumpadEnter, VK_ESCAPE, VK_BACK, VK_SPACE, VK_DELETE,
            VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_HOME, VK_END, VK_PRIOR, VK_NEXT};
        for (UINT vk = 8; vk < 255; ++vk) {
            if (vk == VK_PACKET || vk == VK_PROCESSKEY ||
                std::find(keys.begin(), keys.end(), vk) != keys.end())
                continue;
            const bool layoutKey =
                (vk >= VK_OEM_1 && vk <= VK_OEM_3) || (vk >= VK_OEM_4 && vk <= VK_OEM_8) || vk == VK_OEM_102;
            if (MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) || layoutKey || (vk >= VK_F1 && vk <= VK_F24) ||
                (vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2))
                keys.push_back(vk);
        }
        return keys;
    }();
    return values;
}

inline bool matches(UINT vk, const secure::Text& query) {
    if (query.empty())
        return true;
    auto english = name(vk);
    if (vk == VK_RETURN || vk == NumpadEnter)
        english += L" Return";
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL)
        english += L" Control";
    if (vk == VK_PRIOR)
        english += L" PgUp";
    if (vk == VK_NEXT)
        english += L" PgDown";
    const auto local = hotkey::name(0, vk);
    const auto contains = [&](const std::wstring& label) {
        if (query.size() > label.size())
            return false;
        for (size_t i = 0; i + query.size() <= label.size(); ++i)
            if (CompareStringOrdinal(label.data() + i, static_cast<int>(query.size()), query.data(),
                    static_cast<int>(query.size()), TRUE) == CSTR_EQUAL)
                return true;
        return false;
    };
    return contains(english) || contains(local) ||
           (vk == VK_ESCAPE && query.size() == 3 &&
               CompareStringOrdinal(query.data(), 3, L"esc", 3, TRUE) == CSTR_EQUAL);
}

class Picker {
    HWND owner_ = nullptr, capture_ = nullptr, record_ = nullptr, search_ = nullptr, results_ = nullptr,
         hint_ = nullptr, captureLabel_ = nullptr, searchLabel_ = nullptr;
    std::array<HWND, 4> modifiers_{};
    std::vector<UINT> filtered_;
    snippetdocument::KeyStroke value_{snippetdocument::KeyEncoding::virtualKey, VK_TAB, 0, false};
    std::function<void()> changed_;
    std::function<void(bool)> captureChanged_;
    bool loading_ = false, recording_ = false;
    UINT lastModifier_ = 0;
    snippetdocument::KeyStroke pending_{};
    UINT pendingVirtual_ = 0;

    void caption() {
        auto text = actioneditor::keyName(value_);
        SetWindowTextW(capture_, recording_ ? L"Press a shortcut... (Esc cancels)" : text.c_str());
        SendMessageW(capture_, EM_EMPTYUNDOBUFFER, 0, 0);
        SetWindowTextW(record_, recording_ ? L"Cancel" : L"Record");
    }

    static LRESULT CALLBACK captureProc(
        HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
        auto& self = *reinterpret_cast<Picker*>(data);
        if (message == WM_GETDLGCODE) {
            const auto* keyMessage = reinterpret_cast<const MSG*>(lParam);
            if (self.recording_ ||
                (keyMessage && (keyMessage->wParam == VK_RETURN || keyMessage->wParam == VK_SPACE)))
                return DLGC_WANTALLKEYS;
        }
        if (message == WM_CHAR || message == WM_SYSCHAR)
            return 0;
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
            InvalidateRect(self.owner_, nullptr, FALSE);
        if (message == WM_KILLFOCUS && reinterpret_cast<HWND>(wParam) != self.record_)
            self.stopRecording();
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP ||
            message == WM_SYSKEYUP) {
            const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
            if (self.recording_) {
                self.recordKey(static_cast<UINT>(wParam), up, modifiers(), (lParam & (1 << 24)) != 0);
                return 0;
            }
            if (!up && (wParam == VK_RETURN || wParam == VK_SPACE)) {
                self.startRecording();
                return 0;
            }
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(field, captureProc, id);
        return DefSubclassProc(field, message, wParam, lParam);
    }

    static LRESULT CALLBACK searchProc(
        HWND field, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR data) {
        auto& self = *reinterpret_cast<Picker*>(data);
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS)
            InvalidateRect(self.owner_, nullptr, FALSE);
        if (message == WM_GETDLGCODE) {
            const auto* keyMessage = reinterpret_cast<const MSG*>(lParam);
            if (keyMessage && (keyMessage->wParam == VK_RETURN || keyMessage->wParam == VK_DOWN))
                return DLGC_WANTALLKEYS;
        }
        if (message == WM_KEYDOWN && (wParam == VK_RETURN || wParam == VK_DOWN)) {
            if (!self.filtered_.empty()) {
                if (SendMessageW(self.results_, LB_GETCURSEL, 0, 0) == LB_ERR)
                    SendMessageW(self.results_, LB_SETCURSEL, 0, 0);
                self.command(Results, LBN_SELCHANGE);
                SetFocus(wParam == VK_DOWN ? self.results_ : self.capture_);
            }
            return 0;
        }
        if (message == WM_CHAR && wParam == VK_RETURN)
            return 0;
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(field, searchProc, id);
        return DefSubclassProc(field, message, wParam, lParam);
    }

    void filter() {
        filtered_.clear();
        auto query = secure::Text::read(search_);
        SendMessageW(results_, LB_RESETCONTENT, 0, 0);
        int selected = -1;
        for (UINT vk : catalog())
            if (matches(vk, query)) {
                if ((value_.encoding == snippetdocument::KeyEncoding::virtualKey && value_.code == vk) ||
                    (vk == NumpadEnter && value_.encoding == snippetdocument::KeyEncoding::scanCode &&
                        value_.code == 0x1c && value_.extended))
                    selected = static_cast<int>(filtered_.size());
                filtered_.push_back(vk);
                const auto label = name(vk);
                SendMessageW(results_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
        if (!query.empty() && !filtered_.empty())
            selected = 0;
        SendMessageW(results_, LB_SETCURSEL, selected, 0);
        SetWindowTextW(hint_, filtered_.empty()
                                  ? L"No matches. Try Tab, Enter, F5 or a letter."
                                  : L"Enter selects  |  Down browses  |  Esc cancels recording");
    }

public:
    Picker() = default;
    Picker(const Picker&) = delete;
    Picker& operator=(const Picker&) = delete;

    ~Picker() {
        SecureZeroMemory(&value_, sizeof(value_));
        SecureZeroMemory(&pending_, sizeof(pending_));
    }

    bool create(HWND owner, HINSTANCE instance, const std::function<void(HWND)>& decorate,
        std::function<void()> changed, std::function<void(bool)> captureChanged) {
        owner_ = owner;
        changed_ = std::move(changed);
        captureChanged_ = std::move(captureChanged);
        auto add = [&](const wchar_t* type, const wchar_t* text, DWORD flags, int id) {
            HWND child = CreateWindowExW(0, type, text, WS_CHILD | WS_CLIPSIBLINGS | flags, 0, 0, 0, 0, owner,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            if (!wcscmp(type, L"BUTTON"))
                decorate(child);
            return child;
        };
        captureLabel_ = add(L"STATIC", L"Shortcut", SS_LEFT | SS_NOPREFIX, CaptureLabel);
        capture_ = add(L"EDIT", L"", WS_TABSTOP | ES_READONLY | ES_AUTOHSCROLL, Capture);
        SetWindowSubclass(capture_, captureProc, 1, reinterpret_cast<DWORD_PTR>(this));
        record_ = add(L"BUTTON", L"Record", WS_TABSTOP | BS_OWNERDRAW, Record);
        for (int i = 0; i < 4; ++i)
            modifiers_[i] = add(L"BUTTON",
                i == 0   ? L"Ctrl"
                : i == 1 ? L"Alt"
                : i == 2 ? L"Shift"
                         : L"Win",
                WS_TABSTOP | BS_AUTOCHECKBOX, Ctrl + i);
        searchLabel_ = add(L"STATIC", L"Find a key", SS_LEFT | SS_NOPREFIX, SearchLabel);
        search_ = add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Search);
        SendMessageW(search_, EM_SETLIMITTEXT, 64, 0);
        SendMessageW(search_, EM_SETCUEBANNER, TRUE,
            reinterpret_cast<LPARAM>(L"Search keys: Tab, Enter, Escape, F5..."));
        SetWindowSubclass(search_, dialogs::nameEditProc, 1, 0);
        SetWindowSubclass(search_, searchProc, 2, reinterpret_cast<DWORD_PTR>(this));
        results_ = add(L"LISTBOX", L"",
            WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, Results);
        hint_ = add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, Hint);
        caption();
        return capture_ && record_ && search_ && results_ && hint_ && captureLabel_ && searchLabel_;
    }

    void fonts(HFONT body, HFONT smallFont, UINT dpi) {
        for (HWND field : {capture_, record_, search_, results_})
            SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        for (HWND field : modifiers_)
            SendMessageW(field, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(hint_, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
        SendMessageW(captureLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(searchLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        SendMessageW(results_, LB_SETITEMHEIGHT, 0, MulDiv(30, dpi, 96));
    }

    void layout(controls::Layout& batch, int x, int y, int width, int bottom) {
        const auto move = [&](HWND field, int left, int top, int w, int h) {
            batch.move(field, left, top, w, h);
        };
        move(captureLabel_, x, y + 8, 76, 24);
        move(capture_, x + 90, y + 5, width - 196, 24);
        move(record_, x + width - 96, y, 96, 34);
        for (int i = 0; i < 4; ++i)
            move(modifiers_[i], x + i * (width / 4), y + 44, width / 4 - 8, 30);
        move(searchLabel_, x, y + 90, 96, 24);
        move(search_, x + 114, y + 88, width - 124, 26);
        move(results_, x, y + 126, width, std::max(30, bottom - y - 152));
        move(hint_, x, bottom - 20, width, 20);
    }

    void visible(bool show) {
        if (!show)
            stopRecording();
        for (HWND field : {captureLabel_, capture_, record_, searchLabel_, search_, results_, hint_})
            ShowWindow(field, show ? SW_SHOW : SW_HIDE);
        for (HWND field : modifiers_)
            ShowWindow(field, show ? SW_SHOW : SW_HIDE);
    }

    void enabled(bool enable) {
        if (!enable)
            stopRecording();
        for (HWND field : {capture_, record_, search_, results_})
            EnableWindow(field, enable);
        for (HWND field : modifiers_)
            EnableWindow(field, enable);
    }

    void set(const snippetdocument::KeyStroke& value) {
        loading_ = true;
        stopRecording();
        value_ = value;
        for (int i = 0; i < 4; ++i)
            SendMessageW(
                modifiers_[i], BM_SETCHECK, (value.modifiers & (1u << i)) ? BST_CHECKED : BST_UNCHECKED, 0);
        SetWindowTextW(search_, L"");
        SendMessageW(search_, EM_EMPTYUNDOBUFFER, 0, 0);
        caption();
        filter();
        loading_ = false;
    }

    void clear() {
        stopRecording();
        SecureZeroMemory(&value_, sizeof(value_));
        for (HWND field : modifiers_)
            SendMessageW(field, BM_SETCHECK, BST_UNCHECKED, 0);
        SetWindowTextW(capture_, L"");
        SetWindowTextW(search_, L"");
        SendMessageW(capture_, EM_EMPTYUNDOBUFFER, 0, 0);
        SendMessageW(search_, EM_EMPTYUNDOBUFFER, 0, 0);
        filtered_.clear();
        SendMessageW(results_, LB_RESETCONTENT, 0, 0);
    }

    const snippetdocument::KeyStroke& value() const {
        return value_;
    }

    bool recording() const {
        return recording_;
    }

    bool field(HWND field) const {
        return field == capture_ || field == search_;
    }

    std::array<HWND, 10> readingOrder() const {
        return {captureLabel_, capture_, record_, modifiers_[0], modifiers_[1], modifiers_[2], modifiers_[3],
            searchLabel_, search_, results_};
    }

    void startRecording() {
        if (recording_)
            return;
        recording_ = true;
        lastModifier_ = pendingVirtual_ = 0;
        SecureZeroMemory(&pending_, sizeof(pending_));
        if (captureChanged_)
            captureChanged_(true);
        caption();
        SetFocus(capture_);
    }

    void stopRecording() {
        if (!recording_)
            return;
        recording_ = false;
        lastModifier_ = pendingVirtual_ = 0;
        SecureZeroMemory(&pending_, sizeof(pending_));
        caption();
        if (captureChanged_)
            captureChanged_(false);
    }

    void recordKey(UINT vk, bool up, unsigned held, bool extended = false) {
        if (!recording_)
            return;
        if (!up && vk == VK_ESCAPE) {
            stopRecording();
            return;
        }
        if (pending_.code) {
            if (!up || vk != pendingVirtual_)
                return;
            value_ = pending_;
        } else if (modifier(vk)) {
            if (!up) {
                lastModifier_ = vk;
                return;
            }
            if (vk != lastModifier_)
                return;
            value_ = {snippetdocument::KeyEncoding::virtualKey, vk, 0, false};
        } else {
            if (up || vk < 8 || vk >= 255 || vk == VK_PACKET || vk == VK_PROCESSKEY)
                return;
            pendingVirtual_ = vk;
            pending_ =
                vk == VK_RETURN && extended
                    ? snippetdocument::KeyStroke{snippetdocument::KeyEncoding::scanCode, 0x1c, held, true}
                    : snippetdocument::KeyStroke{snippetdocument::KeyEncoding::virtualKey, vk, held, false};
            auto text = actioneditor::keyName(pending_);
            SetWindowTextW(capture_, text.c_str());
            SendMessageW(capture_, EM_EMPTYUNDOBUFFER, 0, 0);
            return; // Keep app shortcuts paused until the recorded key is released.
        }
        const unsigned chosen = value_.modifiers;
        for (int i = 0; i < 4; ++i)
            SendMessageW(modifiers_[i], BM_SETCHECK, (chosen & (1u << i)) ? BST_CHECKED : BST_UNCHECKED, 0);
        stopRecording();
        filter();
        if (changed_)
            changed_();
    }

    bool command(int id, int notification) {
        if (loading_)
            return id >= Capture && id <= Hint;
        if (id == Record && notification == BN_CLICKED) {
            if (recording_)
                stopRecording();
            else
                startRecording();
            return true;
        }
        if (id == Search && notification == EN_CHANGE) {
            filter();
            return true;
        }
        if (id == Results && (notification == LBN_SELCHANGE || notification == LBN_DBLCLK)) {
            const auto row = SendMessageW(results_, LB_GETCURSEL, 0, 0);
            if (row != LB_ERR && static_cast<size_t>(row) < filtered_.size()) {
                if (filtered_[row] == NumpadEnter) {
                    value_.encoding = snippetdocument::KeyEncoding::scanCode;
                    value_.code = 0x1c;
                    value_.extended = true;
                } else {
                    value_.encoding = snippetdocument::KeyEncoding::virtualKey;
                    value_.code = filtered_[row];
                    value_.extended = false;
                }
                caption();
                if (changed_)
                    changed_();
            }
            return true;
        }
        if (id >= Ctrl && id <= Win && notification == BN_CLICKED) {
            value_.modifiers = 0;
            for (int i = 0; i < 4; ++i)
                if (SendMessageW(modifiers_[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    value_.modifiers |= 1u << i;
            caption();
            if (changed_)
                changed_();
            return true;
        }
        return false;
    }

    bool draw(const dialogs::Style& style, const DRAWITEMSTRUCT& item) const {
        if (item.CtlID != Results)
            return false;
        HBRUSH brush = CreateSolidBrush(item.itemState & ODS_SELECTED ? style.accent : style.field);
        FillRect(item.hDC, &item.rcItem, brush);
        DeleteObject(brush);
        if (item.itemID < filtered_.size()) {
            auto label = name(filtered_[item.itemID]);
            RECT text = item.rcItem;
            text.left += MulDiv(10, style.dpi, 96);
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, item.itemState & ODS_SELECTED ? RGB(255, 255, 255) : style.text);
            const auto old = SelectObject(item.hDC, style.font);
            DrawTextW(item.hDC, label.c_str(), -1, &text,
                DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(item.hDC, old);
        }
        if (item.itemState & ODS_FOCUS)
            DrawFocusRect(item.hDC, &item.rcItem);
        return true;
    }
};
} // namespace keyselector
