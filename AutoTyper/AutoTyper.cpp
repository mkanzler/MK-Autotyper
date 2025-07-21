#include <Windows.h>
#include <string>
#include <iostream> 
#include "resource.h"
#include <atlconv.h>
#include <tchar.h>
#include <cassert>
#include <vector>
#include <thread>

#define UNIQUE_MK_AUTOTYPER L"MK-AutoTyper"

HINSTANCE CURRENT_HINSTANCE;
NOTIFYICONDATA SYSTRAY_DATA;
HWND TXTB_TYPE_CONTENT, TXTB_KEY_STROKE_DELAY, TXTB_INITIAL_DELAY, BTN_CHECKBOX_UNICODE, BTN_TYPE, CURRENT_WINDOW;

void send_unicode_character(TCHAR character) {
    INPUT keystrokes[2] = {};

    // Keydown event
    keystrokes[0].type = INPUT_KEYBOARD;
    keystrokes[0].ki.wVk = 0;
    keystrokes[0].ki.wScan = character;
    keystrokes[0].ki.dwFlags = KEYEVENTF_UNICODE;

    // Keyup event
    keystrokes[1].type = INPUT_KEYBOARD;
    keystrokes[1].ki.wVk = 0;
    keystrokes[1].ki.wScan = character;
    keystrokes[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    // Check if the character is a newline
    if (character == L'\n') {
        // Change the character to VK_RETURN
        keystrokes[0].ki.wVk = VK_RETURN;
        keystrokes[1].ki.wVk = VK_RETURN;
    }
    SendInput(2, keystrokes, sizeof(INPUT));
}

std::wstring get_txtbox_string(HWND txtBox) {
    int length = GetWindowTextLength(txtBox);
    std::wstring buffer(length + 1, L'\0');
    GetWindowText(txtBox, &buffer[0], length + 1);
    std::wstring input(buffer.begin(), buffer.end());
    return input;
}

int get_keystroke_delay() {
    int iKeyStrokeDelay = 10;
    try {
        iKeyStrokeDelay = std::stoi(get_txtbox_string(TXTB_KEY_STROKE_DELAY));
    }
    catch (const std::exception& e) {
        SetWindowText(TXTB_KEY_STROKE_DELAY, L"10");
    }
    if (iKeyStrokeDelay < 2) {
        iKeyStrokeDelay = 2;
    }
    return iKeyStrokeDelay;
}

void send_keybd_character(BYTE keyCode, bool keyCtrl = false, bool keyAlt = false, bool keyShift = false, bool keyAltGr = false) {
    std::vector<INPUT> inputs;

    auto press = [&](WORD vk, DWORD flags = 0) {
        INPUT in = { 0 };
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = flags;
        // mark right-Alt (AltGr) as extended
        if (vk == VK_RMENU && !(flags & KEYEVENTF_KEYUP)) {
            in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        inputs.push_back(in);
    };

    auto release = [&](WORD vk, DWORD flags = KEYEVENTF_KEYUP) {
        INPUT in = { 0 };
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = flags | (vk == VK_RMENU ? KEYEVENTF_EXTENDEDKEY : 0);
        inputs.push_back(in);
    };

    if (keyShift)    press(VK_SHIFT);
    if (keyCtrl)     press(VK_CONTROL);
    if (keyAltGr)    press(VK_RMENU);
    if (keyAlt)      press(VK_MENU);
    int keyStrokeDelay = get_keystroke_delay();
    Sleep(keyStrokeDelay / 2);
    press(keyCode);
    release(keyCode);
    Sleep(keyStrokeDelay / 2);

    if (keyAlt)      release(VK_MENU);
    if (keyAltGr)    release(VK_RMENU);
    if (keyCtrl)     release(VK_CONTROL);
    if (keyShift)    release(VK_SHIFT);

    // stagger inputs if needed
    UINT sent = SendInput((UINT)inputs.size(), inputs.data(), sizeof(INPUT));
    (void)sent; // ignore return
}


void simulate_keybd_keystrokes(const std::wstring& input, const int& keyStrokeDelay) {
    HWND foregroundWindow = GetForegroundWindow();
    for (char32_t c : input) {
        if (foregroundWindow != GetForegroundWindow()) {
            break;
        }
        switch (c) {
        case U'ä':
            send_keybd_character(VK_OEM_7);
            break;
        case U'Ä':
            send_keybd_character(VK_OEM_7,false,false,true);
            break;
        case U'ü':
            send_keybd_character(VK_OEM_1);
            break;
        case U'Ü':
            send_keybd_character(VK_OEM_7, false, false, true);
            break;
        case U'ö':
            send_keybd_character(VK_OEM_3);
            break;
        case U'Ö':
            send_keybd_character(VK_OEM_3, false, false, true);
            break;
        case U'ß':
            send_keybd_character(VK_OEM_4);
            break;
        case U'^':
            send_keybd_character(VK_OEM_5);
            send_keybd_character(VkKeyScan(' '));
            break;
        case U'´':
            send_keybd_character(VK_OEM_6);
            send_keybd_character(VkKeyScan(' '));
            break;
        case U'\u0060':  // Unicode for `
            send_keybd_character(VK_OEM_6, false, false, true, false);
            send_keybd_character(VkKeyScan(' '));
            break;
        case U'@':
            send_keybd_character(VkKeyScan('q'), true, true, false);
            break;
        case U'§':
            send_keybd_character(VkKeyScan('3'), false, false, true);
            break;
        case U'°':
            send_keybd_character(VkKeyScan('^'), false, false, true);
            break;
        case U'€':
            send_keybd_character(VkKeyScan('e'), true, true, false);
            break;
        case U'²':
            send_keybd_character(VkKeyScan('2'), false, false, false, true);
            break;
        case U'³':
            send_keybd_character(VkKeyScan('3'), false, false, false, true);
            break;
        case U'{':
            send_keybd_character(VkKeyScan('7'), false, false, false, true);
            break;
        case U'}':
            send_keybd_character(VkKeyScan('0'), false, false, false, true);
            break;
        default:
            if (c == U'\n') {
                continue;
            }
            // Fallback for special characters. Most applications support SendInput but not all do. This seems to be the best way to do this.
            if (!isprint(c) && c != U'\b' && c != U'\r') {
                send_unicode_character(c);
                continue;
            }
            short vkCode = VkKeyScan(c);
            if ((vkCode & 0x0100) == 0x0100)
                keybd_event(VK_LSHIFT, 0, 0, 0);
            if ((vkCode & 0x0200) == 0x0200)
                keybd_event(VK_LCONTROL, 0, 0, 0);
            if ((vkCode & 0x0400) == 0x0400)
                keybd_event(VK_RMENU, 0, 0, 0);
            Sleep(keyStrokeDelay/2);
            keybd_event((byte)(vkCode & 0x00FF), 0, 0, 0);
            keybd_event((byte)(vkCode & 0x00FF), 0, KEYEVENTF_KEYUP, 0);
            Sleep(keyStrokeDelay/2);
            if ((vkCode & 0x0100) == 0x0100)
                keybd_event(VK_LSHIFT, 0, KEYEVENTF_KEYUP, 0);
            if ((vkCode & 0x0200) == 0x0200)
                keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
            if ((vkCode & 0x0400) == 0x0400)
                keybd_event(VK_RMENU, 0, KEYEVENTF_KEYUP, 0);
            break;
        }
        //Sleep(keyStrokeDelay);
    }
}

int get_btn_checkbox_unicode_state() {
    return SendMessage(BTN_CHECKBOX_UNICODE, BM_GETCHECK, 0, 0);
}

void simulate_unicode_keystrokes(const std::wstring& input, const int& keyStrokeDelay) {
    HWND foregroundWindow = GetForegroundWindow();
    TCHAR* keyChars = W2T((wchar_t*)input.c_str());
    for (UINT i = 0; i < _tcslen(keyChars); ++i) {
        if (foregroundWindow != GetForegroundWindow()) {
            break;
        }
        send_unicode_character(keyChars[i]);
        Sleep(keyStrokeDelay);
    }
}

// Function to handle all keystrokes and chooses between unicode or keybd function
void simulate_keystrokes(const std::wstring& input,const int&keyStrokeDelay, const int& initialDelay) {
    Sleep(initialDelay);
    if (get_btn_checkbox_unicode_state() == 1) {
        simulate_unicode_keystrokes(input, keyStrokeDelay);
    }
    else {
        simulate_keybd_keystrokes(input, keyStrokeDelay);
    }
}


int get_initial_delay() {
    int iInitialDelay = 1000;
    try {
        iInitialDelay = std::stoi(get_txtbox_string(TXTB_INITIAL_DELAY));
    }
    catch (const std::exception& e) {
        SetWindowText(TXTB_INITIAL_DELAY, L"1000");
    }
    return iInitialDelay;
}

void create_system_tray_icon(HWND hWnd) {
    SYSTRAY_DATA.cbSize = sizeof(NOTIFYICONDATA);
    SYSTRAY_DATA.hWnd = hWnd;
    SYSTRAY_DATA.uID = 1;
    SYSTRAY_DATA.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    SYSTRAY_DATA.uCallbackMessage = WM_USER + 1;
    SYSTRAY_DATA.hIcon = LoadIcon(CURRENT_HINSTANCE, MAKEINTRESOURCE(IDI_SMALL));
    lstrcpy(SYSTRAY_DATA.szTip, TEXT("MK AutoTyper"));

    Shell_NotifyIcon(NIM_ADD, &SYSTRAY_DATA);
}

// Function to handle right-click context menu
void show_context_menu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, 1, TEXT("Exit"));

    SetForegroundWindow(hWnd); // Set the window as foreground
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

void remove_system_tray_icon() {
    Shell_NotifyIcon(NIM_DELETE, &SYSTRAY_DATA);
}

void switch_btn_checkbox_unicode_state() {
    int newState = (get_btn_checkbox_unicode_state() == BST_CHECKED) ? BST_UNCHECKED : BST_CHECKED;
    SendMessage(BTN_CHECKBOX_UNICODE, BM_SETCHECK, newState, 0);
}

LRESULT CALLBACK window_proc_handler(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        TXTB_TYPE_CONTENT = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 10, 10, 760, 500, hwnd, NULL, GetModuleHandle(NULL), NULL);
        BTN_TYPE = CreateWindow(L"BUTTON", L"TYPE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        HWND labelKeyStrokeDelay = CreateWindowEx(0, L"STATIC", L"Keystroke delay (ms):", WS_CHILD | WS_VISIBLE, 130, 520, 120, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        TXTB_KEY_STROKE_DELAY = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | ES_NUMBER | WS_VISIBLE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 255, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        HWND labelInitialDelay = CreateWindowEx(0, L"STATIC", L"Initial delay (ms):", WS_CHILD | WS_VISIBLE, 385, 520, 120, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        TXTB_INITIAL_DELAY = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1000", WS_CHILD | ES_NUMBER | WS_VISIBLE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 510, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        BTN_CHECKBOX_UNICODE = CreateWindowEx(0,L"BUTTON", L"Unicode mode", BS_CHECKBOX | WS_VISIBLE | WS_CHILD, 650, 520, 120, 30, hwnd, NULL, NULL, NULL);
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
            case 1: { // ID of the button
                if (HIWORD(wParam) == 0) {
                    remove_system_tray_icon();
                    PostQuitMessage(0);
                }
                break;
            } case BN_CLICKED: {
                if ((HWND)lParam == BTN_TYPE) {
                    ShowWindow(CURRENT_WINDOW, SW_HIDE);
                    simulate_keystrokes(get_txtbox_string(TXTB_TYPE_CONTENT), get_keystroke_delay(), get_initial_delay());
                    ShowWindow(CURRENT_WINDOW, SW_SHOW);
                }
                if ((HWND)lParam == BTN_CHECKBOX_UNICODE) {
                    switch_btn_checkbox_unicode_state();
                }
            }
        }
        break;
    }
    case WM_CLOSE: {
        ShowWindow(CURRENT_WINDOW, SW_HIDE);
    }
    case WM_USER+1: { // Systray icon message
        switch (lParam) {
            case WM_LBUTTONUP: { // Left click on systray icon
                ShowWindow(CURRENT_WINDOW, SW_SHOW);
                ShowWindow(CURRENT_WINDOW, SW_NORMAL);
                break;
            }
            case WM_RBUTTONDOWN: {
                show_context_menu(CURRENT_WINDOW);
            }
        }
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    case WM_CHAR: {
        SetWindowText(TXTB_INITIAL_DELAY, L"1234");
        SendMessage(TXTB_TYPE_CONTENT, EM_SETSEL, 0, -1); // Select all text
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

std::wstring get_cliboard_wtext() {
    OpenClipboard(nullptr);
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    std::wstring text(pszText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}

// Keyboard hook procedure
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        static bool ctrlDown = false;
        static bool altDown = false;

        if (p->vkCode == VK_CONTROL || p->vkCode == VK_LCONTROL || p->vkCode == VK_RCONTROL) {
            ctrlDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        }
        if (p->vkCode == VK_MENU || p->vkCode == VK_LMENU || p->vkCode == VK_RMENU) {
            altDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        }

        if ((wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) && p->vkCode == 'V') {
            if (ctrlDown && altDown) {
                std::thread([] {
                    Sleep(1);
                    simulate_keystrokes(get_cliboard_wtext(), get_keystroke_delay(), get_initial_delay());
                    }).detach();
                    return 1; // Block Ctrl+Alt+V
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// Function to set the keyboard hook
void set_keyboard_hook() {
    HHOOK hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    if (hook == NULL) {
        std::cerr << "Failed to set hook!" << std::endl;
    }
}

// Entry point of the application
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    set_keyboard_hook(); // Set the keyboard hook

    // Attempt to create a named mutex
    HANDLE hMutex = CreateMutex(NULL, TRUE, UNIQUE_MK_AUTOTYPER);
    // Check if the mutex already exists
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hOtherWnd = FindWindow(UNIQUE_MK_AUTOTYPER, NULL);
        ShowWindow(hOtherWnd, SW_SHOW);
        return 0;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = window_proc_handler;
    wc.hInstance = hInstance;
    wc.hbrBackground = CreateSolidBrush(RGB(255, 255, 255));
    CURRENT_HINSTANCE = hInstance;
    wc.lpszClassName = UNIQUE_MK_AUTOTYPER;

    RegisterClass(&wc);

    // Create the window
    CURRENT_WINDOW = CreateWindowEx(0, UNIQUE_MK_AUTOTYPER, L"MK AutoTyper", WS_MINIMIZEBOX | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);
    if (CURRENT_WINDOW == NULL) {
        return 0;
    }
    HICON hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_AUTOTYPER));
    HICON hIconSmall = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));
    SendMessage(CURRENT_WINDOW, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(CURRENT_WINDOW, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
    std::string cmdLine(lpCmdLine);

    if (!(cmdLine.find("/tray") != std::string::npos)) {
        ShowWindow(CURRENT_WINDOW, SW_SHOWDEFAULT);
    }
    if ((cmdLine.find("/minimized") != std::string::npos)) {
        ShowWindow(CURRENT_WINDOW, SW_SHOWMINIMIZED);
    }
    create_system_tray_icon(CURRENT_WINDOW);

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
