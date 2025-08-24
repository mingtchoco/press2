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
#define TIMER_ID 1002

// Global variables
HWND hMainWnd;
HWND hStatusLabel;
HHOOK hKeyboardHook;
bool isEnabled = false;
bool isEPressed = false;
bool isSendingKey = false;  // Flag to prevent blocking our own keys
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
std::uniform_int_distribution<int> normalDist(1, 50);  // Normal: 1-50ms
std::uniform_int_distribution<int> slowDist(50, 100);   // Slow: 50-100ms
std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);  // For probability
HBRUSH hBrushGreen = NULL;
HBRUSH hBrushRed = NULL;

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
void UpdateUI();
void SendEKey();
int GetRandomInterval();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Register window class
    const wchar_t CLASS_NAME[] = L"P2";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;  // We'll handle background painting
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    RegisterClassW(&wc);

    // Create window - ultra compact
    hMainWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"P2",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 90, 90,
        NULL, NULL, hInstance, NULL
    );

    if (hMainWnd == NULL) {
        return 0;
    }

    // Create status label only - ultra minimal
    hStatusLabel = CreateWindowW(
        L"STATIC",
        L"OFF",
        WS_VISIBLE | WS_CHILD | SS_CENTER,
        5, 15, 70, 35,
        hMainWnd, NULL, hInstance, NULL
    );
    
    // Set larger font for status
    HFONT hFontStatus = CreateFontW(
        24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontStatus, TRUE);

    // Don't register hotkeys - we'll handle them in the keyboard hook

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

        case WM_TIMER:
            if (wParam == TIMER_ID) {
                // Continue sending E while flag is set
                if (isEPressed && isEnabled) {
                    // Send E key press
                    SendEKey();
                    // Continue with next interval
                    SetTimer(hMainWnd, TIMER_ID, GetRandomInterval(), NULL);
                } else {
                    // Stop timer if conditions not met
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
        
        // Handle F2 and F3 keys for enable/disable
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (pKeyboard->vkCode == VK_F2) {
                isEnabled = true;
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
                UpdateUI();
                return 1; // Block F2 from reaching the game
            } else if (pKeyboard->vkCode == VK_F3) {
                isEnabled = false;
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
                UpdateUI();
                return 1; // Block F3 from reaching the game
            }
        }
        
        // Don't process our own keys
        if (isSendingKey) {
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }
        
        // Handle E key up - ALWAYS stop when E is released
        if (pKeyboard->vkCode == 'E' && (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
            // Force stop regardless of state
            isEPressed = false;
            KillTimer(hMainWnd, TIMER_ID);
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
                SetTimer(hMainWnd, TIMER_ID, GetRandomInterval(), NULL);
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
    // Force complete window redraw
    InvalidateRect(hMainWnd, NULL, TRUE);
    UpdateWindow(hMainWnd);
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

int GetRandomInterval() {
    // 25% chance for slow interval (50-100ms)
    // 75% chance for fast interval (1-50ms)
    if (chanceDist(rng) < 0.25f) {
        return slowDist(rng);  // Slow: 50-100ms
    } else {
        return normalDist(rng);  // Fast: 1-50ms
    }
}