#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <psapi.h>
#include <string>
#include <random>
#include <chrono>

// Libraries are linked via build script (-lcomctl32 -luser32)

// Constants
#define TIMER_ID 1002

// Window dimensions
constexpr int WINDOW_WIDTH = 90;
constexpr int WINDOW_HEIGHT = 90;

// UI layout
constexpr int LABEL_X = 5;
constexpr int LABEL_Y = 15;
constexpr int LABEL_WIDTH = 70;
constexpr int LABEL_HEIGHT = 35;

// Font settings
constexpr int FONT_SIZE = 24;

// Timing intervals (milliseconds)
constexpr int FAST_MIN = 1;
constexpr int FAST_MAX = 50;
constexpr int SLOW_MIN = 50;
constexpr int SLOW_MAX = 100;
constexpr float SLOW_PROBABILITY = 0.25f;

// Key delay
constexpr int KEY_DELAY_MS = 1;

// League of Legends process detection
constexpr wchar_t LOL_PROCESS_1[] = L"League of Legends.exe";
constexpr wchar_t LOL_PROCESS_2[] = L"LeagueClient.exe";
constexpr wchar_t LOL_WINDOW_TITLE[] = L"League of Legends (TM) Client";

// Colors
constexpr COLORREF ENABLED_BG_COLOR = RGB(240, 255, 240);   // Soft mint
constexpr COLORREF DISABLED_BG_COLOR = RGB(248, 248, 248);  // Light gray
constexpr COLORREF ENABLED_TEXT_COLOR = RGB(34, 139, 34);   // Forest green
constexpr COLORREF DISABLED_TEXT_COLOR = RGB(128, 128, 128); // Gray

