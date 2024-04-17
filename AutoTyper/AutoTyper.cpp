#include <Windows.h>
#include <string>
#include <iostream> 


void PressKey(BYTE keyCode, bool keyCtrl = false, bool keyAlt = false, bool keyShift = false) {
    if (keyShift) keybd_event(VK_LSHIFT, 0, 0, 0);
    if (keyCtrl) keybd_event(VK_LCONTROL, 0, 0, 0);
    if (keyAlt) keybd_event(VK_MENU, 0, 0, 0);
    keybd_event(keyCode, 0, 0, 0); // Press key
    keybd_event(keyCode, 0, KEYEVENTF_KEYUP, 0); // Release key
    if (keyCtrl) keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
    if (keyCtrl) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
    if (keyShift) keybd_event(VK_LSHIFT, 0, KEYEVENTF_KEYUP, 0);
}

// Function to simulate keystrokes
void simulateKeystrokes(const std::wstring& input,const int&keyStrokeDelay, const int& initialDelay) {
    Sleep(initialDelay);
    HWND foregroundWindow = GetForegroundWindow();
 
    for (char c : input) {
        if (foregroundWindow != GetForegroundWindow()) {
            break;
        }
        Sleep(keyStrokeDelay); // Can be adjusted
        switch (c) {
        case 'ü':
            PressKey(VK_OEM_1);
            break;
        case 'ö':
            PressKey(VK_OEM_3);
            break;
        case 'ß':
            PressKey(VK_OEM_4);
            break;
        case '^':
            PressKey(VK_OEM_5);
            break;
        case '´':
            PressKey(VK_OEM_6);
            break;
        case 'ä':
            PressKey(VK_OEM_7);
            break;
        case '@':
            PressKey(VkKeyScan('q'), true, true, false);
            break;
        case '§':
            PressKey(VkKeyScan('3'), false, false, true);
            break;
        default:
            if (!isprint(c) && c != '\n') {
                continue;
            }
            short vkCode = VkKeyScan(c);
            if ((vkCode & 0x0100) == 0x0100)
                keybd_event(VK_LSHIFT, 0, 0, 0);
            if ((vkCode & 0x0200) == 0x0200)
                keybd_event(VK_LCONTROL, 0, 0, 0);
            if ((vkCode & 0x0400) == 0x0400)
                keybd_event(VK_RMENU, 0, 0, 0);
                keybd_event((byte)(vkCode & 0x00FF), 0, 0, 0);
                keybd_event((byte)(vkCode & 0x00FF), 0, KEYEVENTF_KEYUP, 0);
            if ((vkCode & 0x0100) == 0x0100)
                keybd_event(VK_LSHIFT, 0, KEYEVENTF_KEYUP, 0);
            if ((vkCode & 0x0200) == 0x0200)
                keybd_event(VK_LCONTROL, 0, KEYEVENTF_KEYUP, 0);
            if ((vkCode & 0x0400) == 0x0400)
                keybd_event(VK_RMENU, 0, KEYEVENTF_KEYUP, 0);
            break;
        }

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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        textboxTypeContent = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 10, 10, 760, 500, hwnd, NULL, GetModuleHandle(NULL), NULL);
        CreateWindow(L"BUTTON", L"TYPE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 520, 100, 30, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
        HWND labelKeyStrokeDelay = CreateWindowEx(0, L"STATIC", L"Keystroke delay (ms):", WS_CHILD | WS_VISIBLE,130, 520, 120, 30,hwnd,NULL,GetModuleHandle(NULL),NULL);
        txtKeyStrokeDelay = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"10", WS_CHILD | ES_NUMBER | WS_VISIBLE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 255, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        HWND labelInitialDelay = CreateWindowEx(0, L"STATIC", L"Initial delay (ms):", WS_CHILD | WS_VISIBLE, 385, 520, 120, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);
        txtInitialDelay = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"1000", WS_CHILD | ES_NUMBER | WS_VISIBLE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 510, 520, 100, 30, hwnd, NULL, GetModuleHandle(NULL), NULL);

        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1: { // ID of the button
            simulateKeystrokes(getTxtBoxString(textboxTypeContent), getIKeyStrokeDelay(), getiInitialDelay());
            break;
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
    // Register the window class
    const wchar_t CLASS_NAME[] = L"MK-AutoTyper";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"MK AutoTyper", WS_MINIMIZEBOX | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, SW_HIDE);
    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    // Run the message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
