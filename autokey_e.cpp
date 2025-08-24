#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <random>
#include <chrono>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")

// Window controls
#define ID_TRACKBAR 1001
#define TIMER_ID 1002

// Global variables
HWND hMainWnd;
HWND hTrackBar;
HWND hStatusLabel;
HWND hIntervalLabel;
HHOOK hKeyboardHook;
bool isEnabled = false;
bool isEPressed = false;
int currentInterval = 10;
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
void UpdateUI();
void SendEKey();
int GetRandomInterval(int baseInterval);
void CALLBACK TimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Initialize common controls
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    // Register window class
    const wchar_t CLASS_NAME[] = L"AutoKeyE";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    RegisterClassW(&wc);

    // Create window
    hMainWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        CLASS_NAME,
        L"AutoKey E",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 270, 150,
        NULL, NULL, hInstance, NULL
    );

    if (hMainWnd == NULL) {
        return 0;
    }

    // Create status label
    hStatusLabel = CreateWindowW(
        L"STATIC",
        L"Status: OFF",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        10, 10, 240, 30,
        hMainWnd, NULL, hInstance, NULL
    );

    // Create interval label
    hIntervalLabel = CreateWindowW(
        L"STATIC",
        L"Interval: 10ms (±25%)",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        10, 45, 240, 20,
        hMainWnd, NULL, hInstance, NULL
    );

    // Create trackbar
    hTrackBar = CreateWindowExW(
        0,
        TRACKBAR_CLASSW,
        L"Interval",
        WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_TOOLTIPS,
        10, 70, 240, 30,
        hMainWnd, (HMENU)ID_TRACKBAR, hInstance, NULL
    );

    // Set trackbar range (5-100ms)
    SendMessage(hTrackBar, TBM_SETRANGE, TRUE, MAKELPARAM(5, 100));
    SendMessage(hTrackBar, TBM_SETPOS, TRUE, 10);
    SendMessage(hTrackBar, TBM_SETTICFREQ, 10, 0);

    // Register hotkeys
    RegisterHotKey(hMainWnd, 1, 0, VK_F2);
    RegisterHotKey(hMainWnd, 2, 0, VK_F3);

    // Install low-level keyboard hook
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    UpdateUI();
    ShowWindow(hMainWnd, nCmdShow);

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup
    UnhookWindowsHookEx(hKeyboardHook);
    UnregisterHotKey(hMainWnd, 1);
    UnregisterHotKey(hMainWnd, 2);

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_HOTKEY:
            if (wParam == 1) { // F2 - Enable
                isEnabled = true;
                UpdateUI();
            } else if (wParam == 2) { // F3 - Disable
                isEnabled = false;
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
                UpdateUI();
            }
            break;

        case WM_HSCROLL:
            if ((HWND)lParam == hTrackBar) {
                currentInterval = SendMessage(hTrackBar, TBM_GETPOS, 0, 0);
                wchar_t buffer[50];
                wsprintfW(buffer, L"Interval: %dms (±25%%)", currentInterval);
                SetWindowTextW(hIntervalLabel, buffer);
            }
            break;

        case WM_TIMER:
            if (wParam == TIMER_ID && isEPressed && isEnabled) {
                SendEKey();
                // Reset timer with new random interval
                KillTimer(hMainWnd, TIMER_ID);
                SetTimer(hMainWnd, TIMER_ID, GetRandomInterval(currentInterval), NULL);
            }
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && isEnabled) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        
        if (pKeyboard->vkCode == 'E') {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                if (!isEPressed) {
                    isEPressed = true;
                    // Send first E immediately
                    SendEKey();
                    // Start timer for rapid fire
                    SetTimer(hMainWnd, TIMER_ID, GetRandomInterval(currentInterval), NULL);
                }
                return 1; // Block original E key
            } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
                return 1; // Block original E key release
            }
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

void UpdateUI() {
    if (isEnabled) {
        SetWindowTextW(hStatusLabel, L"Status: ON");
        // Set green background
        HDC hdc = GetDC(hMainWnd);
        RECT rect;
        GetClientRect(hMainWnd, &rect);
        HBRUSH hBrush = CreateSolidBrush(RGB(144, 238, 144));
        FillRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);
        ReleaseDC(hMainWnd, hdc);
        // Force redraw all controls
        InvalidateRect(hMainWnd, NULL, FALSE);
    } else {
        SetWindowTextW(hStatusLabel, L"Status: OFF");
        // Set red background
        HDC hdc = GetDC(hMainWnd);
        RECT rect;
        GetClientRect(hMainWnd, &rect);
        HBRUSH hBrush = CreateSolidBrush(RGB(255, 182, 193));
        FillRect(hdc, &rect, hBrush);
        DeleteObject(hBrush);
        ReleaseDC(hMainWnd, hdc);
        // Force redraw all controls
        InvalidateRect(hMainWnd, NULL, FALSE);
    }
}

void SendEKey() {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 'E';
    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(INPUT));
    
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

int GetRandomInterval(int baseInterval) {
    // Generate random interval between 75% and 125% of base interval
    std::uniform_int_distribution<int> dist(
        (int)(baseInterval * 0.75),
        (int)(baseInterval * 1.25)
    );
    return dist(rng);
}