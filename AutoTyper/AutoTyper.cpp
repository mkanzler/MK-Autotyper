#include <Windows.h>
#include <string>
#include <iostream> 
#include "resource.h"
#include <atlconv.h>
#include <tchar.h>
#include <cassert>


HWND CURRENT_WINDOW;
HINSTANCE CURRENT_HINSTANCE;
NOTIFYICONDATA SYSTRAY_DATA;
#define UNIQUE_MK_AUTOTYPER L"MK-AutoTyper"
HWND BTN_TYPE;

void SendCharacter(TCHAR character) {
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
// Function to simulate keystrokes
void simulateKeystrokes(const std::wstring& input,const int&keyStrokeDelay, const int& initialDelay) {
    Sleep(initialDelay);
    HWND foregroundWindow = GetForegroundWindow();
    TCHAR* keyChars = W2T((wchar_t*)input.c_str());
    for (UINT i = 0; i < _tcslen(keyChars); ++i) {
        SendCharacter(keyChars[i]);
        Sleep(keyStrokeDelay); // Can be adjusted
    }
}

// Windows procedure function
HWND textboxTypeContent, txtKeyStrokeDelay, txtInitialDelay;
std::wstring getTxtBoxString(HWND txtBox) {
    int length = GetWindowTextLength(txtBox);
    std::wstring buffer(length + 1, L'\0');
    GetWindowText(txtBox, &buffer[0], length + 1);
    std::wstring input(buffer.begin(), buffer.end());
    return input;
}

int getiInitialDelay() {
    int iInitialDelay = 1000;
    try {
        iInitialDelay = std::stoi(getTxtBoxString(txtInitialDelay));
    }
    catch (const std::exception& e) {
        SetWindowText(txtInitialDelay, L"1000");
    }
    return iInitialDelay;
}

int getIKeyStrokeDelay() {
    int iKeyStrokeDelay = 10;
    try {
        iKeyStrokeDelay = std::stoi(getTxtBoxString(txtKeyStrokeDelay));
    }
    catch (const std::exception& e) {
        SetWindowText(txtKeyStrokeDelay, L"10");
    }
    return iKeyStrokeDelay;
}

// Function to create systray icon
void CreateSystemTrayIcon(HWND hWnd) {
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
void ShowContextMenu(HWND hWnd) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, 1, TEXT("Exit"));

    SetForegroundWindow(hWnd); // Set the window as foreground
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(hMenu);
}

// Function to remove systray icon
void RemoveSystemTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &SYSTRAY_DATA);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        textboxTypeContent = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 10, 10, 760, 500, hwnd, NULL, GetModuleHandle(NULL), NULL);
        BTN_TYPE = CreateWindow(L"BUTTON", L"TYPE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        HWND labelKeyStrokeDelay = CreateWindowEx(0, L"STATIC", L"Keystroke delay (ms):", WS_CHILD | WS_VISIBLE, 130, 520, 120, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        txtKeyStrokeDelay = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | ES_NUMBER | WS_VISIBLE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 255, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        HWND labelInitialDelay = CreateWindowEx(0, L"STATIC", L"Initial delay (ms):", WS_CHILD | WS_VISIBLE, 385, 520, 120, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        txtInitialDelay = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1000", WS_CHILD | ES_NUMBER | WS_VISIBLE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 510, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);

        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
            case 1: { // ID of the button
                if (HIWORD(wParam) == 0) {
                    RemoveSystemTrayIcon();
                    PostQuitMessage(0);
                }
                break;
            } case BN_CLICKED: {
                if ((HWND)lParam == BTN_TYPE) {
                    ShowWindow(CURRENT_WINDOW, SW_HIDE);
                    simulateKeystrokes(getTxtBoxString(textboxTypeContent), getIKeyStrokeDelay(), getiInitialDelay());
                    ShowWindow(CURRENT_WINDOW, SW_SHOW);
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
                ShowContextMenu(CURRENT_WINDOW);
            }
        }
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    case WM_CHAR: {
        SetWindowText(txtInitialDelay, L"1234");
        SendMessage(textboxTypeContent, EM_SETSEL, 0, -1); // Select all text
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

std::wstring GetClipboardTextW() {
    OpenClipboard(nullptr);
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    std::wstring text(pszText);
    GlobalUnlock(hData);
    CloseClipboard();
    return text;
}
// Keyboard hook procedure
int pressed = 0;
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        if (wParam == WM_KEYUP) {
            // Check if Ctrl, Alt, and V are pressed simultaneously
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000 && GetAsyncKeyState(VK_MENU) & 0x8000 && GetAsyncKeyState(VkKeyScan('v')) & 0x8000) {
                if (pressed == 0) {
                    //pressed = 1;
                    simulateKeystrokes(GetClipboardTextW(), getIKeyStrokeDelay(), getiInitialDelay());
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// Function to set the keyboard hook
void setHook() {
    HHOOK hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    if (hook == NULL) {
        std::cerr << "Failed to set hook!" << std::endl;
    }
}

// Entry point of the application
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    setHook(); // Set the keyboard hook

    // Attempt to create a named mutex
    HANDLE hMutex = CreateMutex(NULL, TRUE, UNIQUE_MK_AUTOTYPER);
    // Check if the mutex already exists
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND hOtherWnd = FindWindow(UNIQUE_MK_AUTOTYPER, NULL);

        ShowWindow(hOtherWnd, SW_SHOW);
        return 0;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
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
        ShowWindow(CURRENT_WINDOW, SW_SHOW);
    }
    if ((cmdLine.find("/minimized") != std::string::npos)) {
        ShowWindow(CURRENT_WINDOW, SW_SHOWMINIMIZED);
    }
    CreateSystemTrayIcon(CURRENT_WINDOW);

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
