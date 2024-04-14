#include <Windows.h>
#include <string>
#include <iostream> 


void PressKey(BYTE keyCode) {
    keybd_event(keyCode, 0, 0, 0); // Press key
    keybd_event(keyCode, 0, KEYEVENTF_KEYUP, 0); // Release key
}

// Function to simulate keystrokes
void simulateKeystrokes(const std::wstring& input) {
    Sleep(1000);
    HWND foregroundWindow = GetForegroundWindow();
 
    for (char c : input) {
        if (foregroundWindow != GetForegroundWindow()) {
            break;
        }
        Sleep(10); // Adjust this delay as needed
        switch (c) {
        case 'ü':
            PressKey(VK_OEM_1); // 'ü'
            break;
        case 'ö':
            PressKey(VK_OEM_3); // 'ö'
            break;
        case 'ß':
            PressKey(VK_OEM_4); // 'ß'
            break;
        case '^':
            PressKey(VK_OEM_5); // '^'
            break;
        case '´':
            PressKey(VK_OEM_6); // '´'
            break;
        case 'ä':
            PressKey(VK_OEM_7); // 'ä'
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
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        // Create textbox
        HWND textbox = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL, 10, 10, 500, 300, hwnd, NULL, GetModuleHandle(NULL), NULL);
        // Create button
        CreateWindow(L"BUTTON", L"TYPE", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 10, 320, 100, 30, hwnd, (HMENU)1, GetModuleHandle(NULL), NULL);
        break;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case 1: { // ID of the button
            // Get the text from the textbox
            HWND textbox = GetDlgItem(hwnd, 0);
            int length = GetWindowTextLength(textbox);
            std::wstring buffer(length + 1, L'\0');
            GetWindowText(textbox, &buffer[0], length + 1);

            // Convert wide string to narrow string
            std::wstring input(buffer.begin(), buffer.end());

            // Simulate keystrokes
            simulateKeystrokes(input);
            break;
        }
        }
        break;
    }
    case WM_DESTROY: {
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Entry point of the application
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register the window class
    const wchar_t CLASS_NAME[] = L"MK-AutoTyper";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, L"MK AutoTyper", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 600, 400, NULL, NULL, hInstance, NULL);

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
