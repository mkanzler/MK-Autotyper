#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <richedit.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "Resource.h"
#include "KeyboardInput.h"
#include "Settings.h"
#include "TypingSession.h"
#include "SnippetStore.h"
#include "AppDialogs.h"
#include "SnippetSearch.h"
#include "TypingTarget.h"
#include "PasswordDialog.h"
#include "SessionPanel.h"
#include "StartupShortcut.h"
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Uxtheme.lib")
#pragma comment(lib, "Advapi32.lib")

namespace {
constexpr wchar_t WindowClass[] = L"MK-AutoTyper";
constexpr UINT TrayMessage = WM_APP + 1;
constexpr UINT_PTR SessionTimer = 1;
constexpr int ExitCommand = 100, StopCommand = 101;

enum Control {
    Content = 200,
    Delay,
    InitialDelay,
    ScanMode,
    UnicodeMode,
    Theme,
    Start,
    Startup,
    MoreSettings,
    SnippetMenu,
    NewSnippet,
    SaveSnippet,
    RenameSnippet,
    DeleteSnippet,
    EncryptSnippet
};

// Main-window state is confined to the UI thread. Session owns the worker state.
HINSTANCE instance;
HWND window, contentEdit, delayEdit, initialDelayEdit, scanRadio, unicodeRadio, typeButton, themeButton;
HWND editorLabel, modeLabel, modeHelp, delayLabel, initialLabel, preferencesNote;
HWND startupCheck, settingsButton, snippetMenuButton, newSnippetButton, saveSnippetButton,
    renameSnippetButton, deleteSnippetButton;
HWND statusLabel, shortcutHint;
HWND encryptSnippetCheck;
NOTIFYICONDATAW tray = {};
typing_session::Session session;
hotkey::Registration shortcut;
hotkey::Registration snippetShortcut(&RegisterHotKey, &UnregisterHotKey, 3);
sessionview::Panel sessionPanel;
snippets::Store snippetStore;
encrypted::Store encryptedStore(snippetStore.directory());
std::wstring selectedSnippet;
// The saved source format; the checkbox is the pending format applied by Save.
// Keeping these separate lets Cancel preserve the existing saved snippet.
bool selectedEncrypted = false;
bool dialogOpen = false;
bool typing = false, restoreAfterTyping = false, initializing = true;
bool restoringPlacement = true;
settings::Preferences preferences;
bool darkTheme = false;
HMENU activeThemeMenu = nullptr;
dialogs::Style dialogStyle();
const wchar_t* snippetErrorText(snippets::Result result);
std::wstring statusText = L"Ready when you are";
UINT dpi = 96;
HFONT bodyFont, smallFont, titleFont, headingFont, editorFont;
HICON headerIcon, windowLargeIcon, windowSmallIcon;
HBRUSH backgroundBrush, panelBrush, fieldBrush;
HWND hoveredButton;

struct Palette {
    COLORREF background, panel, field, text, muted, border, accent, hover;
};

Palette colors;

int scale(int value) {
    return MulDiv(value, dpi, 96);
}

RECT client() {
    RECT r;
    GetClientRect(window, &r);
    return r;
}

int logicalWidth() {
    return MulDiv(client().right, 96, dpi);
}

int logicalHeight() {
    return MulDiv(client().bottom, 96, dpi);
}

std::wstring textOf(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring text(length + 1, L'\0');
    const int copied = GetWindowTextW(control, &text[0], length + 1);
    text.resize(copied);
    return text;
}

DWORD delayOf(HWND control, DWORD fallback, DWORD minimum) {
    const std::wstring text = textOf(control);
    DWORD result = fallback;
    if (!text.empty() && text.find_first_not_of(L"0123456789") == std::wstring::npos) {
        try {
            result = std::max(minimum, std::min<DWORD>(60000, std::stoul(text)));
        } catch (...) {
        }
    }
    SetWindowTextW(control, std::to_wstring(result).c_str());
    return result;
}

void persistPreferences() {
    if (initializing)
        return;
    preferences.keystrokeDelay = delayOf(delayEdit, preferences.keystrokeDelay, 2);
    preferences.initialDelay = delayOf(initialDelayEdit, preferences.initialDelay, 0);
    placement::capture(window, preferences.position);
    if (!settings::save(preferences))
        statusText = L"Settings could not be saved";
    InvalidateRect(window, nullptr, FALSE);
}

HFONT font(int size, int weight = FW_NORMAL, const wchar_t* face = L"Segoe UI") {
    return CreateFontW(-scale(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

void createFonts() {
    for (HFONT old : {bodyFont, smallFont, titleFont, headingFont, editorFont})
        if (old)
            DeleteObject(old);
    bodyFont = font(14);
    smallFont = font(12);
    titleFont = font(25, FW_SEMIBOLD);
    headingFont = font(14, FW_SEMIBOLD);
    editorFont = font(16, FW_NORMAL, L"Consolas");
    for (HWND control : {delayEdit, initialDelayEdit, scanRadio, unicodeRadio, typeButton, themeButton,
             startupCheck, settingsButton, snippetMenuButton, newSnippetButton, saveSnippetButton,
             encryptSnippetCheck, renameSnippetButton, deleteSnippetButton})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(bodyFont), TRUE);
    for (HWND control : {modeLabel, delayLabel, initialLabel, editorLabel})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont), TRUE);
    SendMessageW(statusLabel, WM_SETFONT, reinterpret_cast<WPARAM>(headingFont), TRUE);
    SendMessageW(shortcutHint, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    for (HWND control : {modeHelp, preferencesNote})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(smallFont), TRUE);
    SendMessageW(contentEdit, WM_SETFONT, reinterpret_cast<WPARAM>(editorFont), TRUE);
}

void applyTheme() {
    darkTheme = settings::usesDarkTheme(preferences.theme);
    colors = darkTheme ? Palette{RGB(12, 18, 30), RGB(21, 30, 47), RGB(29, 40, 60), RGB(233, 238, 248),
                             RGB(158, 173, 196), RGB(47, 61, 83), RGB(80, 94, 220), RGB(99, 113, 240)}
                       : Palette{RGB(243, 246, 251), RGB(255, 255, 255), RGB(242, 245, 250), RGB(26, 36, 58),
                             RGB(100, 115, 137), RGB(219, 226, 237), RGB(70, 83, 205), RGB(85, 99, 223)};
    for (HBRUSH brush : {backgroundBrush, panelBrush, fieldBrush})
        if (brush)
            DeleteObject(brush);
    backgroundBrush = CreateSolidBrush(colors.background);
    panelBrush = CreateSolidBrush(colors.panel);
    fieldBrush = CreateSolidBrush(colors.field);
    const LRESULT modified = SendMessageW(contentEdit, EM_GETMODIFY, 0, 0);
    SendMessageW(contentEdit, EM_SETBKGNDCOLOR, 0, colors.panel);
    CHARFORMAT2W format = {};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR | CFM_BACKCOLOR;
    format.crTextColor = colors.text;
    format.crBackColor = colors.panel;
    SendMessageW(contentEdit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
    SendMessageW(contentEdit, EM_SETMODIFY, modified, 0);
    const BOOL dark = darkTheme;
    DwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    for (HWND control : {scanRadio, unicodeRadio, startupCheck, encryptSnippetCheck})
        SetWindowTheme(control, L"", L"");
    SetWindowTextW(themeButton, settings::themeLabel(preferences.theme));
    RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
    const auto style = dialogStyle();
    EnumThreadWindows(
        GetCurrentThreadId(),
        [](HWND child, LPARAM data) -> BOOL {
            if (GetPropW(child, dialogs::ThemeProperty))
                SendMessageW(child, dialogs::ThemeChanged, 0, data);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&style));
}

void move(HWND control, int x, int y, int width, int height) {
    MoveWindow(control, scale(x), scale(y), scale(width), scale(height), TRUE);
}

void layout() {
    const int w = logicalWidth(), h = logicalHeight(), side = w - 300;
    move(settingsButton, w - 326, 32, 144, 36);
    move(themeButton, w - 170, 32, 146, 36);
    move(editorLabel, 44, 138, 180, 24);
    const int available = w - 388;
    move(encryptSnippetCheck, 44 + available - 326, 134, 170, 32);
    move(snippetMenuButton, 44, 175, available - 266, 32);
    move(newSnippetButton, 44 + available - 258, 175, 48, 32);
    move(saveSnippetButton, 44 + available - 204, 175, 48, 32);
    move(renameSnippetButton, 44 + available - 150, 175, 66, 32);
    move(deleteSnippetButton, 44 + available - 78, 175, 78, 32);
    move(contentEdit, 44, 222, available, std::max(100, h - 340));
    RECT formatting;
    GetClientRect(contentEdit, &formatting);
    InflateRect(&formatting, -scale(2), -scale(2));
    SendMessageW(contentEdit, EM_SETRECT, 0, reinterpret_cast<LPARAM>(&formatting));
    move(modeLabel, side + 22, 174, 232, 24);
    move(scanRadio, side + 22, 200, 232, 36);
    move(unicodeRadio, side + 22, 242, 232, 36);
    move(modeHelp, side + 22, 283, 232, 42);
    move(delayLabel, side + 22, 342, 232, 24);
    move(delayEdit, side + 34, 379, 208, 22);
    move(initialLabel, side + 22, 424, 232, 24);
    move(initialDelayEdit, side + 34, 461, 208, 22);
    move(startupCheck, side + 22, 502, 232, 32);
    move(preferencesNote, side + 22, 536, 232, 18);
    move(typeButton, side, h - 68, 276, 44);
    move(statusLabel, 24, h - 70, w - 348, 24);
    move(shortcutHint, 24, h - 43, w - 348, 20);
    InvalidateRect(window, nullptr, FALSE);
}

void drawText(HDC dc, const std::wstring& text, RECT rect, HFONT typeface, COLORREF color,
    UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    const HGDIOBJ old = SelectObject(dc, typeface);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rect, flags);
    SelectObject(dc, old);
}

RECT area(int x, int y, int w, int h) {
    return {scale(x), scale(y), scale(x + w), scale(y + h)};
}

void roundBox(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius = 12) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, scale(radius), scale(radius));
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