// Global variables
HWND hMainWnd = NULL;
HWND hStatusLabel = NULL;
HHOOK hKeyboardHook = NULL;
HWINEVENTHOOK hWinEventHook = NULL;  // For window focus detection
HFONT hFontStatus = NULL;  // Track font resource
bool isEnabled = false;
bool isEPressed = false;
bool isSendingKey = false;  // Flag to prevent blocking our own keys
bool isLoLFocused = false;  // Track if League of Legends is currently focused
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
std::uniform_int_distribution<int> normalDist(FAST_MIN, FAST_MAX);  // Fast intervals
std::uniform_int_distribution<int> slowDist(SLOW_MIN, SLOW_MAX);   // Slow intervals
std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);  // For probability
HBRUSH hBrushGreen = NULL;
HBRUSH hBrushRed = NULL;

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
VOID CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime);
bool IsLeagueOfLegendsWindow(HWND hwnd);
bool IsLeagueOfLegendsProcess(DWORD processId);
void EnableAutoMode();
void DisableAutoMode();
void UpdateUI();
void SendEKey();
int GetRandomInterval();

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
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
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT,
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
        LABEL_X, LABEL_Y, LABEL_WIDTH, LABEL_HEIGHT,
        hMainWnd, NULL, hInstance, NULL
    );
    
    // Set larger font for status
    hFontStatus = CreateFontW(
        FONT_SIZE, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    if (hFontStatus) {
        SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontStatus, TRUE);
    }

    // Don't register hotkeys - we'll handle them in the keyboard hook

    // Install low-level keyboard hook
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    // Install window event hook for auto-detection
    hWinEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,    // eventMin
        EVENT_SYSTEM_FOREGROUND,    // eventMax
        NULL,                       // hmodWinEventProc
        WinEventProc,              // lpfnWinEventProc
        0,                          // idProcess (0 = all processes)
        0,                          // idThread (0 = all threads)
        WINEVENT_OUTOFCONTEXT      // dwFlags
    );

    // Check initial foreground window
    HWND foregroundWindow = GetForegroundWindow();
    if (IsLeagueOfLegendsWindow(foregroundWindow)) {
        EnableAutoMode();
    }

    UpdateUI();
    ShowWindow(hMainWnd, nCmdShow);

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup resources in proper order
    if (hKeyboardHook) {
        UnhookWindowsHookEx(hKeyboardHook);
        hKeyboardHook = NULL;
    }

    if (hWinEventHook) {
        UnhookWinEvent(hWinEventHook);
        hWinEventHook = NULL;
    }

    // Clean up font
    if (hFontStatus) {
        DeleteObject(hFontStatus);
        hFontStatus = NULL;
    }

    // Clean up brushes (if not already done in WM_DESTROY)
    if (hBrushGreen) {
        DeleteObject(hBrushGreen);
        hBrushGreen = NULL;
    }
    if (hBrushRed) {
        DeleteObject(hBrushRed);
        hBrushRed = NULL;
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            // Create brushes - soft colors for less eye strain
            hBrushGreen = CreateSolidBrush(ENABLED_BG_COLOR);
            hBrushRed = CreateSolidBrush(DISABLED_BG_COLOR);
            break;
            
        case WM_CTLCOLORSTATIC:
            {
                HDC hdcStatic = (HDC)wParam;
                if (isEnabled) {
                    SetTextColor(hdcStatic, ENABLED_TEXT_COLOR);
                    SetBkMode(hdcStatic, TRANSPARENT);
                    return (INT_PTR)hBrushGreen;
                } else {
                    SetTextColor(hdcStatic, DISABLED_TEXT_COLOR);
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
            if (hBrushGreen) {
                DeleteObject(hBrushGreen);
                hBrushGreen = NULL;
            }
            if (hBrushRed) {
                DeleteObject(hBrushRed);
                hBrushRed = NULL;
            }

            // Clean up font
            if (hFontStatus) {
                DeleteObject(hFontStatus);
                hFontStatus = NULL;
            }

            // Kill any running timer
            KillTimer(hwnd, TIMER_ID);

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
    Sleep(KEY_DELAY_MS); // Very short delay
    keybd_event('E', MapVirtualKey('E', MAPVK_VK_TO_VSC), KEYEVENTF_KEYUP, 0);
    
    // Reset flag
    isSendingKey = false;
}

int GetRandomInterval() {
    // Configurable probability for slow vs fast intervals
    if (chanceDist(rng) < SLOW_PROBABILITY) {
        return slowDist(rng);  // Slow intervals
    } else {
        return normalDist(rng);  // Fast intervals
    }
}

// Check if a process ID belongs to League of Legends
bool IsLeagueOfLegendsProcess(DWORD processId) {
    if (processId == 0) return false;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!hProcess) return false;

    wchar_t processName[MAX_PATH] = {0};
    bool isLoL = false;

    // Get the process executable name
    if (GetModuleFileNameExW(hProcess, NULL, processName, MAX_PATH)) {
        // Extract just the filename from the full path
        wchar_t* fileName = wcsrchr(processName, L'\\');
        if (fileName) {
            fileName++; // Skip the backslash

            // Check if it's one of the League of Legends processes
            if (wcscmp(fileName, LOL_PROCESS_1) == 0 ||
                wcscmp(fileName, LOL_PROCESS_2) == 0) {
                isLoL = true;
            }
        }
    }

    CloseHandle(hProcess);
    return isLoL;
}

// Check if a window belongs to League of Legends
bool IsLeagueOfLegendsWindow(HWND hwnd) {
    if (!hwnd) return false;

    // First check by process
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    if (IsLeagueOfLegendsProcess(processId)) {
        return true;
    }

    // Also check by window title for the client
    wchar_t windowTitle[256] = {0};
    if (GetWindowTextW(hwnd, windowTitle, 256)) {
        if (wcsstr(windowTitle, LOL_WINDOW_TITLE) != nullptr) {
            return true;
        }
    }

    return false;
}

// Enable auto mode (called when LoL gains focus)
void EnableAutoMode() {
    if (!isLoLFocused) {
        isLoLFocused = true;
        if (!isEnabled) {  // Only auto-enable if not manually disabled
            isEnabled = true;
            isEPressed = false; // Reset E key state
            KillTimer(hMainWnd, TIMER_ID);
            UpdateUI();
        }
    }
}

// Disable auto mode (called when LoL loses focus)
void DisableAutoMode() {
    if (isLoLFocused) {
        isLoLFocused = false;
        if (isEnabled) {  // Auto-disable if currently enabled
            isEnabled = false;
            isEPressed = false;
            KillTimer(hMainWnd, TIMER_ID);
            UpdateUI();
        }
    }
}

// WinEventProc callback - called when foreground window changes
VOID CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    // Only handle foreground window change events
    if (event != EVENT_SYSTEM_FOREGROUND) return;

    // Only process window objects
    if (idObject != OBJID_WINDOW) return;

    // Check if the new foreground window is League of Legends
    if (IsLeagueOfLegendsWindow(hwnd)) {
        EnableAutoMode();
    } else {
        DisableAutoMode();
    }
}