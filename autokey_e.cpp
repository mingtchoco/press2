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
bool isSendingKey = false;  // Flag to prevent blocking our own keys
int currentInterval = 5;  // Default 5ms for faster response
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
HBRUSH hBrushGreen = NULL;
HBRUSH hBrushRed = NULL;

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
    wc.hbrBackground = NULL;  // We'll handle background painting
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    RegisterClassW(&wc);

    // Create window - more compact
    hMainWnd = CreateWindowExW(
        WS_EX_TOPMOST,
        CLASS_NAME,
        L"AutoKey E",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 220, 120,
        NULL, NULL, hInstance, NULL
    );

    if (hMainWnd == NULL) {
        return 0;
    }

    // Create status label - compact
    hStatusLabel = CreateWindowW(
        L"STATIC",
        L"OFF",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        10, 10, 190, 25,
        hMainWnd, NULL, hInstance, NULL
    );
    
    // Set compact font for status
    HFONT hFontStatus = CreateFontW(
        18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontStatus, TRUE);

    // Create interval label
    hIntervalLabel = CreateWindowW(
        L"STATIC",
        L"5ms",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        10, 40, 190, 15,
        hMainWnd, NULL, hInstance, NULL
    );
    
    // Set font for interval label
    HFONT hFontInterval = CreateFontW(
        12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    SendMessage(hIntervalLabel, WM_SETFONT, (WPARAM)hFontInterval, TRUE);

    // Create trackbar - compact
    hTrackBar = CreateWindowExW(
        0,
        TRACKBAR_CLASSW,
        L"Interval",
        WS_VISIBLE | WS_CHILD | TBS_HORZ | TBS_NOTICKS,
        10, 60, 190, 20,
        hMainWnd, (HMENU)ID_TRACKBAR, hInstance, NULL
    );

    // Set trackbar range (1-50ms) - faster range
    SendMessage(hTrackBar, TBM_SETRANGE, TRUE, MAKELPARAM(1, 50));
    SendMessage(hTrackBar, TBM_SETPOS, TRUE, 5);  // Default 5ms
    SendMessage(hTrackBar, TBM_SETTICFREQ, 5, 0);

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
        case WM_CREATE:
            // Create brushes - soft colors for less eye strain
            hBrushGreen = CreateSolidBrush(RGB(240, 255, 240));  // Soft mint
            hBrushRed = CreateSolidBrush(RGB(248, 248, 248));    // Light gray
            break;
            
        case WM_CTLCOLORSTATIC:
            {
                HDC hdcStatic = (HDC)wParam;
                if (isEnabled) {
                    SetTextColor(hdcStatic, RGB(34, 139, 34));  // Forest green
                    SetBkMode(hdcStatic, TRANSPARENT);
                    return (INT_PTR)hBrushGreen;
                } else {
                    SetTextColor(hdcStatic, RGB(128, 128, 128));  // Gray
                    SetBkMode(hdcStatic, TRANSPARENT);
                    return (INT_PTR)hBrushRed;
                }
            }
            break;
            
        case WM_ERASEBKGND:
            {
                HDC hdc = (HDC)wParam;
                RECT rect;
                GetClientRect(hwnd, &rect);
                FillRect(hdc, &rect, isEnabled ? hBrushGreen : hBrushRed);
                return 1;  // Tell Windows we handled it
            }
            break;
            
        case WM_HOTKEY:
            if (wParam == 1) { // F2 - Enable
                isEnabled = true;
                // Reset state when enabling
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
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
                wsprintfW(buffer, L"%dms", currentInterval);
                SetWindowTextW(hIntervalLabel, buffer);
                // Force trackbar redraw
                InvalidateRect(hTrackBar, NULL, TRUE);
            }
            break;

        case WM_TIMER:
            if (wParam == TIMER_ID) {
                // Check if E is still actually pressed
                if (isEPressed && isEnabled && (GetAsyncKeyState('E') & 0x8000)) {
                    // Send E key press
                    SendEKey();
                    // Continue with next interval
                    KillTimer(hMainWnd, TIMER_ID);
                    SetTimer(hMainWnd, TIMER_ID, GetRandomInterval(currentInterval), NULL);
                } else {
                    // E is not pressed anymore, stop everything
                    isEPressed = false;
                    KillTimer(hMainWnd, TIMER_ID);
                }
            }
            break;

        case WM_DESTROY:
            // Clean up brushes
            if (hBrushGreen) DeleteObject(hBrushGreen);
            if (hBrushRed) DeleteObject(hBrushRed);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;
        
        // Don't process our own keys
        if (isSendingKey) {
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }
        
        // Always handle E key up to prevent stuck keys
        if (pKeyboard->vkCode == 'E' && (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
            if (isEPressed) {
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
            }
            if (isEnabled) {
                return 1; // Block original E key up when enabled
            }
        }
        
        // Handle E key down only when enabled
        if (isEnabled && pKeyboard->vkCode == 'E' && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            if (!isEPressed) {
                isEPressed = true;
                // Send first E immediately
                SendEKey();
                // Start timer for rapid fire
                SetTimer(hMainWnd, TIMER_ID, GetRandomInterval(currentInterval), NULL);
            }
            return 1; // Block original E key down
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

void UpdateUI() {
    if (isEnabled) {
        SetWindowTextW(hStatusLabel, L"ON");
    } else {
        SetWindowTextW(hStatusLabel, L"OFF");
    }
    // Force complete window redraw including trackbar
    InvalidateRect(hMainWnd, NULL, TRUE);
    InvalidateRect(hTrackBar, NULL, TRUE);
    UpdateWindow(hMainWnd);
    UpdateWindow(hTrackBar);
}

void SendEKey() {
    // Set flag to prevent blocking our own keys
    isSendingKey = true;
    
    // Method 1: Try keybd_event first (older but sometimes more compatible)
    keybd_event('E', MapVirtualKey('E', MAPVK_VK_TO_VSC), 0, 0);
    Sleep(1); // Very short delay
    keybd_event('E', MapVirtualKey('E', MAPVK_VK_TO_VSC), KEYEVENTF_KEYUP, 0);
    
    // Reset flag
    isSendingKey = false;
}

int GetRandomInterval(int baseInterval) {
    // Generate random interval between 75% and 125% of base interval
    std::uniform_int_distribution<int> dist(
        (int)(baseInterval * 0.75),
        (int)(baseInterval * 1.25)
    );
    return dist(rng);
}