bool isChoice(HWND hwnd) {
    const LONG_PTR type = GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK;
    return type == BS_AUTORADIOBUTTON || type == BS_AUTOCHECKBOX;
}

COLORREF blend(COLORREF foreground, COLORREF background, int percent) {
    const auto channel = [percent](int a, int b) { return (a * percent + b * (100 - percent)) / 100; };
    return RGB(channel(GetRValue(foreground), GetRValue(background)),
        channel(GetGValue(foreground), GetGValue(background)),
        channel(GetBValue(foreground), GetBValue(background)));
}

// Paint native radio/checkbox controls rather than replacing their semantics.
// Windows still supplies grouping, Space/arrow keys, mnemonics and accessibility.
void paintChoice(HWND hwnd, HDC dc) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    const bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
    const bool disabled = !IsWindowEnabled(hwnd);
    const bool focused = (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_FOCUS) != 0;
    const bool hot = hoveredButton == hwnd || (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED);
    const COLORREF accent = disabled ? colors.muted : colors.accent;
    FillRect(dc, &rect, panelBrush);
    RECT label = rect;
    const int center = (rect.top + rect.bottom) / 2;
    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        label.right -= scale(48);
        if (hot)
            roundBox(dc, rect, colors.field, colors.field, 8);
        RECT track{rect.right - scale(38), center - scale(10), rect.right - scale(2), center + scale(10)};
        roundBox(dc, track, checked ? accent : colors.border, checked ? accent : colors.border, 20);
        const int thumbX = checked ? track.right - scale(10) : track.left + scale(10);
        HBRUSH thumb = CreateSolidBrush(checked ? RGB(255, 255, 255) : colors.muted);
        const HGDIOBJ oldBrush = SelectObject(dc, thumb), oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, thumbX - scale(7), center - scale(7), thumbX + scale(7), center + scale(7));
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(thumb);
    } else {
        const COLORREF fill = checked ? blend(accent, colors.panel, hot ? 23 : 14)
                              : hot   ? colors.field
                                      : colors.panel;
        roundBox(dc, rect, fill, checked || focused ? accent : colors.border, 10);
        label.left += scale(12);
        label.right -= scale(36);
        const int x = rect.right - scale(21);
        HBRUSH brush = CreateSolidBrush(checked ? accent : fill);
        HPEN pen = CreatePen(PS_SOLID, scale(1), checked ? accent : colors.muted);
        const HGDIOBJ oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
        Ellipse(dc, x - scale(8), center - scale(8), x + scale(8), center + scale(8));
        if (checked) {
            HBRUSH dot = CreateSolidBrush(RGB(255, 255, 255));
            SelectObject(dc, dot);
            SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, x - scale(3), center - scale(3), x + scale(3), center + scale(3));
            SelectObject(dc, brush);
            DeleteObject(dot);
        }
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(brush);
        DeleteObject(pen);
    }
    const LRESULT uiState = SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0);
    drawText(dc, textOf(hwnd), label, bodyFont, disabled ? colors.muted : colors.text,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | ((uiState & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0));
    if (focused && !(uiState & UISF_HIDEFOCUS)) {
        RECT focus = rect;
        InflateRect(&focus, -scale(3), -scale(3));
        SetTextColor(dc, colors.text);
        SetBkColor(dc, colors.panel);
        DrawFocusRect(dc, &focus);
    }
}

void paint() {
    if (textOf(statusLabel) != statusText)
        SetWindowTextW(statusLabel, statusText.c_str());
    const auto hint =
        hotkey::name(preferences.hotkeyModifiers, preferences.hotkeyKey) + L"  -  clipboard / stop     " +
        hotkey::name(preferences.snippetHotkeyModifiers, preferences.snippetHotkeyKey) + L"  -  snippets";
    if (textOf(shortcutHint) != hint)
        SetWindowTextW(shortcutHint, hint.c_str());
    PAINTSTRUCT ps;
    HDC target = BeginPaint(window, &ps);
    const RECT rect = client();
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, rect.right, rect.bottom);
    HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
    FillRect(dc, &rect, backgroundBrush);
    const int w = logicalWidth(), h = logicalHeight(), side = w - 300;
    if (headerIcon)
        DrawIconEx(dc, scale(24), scale(30), headerIcon, scale(44), scale(44), 0, nullptr, DI_NORMAL);
    drawText(dc, L"MK AutoTyper", area(84, 23, 300, 36), titleFont, colors.text);
    drawText(dc, L"Your text. One keystroke at a time.", area(85, 62, 400, 20), smallFont, colors.muted);
    roundBox(dc, area(24, 118, w - 348, h - 214), colors.panel, colors.border);
    roundBox(dc, area(side, 118, 276, h - 214), colors.panel, colors.border);
    roundBox(dc, area(side + 22, 372, 232, 36), colors.field,
        GetFocus() == delayEdit ? colors.accent : colors.border, 8);
    roundBox(dc, area(side + 22, 454, 232, 36), colors.field,
        GetFocus() == initialDelayEdit ? colors.accent : colors.border, 8);
    drawText(dc, L"TYPING SETUP", area(side + 22, 136, 232, 22), smallFont, colors.muted);
    const int length = GetWindowTextLengthW(contentEdit);
    drawText(dc, std::to_wstring(length) + (length == 1 ? L" character" : L" characters"),
        area(w - 492, 138, 120, 24), smallFont, colors.muted, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (typing) {
        RECT track = area(24, h - 89, w - 348, 4);
        FillRect(dc, &track, fieldBrush);
        const auto progress = session.snapshot().progress;
        if (progress.total) {
            track.right = track.left +
                          static_cast<LONG>((track.right - track.left) * progress.completed / progress.total);
            HBRUSH accent = CreateSolidBrush(colors.accent);
            FillRect(dc, &track, accent);
            DeleteObject(accent);
        }
    }
    BitBlt(target, 0, 0, rect.right, rect.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window, &ps);
}

LRESULT CALLBACK editProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT && hwnd == contentEdit) {
        const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
        if (!GetWindowTextLengthW(hwnd) && GetFocus() != hwnd) {
            HDC dc = GetDC(hwnd);
            RECT rect;
            GetClientRect(hwnd, &rect);
            InflateRect(&rect, -scale(3), -scale(3));
            drawText(dc, L"Write or paste your text here...", rect, bodyFont, colors.muted,
                DT_LEFT | DT_TOP | DT_SINGLELINE);
            ReleaseDC(hwnd, dc);
        }
        return result;
    }
    if (message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (wParam == 'A') {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        if (hwnd == contentEdit && wParam == 'Z' && (GetKeyState(VK_SHIFT) & 0x8000)) {
            SendMessageW(hwnd, EM_REDO, 0, 0);
            return 0;
        }
    }
    if (message == WM_CHAR && (wParam == 1 || (wParam == 26 && (GetKeyState(VK_SHIFT) & 0x8000))))
        return 0;
    if (message == WM_CONTEXTMENU && hwnd == contentEdit) {
        CHARRANGE selection;
        SendMessageW(hwnd, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        const bool selected = selection.cpMin != selection.cpMax;
        HMENU menu = CreatePopupMenu();
        auto item = [menu](UINT id, const wchar_t* text, bool enabled) {
            AppendMenuW(menu, MF_STRING | (enabled ? 0 : MF_GRAYED), id, text);
        };
        item(1, L"Undo\tCtrl+Z", SendMessageW(hwnd, EM_CANUNDO, 0, 0) != 0);
        item(2, L"Redo\tCtrl+Y", SendMessageW(hwnd, EM_CANREDO, 0, 0) != 0);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        item(3, L"Cut\tCtrl+X", selected);
        item(4, L"Copy\tCtrl+C", selected);
        item(5, L"Paste\tCtrl+V", IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE);
        item(6, L"Delete", selected);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        item(7, L"Select all\tCtrl+A", GetWindowTextLengthW(hwnd) > 0);
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (point.x == -1 && point.y == -1) {
            GetCaretPos(&point);
            ClientToScreen(hwnd, &point);
        }
        const UINT command =
            TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
        const UINT messages[] = {0, EM_UNDO, EM_REDO, WM_CUT, WM_COPY, WM_PASTE, WM_CLEAR, EM_SETSEL};
        if (command > 0 && command <= 7)
            SendMessageW(hwnd, messages[command], 0, command == 7 ? -1 : 0);
        return 0;
    }
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        InvalidateRect(hwnd, nullptr, FALSE);
        InvalidateRect(window, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, editProc, 1);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK buttonProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (isChoice(hwnd)) {
        if (message == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            paintChoice(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        if (message == WM_PRINTCLIENT) {
            paintChoice(hwnd, reinterpret_cast<HDC>(wParam));
            return 0;
        }
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == BM_SETCHECK || message == BM_SETSTATE || message == WM_SETFOCUS ||
            message == WM_KILLFOCUS || message == WM_ENABLE || message == WM_UPDATEUISTATE) {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            InvalidateRect(hwnd, nullptr, FALSE);
            return result;
        }
    }
    if (message == WM_MOUSEMOVE && hoveredButton != hwnd) {
        hoveredButton = hwnd;
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        InvalidateRect(hwnd, nullptr, FALSE);
    } else if (message == WM_MOUSELEAVE) {
        hoveredButton = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, buttonProc, 1);
    return DefSubclassProc(hwnd, message, wParam, lParam);
}

bool copyClipboardText(const wchar_t* data, size_t capacity, secure::Text& text) {
    text.clear();
    if (!data || !capacity)
        return false;
    const size_t limit = (std::min)(capacity, size_t(1024 * 1024 + 1));
    const wchar_t* end = std::wmemchr(data, L'\0', limit);
    if (!end)
        return false;
    text = secure::Text(data, static_cast<size_t>(end - data));
    return true;
}

bool clipboardText(secure::Text& text) {
    text.clear();
    if (!OpenClipboard(window))
        return false;

    struct Close {
        ~Close() {
            CloseClipboard();
        }
    } close;

    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    const auto* buffer = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
    if (!buffer)
        return false;

    struct Unlock {
        HANDLE data;

        ~Unlock() {
            GlobalUnlock(data);
        }
    } unlock{data};

    try {
        return copyClipboardText(buffer, GlobalSize(data) / sizeof(wchar_t), text);
    } catch (...) {
        return false;
    }
}

struct ModalScope {
    bool previous = dialogOpen;

    ModalScope() {
        dialogOpen = true;
    }

    ~ModalScope() {
        dialogOpen = previous;
    }
};

dialogs::Style dialogStyle() {
    return {instance, dpi, bodyFont, headingFont, colors.panel, colors.field, colors.text, colors.muted,
        colors.border, colors.accent, darkTheme};
}

int showMessage(HWND owner, const wchar_t* text, const wchar_t* title, UINT flags) {
    ModalScope modal;
    return dialogs::message(owner, dialogStyle(), text, title, flags);
}

bool snippetCatalog(dialogs::State& state) {
    std::vector<std::wstring> plain, locked;
    if (!snippetStore.list(plain)) {
        showMessage(
            window, L"The saved snippets could not be listed.", L"Snippets", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    const auto result = encryptedStore.list(locked);
    if (result != encrypted::Result::ok) {
        showMessage(window, passwords::errorText(result), L"Encrypted snippets", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    std::vector<std::pair<std::wstring, bool>> entries;
    for (const auto& name : plain)
        entries.emplace_back(name, false);
    for (const auto& name : locked)
        entries.emplace_back(name, true);
    std::sort(entries.begin(), entries.end(),
        [](const std::pair<std::wstring, bool>& a, const std::pair<std::wstring, bool>& b) {
            const int order = snippets::compareNames(a.first, b.first);
            return order == CSTR_EQUAL ? a.second < b.second : order == CSTR_LESS_THAN;
        });
    state.names.clear();
    state.encrypted.clear();
    for (const auto& entry : entries) {
        state.names.push_back(entry.first);
        state.encrypted.push_back(entry.second);
    }
    return true;
}

bool protectedAction(HWND owner, passwords::Mode mode,
    std::function<encrypted::Result(const secure::Text&, const secure::Text&)> action,
    const RECT* placement = nullptr) {
    ModalScope modal;
    passwords::State state;
    state.ui.style = dialogStyle();
    state.mode = mode;
    state.action = std::move(action);
    state.placement = placement;
    const bool hadTyping = shortcut.registered(), hadSearch = snippetShortcut.registered();
    // Configurable Ctrl+Alt combinations must not consume AltGr characters
    // while the user enters a master password. Preserve a parent settings pause.
    shortcut.clear();
    snippetShortcut.clear();
    const bool accepted = passwords::show(owner, state);
    const bool typingRestored =
        !hadTyping || shortcut.replace(window, preferences.hotkeyModifiers, preferences.hotkeyKey);
    const bool searchRestored =
        !hadSearch ||
        snippetShortcut.replace(window, preferences.snippetHotkeyModifiers, preferences.snippetHotkeyKey);
    if (!typingRestored || !searchRestored)
        showMessage(owner,
            L"A global shortcut could not be restored. Choose an available combination in More settings.",
            L"Shortcut unavailable", MB_OK | MB_ICONINFORMATION);
    return accepted;
}

bool loadSnippetText(
    const std::wstring& name, bool encrypted, secure::Text& text, const RECT* placement = nullptr) {
    if (encrypted)
        return protectedAction(
            window, passwords::Mode::unlock,
            [&](const secure::Text& password, const secure::Text&) {
                return encryptedStore.load(name, password, text);
            },
            placement);
    const auto result = snippetStore.load(name, text);
    if (result != snippets::Result::ok) {
        showMessage(window, snippetErrorText(result), L"Snippets", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

void changeMasterPassword(HWND owner) {
    if (!encryptedStore.exists()) {
        showMessage(owner, L"Save an encrypted snippet to set your master password.", L"Master password",
            MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (protectedAction(
            owner, passwords::Mode::change, [](const secure::Text& old, const secure::Text& next) {
                return encryptedStore.changePassword(old, next);
            })) {
        statusText = L"Master password updated for all encrypted snippets";
        InvalidateRect(window, nullptr, FALSE);
    }
}

std::wstring hotkeyDisplayText(HWND field) {
    UINT modifiers = 0, key = 0;
    dialogs::readHotkey(field, modifiers, key);
    return key ? hotkey::name(modifiers, key) : L"";
}

void positionHotkeyCaret(HWND field, bool recreate = false) {
    if (GetFocus() != field)
        return;
    HDC dc = GetDC(field);
    const HGDIOBJ previous = SelectObject(dc, bodyFont);
    const auto label = hotkeyDisplayText(field);
    SIZE text{};
    TEXTMETRICW metrics{};
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &text);
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(field, dc);
    RECT bounds;
    GetClientRect(field, &bounds);
    if (recreate) {
        DestroyCaret();
        CreateCaret(field, nullptr, std::max(1, scale(1)), metrics.tmHeight);
        ShowCaret(field);
    }
    SetCaretPos(std::min(bounds.right - scale(8), scale(10) + text.cx),
        std::max<LONG>(0, (bounds.bottom - metrics.tmHeight) / 2));
}

LRESULT CALLBACK hotkeyFieldProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT || message == WM_PRINTCLIENT) {
        PAINTSTRUCT ps = {};
        HDC dc = message == WM_PAINT ? BeginPaint(hwnd, &ps) : reinterpret_cast<HDC>(wParam);
        RECT rect;
        GetClientRect(hwnd, &rect);
        roundBox(dc, rect, colors.field, GetFocus() == hwnd ? colors.accent : colors.border, 8);
        const WORD chosen = static_cast<WORD>(SendMessageW(hwnd, HKM_GETHOTKEY, 0, 0));
        const BYTE flags = HIBYTE(chosen);
        const UINT modifiers = ((flags & HOTKEYF_CONTROL) ? MOD_CONTROL : 0) |
                               ((flags & HOTKEYF_ALT) ? MOD_ALT : 0) |
                               ((flags & HOTKEYF_SHIFT) ? MOD_SHIFT : 0);
        rect.left += scale(10);
        drawText(dc, LOBYTE(chosen) ? hotkey::name(modifiers, LOBYTE(chosen)) : L"Press a shortcut...", rect,
            bodyFont, colors.text);
        if (message == WM_PAINT)
            EndPaint(hwnd, &ps);
        positionHotkeyCaret(hwnd);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, hotkeyFieldProc, 1);
    const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
    if (message == WM_SETFOCUS)
        positionHotkeyCaret(hwnd, true);
    if (message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS || message == HKM_SETHOTKEY) {
        positionHotkeyCaret(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    return result;
}

void chooseTheme(decltype(&TrackPopupMenuEx) trackMenu = &TrackPopupMenuEx) {
    ModalScope modal;
    dialogs::ThemeMenu menu(dialogStyle(), preferences.theme);
    if (!menu.ready)
        return;
    RECT anchor;
    GetWindowRect(themeButton, &anchor);
    TPMPARAMS position{sizeof(position)};
    position.rcExclude = anchor;
    activeThemeMenu = menu.handle;
    const UINT command =
        trackMenu(menu.handle, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
            anchor.right, anchor.bottom + scale(4), window, &position);
    activeThemeMenu = nullptr;
    if (dialogs::isThemeCommand(command)) {
        preferences.theme = static_cast<settings::ThemeMode>(command - dialogs::ThemeSystem);
        applyTheme();
        persistPreferences();
    }
}

void openSettings() {
    if (typing)
        return;
    persistPreferences();
    ModalScope modal;
    dialogs::State state;
    state.style = dialogStyle();
    state.title = L"Typing settings";
    state.options = true;
    state.draft = preferences;
    state.changePassword = changeMasterPassword;
    state.decorate = [](HWND child) {
        wchar_t name[64];
        GetClassNameW(child, name, 64);
        if (_wcsicmp(name, HOTKEY_CLASSW) == 0) {
            SetWindowLongPtrW(child, GWL_EXSTYLE, GetWindowLongPtrW(child, GWL_EXSTYLE) & ~WS_EX_CLIENTEDGE);
            SetWindowPos(child, nullptr, 0, 0, 0, 0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            SetWindowSubclass(child, hotkeyFieldProc, 1, 0);
        } else
            SetWindowSubclass(child, buttonProc, 1, 0);
    };
    state.apply = [](settings::Preferences& draft) {
        shortcut.clear();
        snippetShortcut.clear();
        if (!shortcut.replace(window, draft.hotkeyModifiers, draft.hotkeyKey) ||
            !snippetShortcut.replace(window, draft.snippetHotkeyModifiers, draft.snippetHotkeyKey)) {
            shortcut.clear();
            snippetShortcut.clear();
            return false;
        }
        preferences = draft;
        persistPreferences();
        return true;
    };
    // A registered global shortcut never reaches the native hotkey field.
    // Pause it for this modal editor, including retries after a conflict.
    shortcut.clear();
    snippetShortcut.clear();
    dialogs::show(window, state);
    if (!shortcut.replace(window, preferences.hotkeyModifiers, preferences.hotkeyKey))
        showMessage(window,
            L"The start / stop shortcut could not be restored. Another application may have claimed "
            L"it. Choose an available shortcut in More settings.",
            L"Shortcut unavailable", MB_OK | MB_ICONINFORMATION);
    if (!snippetShortcut.replace(window, preferences.snippetHotkeyModifiers, preferences.snippetHotkeyKey))
        showMessage(window,
            L"The snippet search shortcut could not be restored. Choose an available shortcut in "
            L"More settings.",
            L"Shortcut unavailable", MB_OK | MB_ICONINFORMATION);
    InvalidateRect(window, nullptr, FALSE);
}

std::wstring menuText(const std::wstring& text) {
    std::wstring escaped;
    for (wchar_t c : text) {
        if (c == L'&')
            escaped += c;
        escaped += c;
    }
    return escaped;
}

bool encryptionChecked() {
    return SendMessageW(encryptSnippetCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

bool editorModified() {
    return GetWindowTextLengthW(contentEdit) > 0 &&
           (SendMessageW(contentEdit, EM_GETMODIFY, 0, 0) || encryptionChecked() != selectedEncrypted);
}

void updateEditorActions() {
    SetWindowTextW(snippetMenuButton, (selectedSnippet.empty() ? L"Saved snippets \u25be"
                                                               : (selectedEncrypted ? L"[Encrypted] " : L"") +
                                                                     menuText(selectedSnippet) + L" \u25be")
                                          .c_str());
    SetWindowTextW(editorLabel, selectedEncrypted ? L"Encrypted snippet" : L"&Text to type");
    EnableWindow(renameSnippetButton, !typing && !selectedSnippet.empty());
    EnableWindow(deleteSnippetButton, !typing && !selectedSnippet.empty());
    const std::wstring title = L"MK AutoTyper" +
                               (selectedSnippet.empty() ? std::wstring{} : L" - " + selectedSnippet) +
                               (editorModified() ? L" *" : L"");
    SetWindowTextW(window, title.c_str());
}

void clearProtectedEditor() {
    if (!selectedEncrypted && !encryptionChecked())
        return;
    SetWindowTextW(contentEdit, L"");
    SendMessageW(contentEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
    selectedSnippet.clear();
    selectedEncrypted = false;
    SendMessageW(encryptSnippetCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    EnableWindow(contentEdit, TRUE);
    updateEditorActions();
}

const wchar_t* snippetErrorText(snippets::Result result) {
    switch (result) {
    case snippets::Result::invalidName:
        return L"Use a name of 1-80 characters without control characters or invalid Unicode.";
    case snippets::Result::exists:
        return L"A snippet with that name already exists. Choose a different name.";
    case snippets::Result::invalidText:
        return L"The snippet contains invalid text encoding.";
    case snippets::Result::invalidDocument:
        return L"The snippet document is damaged or invalid. It was not changed.";
    case snippets::Result::unsupportedFormat:
        return L"This snippet uses an unsupported document version or action. It was not changed.";
    case snippets::Result::unsupportedActions:
        return L"This snippet contains steps this version cannot edit or run. It was not changed.";
    case snippets::Result::tooLarge:
        return L"The snippet is too large for this editor.";
    default:
        return L"The snippet could not be read or saved. Check the file and folder permissions.";
    }
}

void snippetError(snippets::Result result) {
    showMessage(window, snippetErrorText(result), L"Snippets", MB_OK | MB_ICONINFORMATION);
}

bool promptNewSnippetName(std::wstring& name) {
    for (;;) {
        dialogs::State state;
        state.style = dialogStyle();
        state.title = L"Save snippet";
        state.name = name;
        if (dialogs::show(window, state) != IDOK)
            return false;
        name = state.name;
        if (!snippets::validName(name)) {
            snippetError(snippets::Result::invalidName);
            continue;
        }
        dialogs::State catalog;
        if (!snippetCatalog(catalog))
            return false;
        const bool collision = std::any_of(catalog.names.begin(), catalog.names.end(),
            [&](const std::wstring& existing) { return snippets::sameName(existing, name); });
        if (!collision)
            return true;
        snippetError(snippets::Result::exists);
    }
}

bool saveSnippetContents(const std::wstring& name, bool creating, bool encrypt) {
    const bool converting = !creating && encrypt != selectedEncrypted;
    auto document = snippetdocument::Document::fromText(secure::Text::read(contentEdit));
    if (encrypt || converting) {
        const bool setup = !encryptedStore.exists() && encrypt;
        return protectedAction(window, setup ? passwords::Mode::create : passwords::Mode::unlock,
            [&](const secure::Text& password, const secure::Text& next) {
                const auto& master = setup ? next : password;
                if (converting)
                    return encryptedStore.convert(name, document, master, encrypt, snippetStore);
                return encryptedStore.save(name, document, master, !creating);
            });
    }
    const auto result = snippetStore.save(name, document, !creating);
    if (result == snippets::Result::ok)
        return true;
    snippetError(result);
    return false;
}

bool saveSnippet() {
    if (typing)
        return false;
    ModalScope modal;
    const bool creating = selectedSnippet.empty();
    const bool encrypt = encryptionChecked();
    std::wstring name = selectedSnippet;
    if (creating && !promptNewSnippetName(name))
        return false;
    if (!saveSnippetContents(name, creating, encrypt))
        return false;

    selectedEncrypted = encrypt;
    selectedSnippet = name;
    SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
    statusText = encrypt ? L"Encrypted snippet saved" : L"Snippet saved (unencrypted)";
    updateEditorActions();
    InvalidateRect(window, nullptr, FALSE);
    return true;
}

bool confirmEditorChange() {
    if (!editorModified())
        return true;
    ModalScope modal;
    const int choice = showMessage(window, L"Save your editor changes as a snippet before continuing?",
        L"Unsaved text", MB_YESNOCANCEL | MB_ICONQUESTION);
    return choice == IDNO || (choice == IDYES && saveSnippet());
}

void newSnippet() {
    if (typing || !confirmEditorChange())
        return;
    clearProtectedEditor();
    selectedSnippet.clear();
    selectedEncrypted = false;
    SendMessageW(encryptSnippetCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowTextW(contentEdit, L"");
    SendMessageW(contentEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
    updateEditorActions();
    SetFocus(contentEdit);
}

void chooseSnippet() {
    if (typing)
        return;
    dialogs::State picker;
    picker.style = dialogStyle();
    picker.title = L"Saved snippets";
    if (!snippetCatalog(picker))
        return;
    picker.picker = true;
    picker.name = selectedSnippet;
    {
        ModalScope modal;
        if (dialogs::show(window, picker) != IDOK)
            return;
    }
    if (!confirmEditorChange())
        return;
    secure::Text text;
    if (!loadSnippetText(picker.name, picker.encrypt, text))
        return;
    SendMessageW(contentEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    selectedEncrypted = picker.encrypt;
    SendMessageW(encryptSnippetCheck, BM_SETCHECK, selectedEncrypted ? BST_CHECKED : BST_UNCHECKED, 0);
    selectedSnippet = picker.name;
    SetWindowTextW(contentEdit, text.c_str());
    text.clear();
    SendMessageW(contentEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageW(contentEdit, EM_SETMODIFY, FALSE, 0);
    updateEditorActions();
    SetFocus(contentEdit);
}

void renameSnippet() {
    if (typing || selectedSnippet.empty())
        return;
    ModalScope modal;
    dialogs::State state;
    state.style = dialogStyle();
    state.title = L"Rename snippet";
    state.name = selectedSnippet;
    while (dialogs::show(window, state) == IDOK) {
        dialogs::State catalog;
        if (!snippetCatalog(catalog))
            return;
        bool collision = false;
        for (size_t i = 0; i < catalog.names.size(); ++i)
            if (snippets::sameName(catalog.names[i], state.name) &&
                (!snippets::sameName(catalog.names[i], selectedSnippet) ||
                    catalog.encrypted[i] != selectedEncrypted))
                collision = true;
        if (collision) {
            snippetError(snippets::Result::exists);
            continue;
        }
        if (selectedEncrypted) {
            if (protectedAction(
                    window, passwords::Mode::unlock, [&](const secure::Text& password, const secure::Text&) {
                        return encryptedStore.rename(selectedSnippet, state.name, password);
                    })) {
                selectedSnippet = state.name;
                updateEditorActions();
            }
            return;
        }
        const auto result = snippetStore.rename(selectedSnippet, state.name);
        if (result == snippets::Result::ok) {
            selectedSnippet = state.name;
            updateEditorActions();
            return;
        }
        snippetError(result);
        if (result != snippets::Result::invalidName && result != snippets::Result::exists)
            return;
    }
}

void deleteSnippet() {
    if (typing || selectedSnippet.empty())
        return;
    ModalScope modal;
    const std::wstring question =
        L"Delete the saved snippet '" + selectedSnippet + L"'?\nThe text will remain in the editor.";
    if (showMessage(
            window, question.c_str(), L"Delete snippet", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES)
        return;
    if (selectedEncrypted) {
        if (!protectedAction(
                window, passwords::Mode::unlock, [&](const secure::Text& password, const secure::Text&) {
                    return encryptedStore.remove(selectedSnippet, password);
                }))
            return;
    } else {
        const auto result = snippetStore.remove(selectedSnippet);
        if (result != snippets::Result::ok) {
            snippetError(result);
            return;
        }
    }
    selectedSnippet.clear();
    SendMessageW(contentEdit, EM_SETMODIFY, TRUE, 0);
    updateEditorActions();
}

void stopSession() {
    if (!typing)
        return;
    session.requestStop();
    statusText = L"Stopping...";
    InvalidateRect(window, nullptr, FALSE);
}

void setSessionControls(bool active) {
    SetWindowTextW(typeButton, active ? L"Stop t&yping" : L"Start t&yping");
    for (HWND child : {settingsButton, snippetMenuButton, newSnippetButton, saveSnippetButton,
             encryptSnippetCheck, scanRadio, unicodeRadio, delayEdit, initialDelayEdit})
        EnableWindow(child, !active);
    if (selectedEncrypted || encryptionChecked())
        EnableWindow(contentEdit, !active);
    updateEditorActions();
}

void startTyping(secure::Text text, bool hideWindow, HWND target = nullptr) {
    if (typing) {
        stopSession();
        return;
    }
    if (!keyboard::hasText(text)) {
        clearProtectedEditor();
        SetFocus(contentEdit);
        return;
    }
    persistPreferences();
    keyboard::Options options;
    options.delay = preferences.keystrokeDelay;
    options.initialDelay = preferences.initialDelay;
    options.unicode = SendMessageW(unicodeRadio, BM_GETCHECK, 0, 0) == BST_CHECKED;
    options.shiftEnter = preferences.shiftEnter;
    options.triggerKey = preferences.hotkeyKey;
    options.hotkeyModifiers = preferences.hotkeyModifiers;
    options.snippetHotkeyModifiers = preferences.snippetHotkeyModifiers;
    options.snippetHotkeyKey = preferences.snippetHotkeyKey;
    options.target = target ? target : hideWindow ? nullptr : GetForegroundWindow();
    if (!hideWindow && options.target == window) {
        clearProtectedEditor();
        return;
    }
    restoreAfterTyping = hideWindow;
    sessionPanel.close();
    if (hideWindow)
        ShowWindow(window, SW_HIDE);
    if (!session.start(std::move(text), options)) {
        clearProtectedEditor();
        if (hideWindow)
            ShowWindow(window, SW_SHOWNOACTIVATE);
        statusText = L"Unable to start typing";
        InvalidateRect(window, nullptr, FALSE);
        return;
    }
    typing = true;
    setSessionControls(true);
    SetTimer(window, SessionTimer, 100, nullptr);
    statusText = L"Preparing to type...";
    InvalidateRect(window, nullptr, FALSE);
}

void openSnippetSearch() {
    if (typing || dialogOpen)
        return;
    const auto destination = typing_target::Destination::capture();
    const HWND target = destination.window;
    DWORD process = 0;
    GetWindowThreadProcessId(target, &process);
    if (!destination.valid() || process == GetCurrentProcessId())
        return;
    snippetsearch::State state;
    state.ui.style = dialogStyle();
    state.target = target;
    if (!snippetCatalog(state.ui))
        return;
    INT_PTR result;
    {
        ModalScope modal;
        result = snippetsearch::show(window, state);
    }
    if (result != IDOK) {
        if (GetForegroundWindow() == window)
            destination.restore();
        return;
    }
    secure::Text text;
    if (!loadSnippetText(state.ui.name, state.ui.encrypt, text, &state.bounds)) {
        if (GetForegroundWindow() == window)
            destination.restore();
        return;
    }
    if (!keyboard::hasText(text)) {
        showMessage(
            window, L"This saved snippet has no text to type.", L"Empty snippet", MB_OK | MB_ICONINFORMATION);
        return;
    }
    // Restore the original destination before the worker starts. Its existing
    // foreground checks stop typing if the user changes apps during the delay.
    if (!destination.restore()) {
        showMessage(window,
            L"The destination window is no longer available. Focus it and open snippet search again.",
            L"Cannot type snippet", MB_OK | MB_ICONINFORMATION);
        return;
    }
    startTyping(std::move(text), false, target);
}

void updateSession() {
    if (!typing)
        return;
    const auto snapshot = session.snapshot();
    if (snapshot.finished) {
        session.join();
        KillTimer(window, SessionTimer);
        sessionPanel.close();
        clearProtectedEditor();
        typing = false;
        setSessionControls(false);
        statusText = snapshot.result == keyboard::Result::completed ? L"Finished: " : L"Stopped: ";
        statusText += std::to_wstring(snapshot.progress.completed) + L" / " +
                      std::to_wstring(snapshot.progress.total) + L" characters";
        if (restoreAfterTyping)
            ShowWindow(window, SW_SHOWNOACTIVATE);
        if (snapshot.result == keyboard::Result::unsupportedLayout)
            showMessage(window, L"Select an English or German keyboard layout in the target application.",
                L"MK AutoTyper", MB_OK | MB_ICONINFORMATION);
        else if (snapshot.result == keyboard::Result::inputBlocked)
            showMessage(window,
                L"Windows could not send all keystrokes. Run the target and AutoTyper at the same "
                L"permission level.",
                L"MK AutoTyper", MB_OK | MB_ICONERROR);
    } else {
        statusText = session.stopping() ? L"Stopping..." : sessionview::summary(snapshot);
        sessionPanel.update(GetForegroundWindow(),
            {instance, dpi, bodyFont, headingFont, colors.panel, colors.text, colors.muted, colors.border,
                colors.accent, darkTheme},
            snapshot, preferences.showCountdown, session.stopping(),
            hotkey::name(preferences.hotkeyModifiers, preferences.hotkeyKey), stopSession);
    }
    InvalidateRect(window, nullptr, FALSE);
}

void toggleTypingShortcut() {
    if (typing) {
        stopSession();
        return;
    }
    if (dialogOpen)
        return;
    secure::Text text;
    if (clipboardText(text))
        startTyping(std::move(text), false);
}

HWND control(const wchar_t* kind, const wchar_t* title, DWORD style, int id = 0, DWORD extended = 0) {
    return CreateWindowExW(extended, kind, title, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
}

bool createControls() {
    // Creation order also defines accessible labels, tab order and mnemonic targets.
    editorLabel = control(L"STATIC", L"&Text to type", SS_LEFT);
    contentEdit = control(MSFTEDIT_CLASS, L"",
        WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | ES_NOHIDESEL, Content);
    if (!contentEdit)
        return false;
    SendMessageW(contentEdit, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(contentEdit, EM_EXLIMITTEXT, 0, 1024 * 1024);
    SendMessageW(contentEdit, EM_SETUNDOLIMIT, 200, 0);
    SendMessageW(contentEdit, EM_SETEVENTMASK, 0, ENM_CHANGE);
    snippetMenuButton = control(L"BUTTON", L"Saved snippets \u25be", WS_TABSTOP | BS_OWNERDRAW, SnippetMenu);
    newSnippetButton = control(L"BUTTON", L"New", WS_TABSTOP | BS_OWNERDRAW, NewSnippet);
    saveSnippetButton = control(L"BUTTON", L"Save", WS_TABSTOP | BS_OWNERDRAW, SaveSnippet);
    encryptSnippetCheck =
        control(L"BUTTON", L"Encrypt snippet", WS_TABSTOP | BS_AUTOCHECKBOX, EncryptSnippet);
    renameSnippetButton = control(L"BUTTON", L"Rename", WS_TABSTOP | BS_OWNERDRAW, RenameSnippet);
    deleteSnippetButton = control(L"BUTTON", L"Delete", WS_TABSTOP | BS_OWNERDRAW, DeleteSnippet);
    modeLabel = control(L"STATIC", L"INPUT METHOD", SS_LEFT);
    scanRadio = control(L"BUTTON", L"&Scan-code input", WS_TABSTOP | WS_GROUP | BS_AUTORADIOBUTTON, ScanMode);
    unicodeRadio = control(L"BUTTON", L"&Unicode input", BS_AUTORADIOBUTTON, UnicodeMode);
    SendMessageW(scanRadio, BM_SETCHECK, BST_CHECKED, 0); // Preserve the original default on every launch.
    modeHelp = control(L"STATIC", L"Simulates keys using the target's\nEnglish or German keyboard layout.",
        SS_LEFT | WS_GROUP);
    delayLabel = control(L"STATIC", L"&Keystroke delay (ms)", SS_LEFT);
    delayEdit = control(L"EDIT", std::to_wstring(preferences.keystrokeDelay).c_str(),
        WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, Delay);
    initialLabel = control(L"STATIC", L"&Initial delay (ms)", SS_LEFT);
    initialDelayEdit = control(L"EDIT", std::to_wstring(preferences.initialDelay).c_str(),
        WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, InitialDelay);
    startupCheck = control(L"BUTTON", L"Start with Windows", WS_TABSTOP | BS_AUTOCHECKBOX, Startup);
    SendMessageW(startupCheck, BM_SETCHECK, startup::enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    preferencesNote = control(L"STATIC", L"Starts hidden in the system tray.", SS_LEFT);
    themeButton =
        control(L"BUTTON", settings::themeLabel(preferences.theme), WS_TABSTOP | BS_OWNERDRAW, Theme);
    settingsButton = control(L"BUTTON", L"More settings", WS_TABSTOP | BS_OWNERDRAW, MoreSettings);
    typeButton = control(L"BUTTON", L"Start t&yping", WS_TABSTOP | BS_OWNERDRAW, Start);
    statusLabel = control(L"STATIC", statusText.c_str(), SS_LEFT | SS_ENDELLIPSIS);
    shortcutHint = control(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS);
    for (HWND edit : {contentEdit, delayEdit, initialDelayEdit})
        SetWindowSubclass(edit, editProc, 1, 0);
    for (HWND button : {themeButton, typeButton, scanRadio, unicodeRadio, startupCheck, settingsButton,
             snippetMenuButton, newSnippetButton, saveSnippetButton, encryptSnippetCheck, renameSnippetButton,
             deleteSnippetButton})
        SetWindowSubclass(button, buttonProc, 1, 0);
    SendMessageW(delayEdit, EM_SETLIMITTEXT, 5, 0);
    SendMessageW(initialDelayEdit, EM_SETLIMITTEXT, 5, 0);
    return editorLabel && modeLabel && scanRadio && unicodeRadio && modeHelp && delayLabel && delayEdit &&
           initialLabel && initialDelayEdit && startupCheck && preferencesNote && themeButton && typeButton &&
           settingsButton && snippetMenuButton && newSnippetButton && saveSnippetButton &&
           encryptSnippetCheck && renameSnippetButton && deleteSnippetButton && statusLabel && shortcutHint;
}

HICON sizedIcon(int resource, int widthMetric, int heightMetric, UINT iconDpi) {
    return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(resource), IMAGE_ICON,
        GetSystemMetricsForDpi(widthMetric, iconDpi), GetSystemMetricsForDpi(heightMetric, iconDpi), 0));
}

void updateWindowIcons() {
    const HICON largeIcon = sizedIcon(IDI_AUTOTYPER, SM_CXICON, SM_CYICON, dpi);
    const HICON smallIcon = sizedIcon(IDI_SMALL, SM_CXSMICON, SM_CYSMICON, dpi);
    if (largeIcon) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(largeIcon));
        if (windowLargeIcon)
            DestroyIcon(windowLargeIcon);
        windowLargeIcon = largeIcon;
    }
    if (smallIcon) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
        if (windowSmallIcon)
            DestroyIcon(windowSmallIcon);
        windowSmallIcon = smallIcon;
    }
    const HICON brandIcon = static_cast<HICON>(
        LoadImageW(instance, MAKEINTRESOURCEW(IDI_AUTOTYPER), IMAGE_ICON, scale(44), scale(44), 0));
    if (headerIcon)
        DestroyIcon(headerIcon);
    headerIcon = brandIcon;
}

void addTrayIcon() {
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    tray.uCallbackMessage = TrayMessage;
    tray.hIcon = sizedIcon(IDI_SMALL, SM_CXSMICON, SM_CYSMICON, GetDpiForSystem());
    lstrcpyW(tray.szTip, L"MK AutoTyper");
    Shell_NotifyIconW(NIM_ADD, &tray);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        window = hwnd;
        dpi = GetDpiForWindow(hwnd);
        if (!createControls())
            return -1;
        createFonts();
        applyTheme();
        initializing = false;
        updateWindowIcons();
        updateEditorActions();
        return 0;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
        if (bodyFont && contentEdit && preferences.theme == settings::ThemeMode::system &&
            darkTheme != settings::usesDarkTheme(preferences.theme)) {
            if (activeThemeMenu)
                EndMenu();
            applyTheme();
            if (typing)
                updateSession();
        }
        return 0;
    case WM_TIMER:
        if (wParam == SessionTimer)
            updateSession();
        return 0;
    case WM_EXITSIZEMOVE:
        persistPreferences();
        return 0;
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED && contentEdit)
            layout();
        if (!restoringPlacement)
            placement::capture(hwnd, preferences.position);
        return 0;
    case WM_MOVE:
        if (!restoringPlacement)
            placement::capture(hwnd, preferences.position);
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize = {scale(900), scale(690)};
        return 0;
    }
    case WM_DPICHANGED: {
        dpi = HIWORD(wParam);
        createFonts();
        updateWindowIcons();
        const auto* bounds = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
            bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        const HWND child = reinterpret_cast<HWND>(lParam);
        const bool field = child == delayEdit || child == initialDelayEdit;
        const bool muted = child == modeHelp || child == preferencesNote;
        if (child == statusLabel || child == shortcutHint) {
            SetTextColor(dc, child == shortcutHint ? colors.muted : colors.text);
            SetBkColor(dc, colors.background);
            return reinterpret_cast<LRESULT>(backgroundBrush);
        }
        SetTextColor(dc, muted ? colors.muted : colors.text);
        SetBkColor(dc, field ? colors.field : colors.panel);
        return reinterpret_cast<LRESULT>(field ? fieldBrush : panelBrush);
    }
    case WM_MEASUREITEM: {
        auto& item = *reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (item.CtlType == ODT_MENU && dialogs::isThemeCommand(item.itemID)) {
            item.itemWidth = scale(192);
            item.itemHeight = scale(36);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (item->CtlType == ODT_MENU && dialogs::isThemeCommand(item->itemID)) {
            dialogs::drawThemeChoice(dialogStyle(), *item);
            return TRUE;
        }
        const bool primary = item->hwndItem == typeButton;
        if (item->CtlType != ODT_BUTTON)
            break;
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        const bool active = hoveredButton == item->hwndItem || (item->itemState & ODS_SELECTED);
        const COLORREF fill = primary  ? (disabled    ? colors.border
                                             : active ? colors.hover
                                                      : colors.accent)
                              : active ? colors.field
                                       : colors.panel;
        FillRect(item->hDC, &item->rcItem, backgroundBrush);
        roundBox(item->hDC, item->rcItem, fill, primary ? fill : colors.border, 10);
        drawText(item->hDC, textOf(item->hwndItem), item->rcItem, primary ? headingFont : bodyFont,
            disabled  ? colors.muted
            : primary ? RGB(255, 255, 255)
                      : colors.text,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (item->itemState & ODS_FOCUS) {
            RECT focus = item->rcItem;
            InflateRect(&focus, -scale(4), -scale(4));
            DrawFocusRect(item->hDC, &focus);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam), notification = HIWORD(wParam);
        if (id == ExitCommand) {
            stopSession();
            if (confirmEditorChange())
                DestroyWindow(hwnd);
        } else if (id == StopCommand)
            stopSession();
        else if (id == Start && notification == BN_CLICKED)
            startTyping(secure::Text::read(contentEdit), true);
        else if (id == MoreSettings && notification == BN_CLICKED)
            openSettings();
        else if (id == SnippetMenu && notification == BN_CLICKED)
            chooseSnippet();
        else if (id == NewSnippet && notification == BN_CLICKED)
            newSnippet();
        else if (id == SaveSnippet && notification == BN_CLICKED)
            saveSnippet();
        else if (id == EncryptSnippet && notification == BN_CLICKED)
            updateEditorActions();
        else if (id == RenameSnippet && notification == BN_CLICKED)
            renameSnippet();
        else if (id == DeleteSnippet && notification == BN_CLICKED)
            deleteSnippet();
        else if (id == Theme && notification == BN_CLICKED)
            chooseTheme();
        else if (id == Startup && notification == BN_CLICKED) {
            const bool enable = SendMessageW(startupCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!startup::setEnabled(enable)) {
                SendMessageW(startupCheck, BM_SETCHECK, startup::enabled() ? BST_CHECKED : BST_UNCHECKED, 0);
                showMessage(hwnd,
                    L"The Startup shortcut could not be updated. Check that your Windows Startup "
                    L"folder is writable.",
                    L"MK AutoTyper", MB_OK | MB_ICONERROR);
            } else {
                statusText = enable ? L"Startup enabled: system tray only" : L"Startup disabled";
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        } else if ((id == ScanMode || id == UnicodeMode) && notification == BN_CLICKED) {
            SetWindowTextW(modeHelp,
                id == ScanMode ? L"Simulates keys using the target's\nEnglish or German keyboard layout."
                               : L"Sends characters directly using\nWindows Unicode text events.");
        } else if ((id == Delay || id == InitialDelay) && notification == EN_KILLFOCUS)
            persistPreferences();
        else if (id == Content && notification == EN_CHANGE) {
            updateEditorActions();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_HOTKEY:
        if (shortcut.matches(wParam))
            toggleTypingShortcut();
        else if (snippetShortcut.matches(wParam))
            openSnippetSearch();
        return 0;
    case WM_CLOSE:
        if (selectedEncrypted || encryptionChecked()) {
            if (!confirmEditorChange())
                return 0;
            clearProtectedEditor();
        }
        persistPreferences();
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case TrayMessage:
        if (lParam == WM_LBUTTONUP) {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT point;
            GetCursorPos(&point);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | (typing ? 0 : MF_GRAYED), StopCommand, L"Stop typing");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, ExitCommand, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(hwnd, WM_NULL, 0, 0);
        }
        return 0;
    case WM_DESTROY:
        persistPreferences();
        KillTimer(hwnd, SessionTimer);
        session.requestStop();
        session.join();
        sessionPanel.close();
        clearProtectedEditor();
        shortcut.clear();
        snippetShortcut.clear();
        Shell_NotifyIconW(NIM_DELETE, &tray);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace

int WINAPI WinMain(
    _In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPSTR commandLine, _In_ int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::string arguments(commandLine);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, WindowClass);
    if (!mutex)
        return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(WindowClass, nullptr);
        if (other && arguments.find("/tray") == std::string::npos) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        CloseHandle(mutex);
        return 0;
    }
    HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!richEdit) {
        CloseHandle(mutex);
        return 1;
    }
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_HOTKEY_CLASS};
    InitCommonControlsEx(&controls);
    instance = hInstance;
    preferences = settings::load();
    WNDCLASSW wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_AUTOTYPER));
    wc.lpszClassName = WindowClass;
    int result = 1;
    if (RegisterClassW(&wc)) {
        const UINT systemDpi = GetDpiForSystem();
        window = CreateWindowExW(WS_EX_CONTROLPARENT, WindowClass, L"MK AutoTyper",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, MulDiv(1040, systemDpi, 96),
            MulDiv(760, systemDpi, 96), nullptr, nullptr, instance, nullptr);
        if (window) {
            placement::restore(window, preferences.position);
            restoringPlacement = false;
            layout();
            placement::capture(window, preferences.position);
            if (!shortcut.replace(window, preferences.hotkeyModifiers, preferences.hotkeyKey))
                statusText = L"Shortcut unavailable - choose another in More settings";
            if (!snippetShortcut.replace(
                    window, preferences.snippetHotkeyModifiers, preferences.snippetHotkeyKey))
                statusText = L"Snippet shortcut unavailable - choose another in More settings";
            addTrayIcon();
            if (arguments.find("/tray") == std::string::npos)
                ShowWindow(window,
                    arguments.find("/minimized") != std::string::npos ? SW_SHOWMINIMIZED : showCommand);
            MSG message = {};
            int status;
            while ((status = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                if (message.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000) && !dialogOpen &&
                    !typing) {
                    if (message.wParam == 'S') {
                        saveSnippet();
                        continue;
                    }
                    if (message.wParam == 'N') {
                        newSnippet();
                        continue;
                    }
                }
                if (!IsDialogMessageW(window, &message)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            result = status == -1 ? 1 : static_cast<int>(message.wParam);
        }
        UnregisterClassW(WindowClass, instance);
    }
    for (HFONT old : {bodyFont, smallFont, titleFont, headingFont, editorFont})
        if (old)
            DeleteObject(old);
    for (HBRUSH brush : {backgroundBrush, panelBrush, fieldBrush})
        if (brush)
            DeleteObject(brush);
    for (HICON icon : {headerIcon, windowLargeIcon, windowSmallIcon, tray.hIcon})
        if (icon)
            DestroyIcon(icon);
    if (SUCCEEDED(apartment))
        CoUninitialize();
    FreeLibrary(richEdit);
    CloseHandle(mutex);
    return result;
}
