#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <psapi.h>

// Libraries are linked via build script (-lcomctl32 -luser32 -lpsapi)

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

// Key interval (no delay needed with SendInput batch)
constexpr int E_KEY_INTERVAL = 10;  // Fixed 10ms interval

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
HWINEVENTHOOK hWinEventHook = NULL;  // For League of Legends detection
HFONT hFontStatus = NULL;  // Track font resource
bool isEnabled = false;
bool isEPressed = false;
// isSendingKey removed: using LLKHF_INJECTED flag instead (race-condition free)
HBRUSH hBrushGreen = NULL;
HBRUSH hBrushRed = NULL;

// Monitor enumeration data
struct MonitorEnumData {
    int leftmostX = INT_MAX;
    int leftmostY = 0;
    bool found = false;
};

// Function declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
VOID CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
bool IsLeagueOfLegendsWindow(HWND hwnd);
bool IsLeagueOfLegendsProcess(DWORD processId);
void UpdateUI();
void SendEKey();

// Monitor enumeration callback to find leftmost monitor
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData) {
    MonitorEnumData* data = reinterpret_cast<MonitorEnumData*>(dwData);

    // Check if this monitor is more to the left
    if (lprcMonitor->left < data->leftmostX) {
        data->leftmostX = lprcMonitor->left;
        data->leftmostY = lprcMonitor->top;
        data->found = true;
    }

    return TRUE; // Continue enumeration
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"P2";

    // Check for existing instance and terminate it
    HWND hwndExisting = FindWindowW(CLASS_NAME, L"P2");
    if (hwndExisting) {
        // Try graceful shutdown first
        SendMessage(hwndExisting, WM_CLOSE, 0, 0);

        // Wait up to 2 seconds for graceful shutdown
        for (int i = 0; i < 20; i++) {
            if (!IsWindow(hwndExisting)) break;
            Sleep(100);
        }

        // Force terminate if still running
        if (IsWindow(hwndExisting)) {
            DWORD processId = 0;
            GetWindowThreadProcessId(hwndExisting, &processId);
            if (processId != 0) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                    Sleep(200); // Wait for process cleanup
                }
            }
        }
    }

    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;  // We'll handle background painting
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

    RegisterClassW(&wc);

    // Find the leftmost monitor's actual top-left corner
    MonitorEnumData monitorData;
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitorData));

    // Use found coordinates, or fallback to (0, 0) if enumeration failed
    int windowX = monitorData.found ? monitorData.leftmostX : 0;
    int windowY = monitorData.found ? monitorData.leftmostY : 0;

    // Create window - positioned at leftmost monitor's top-left corner
    hMainWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"P2",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        windowX, windowY, WINDOW_WIDTH, WINDOW_HEIGHT,
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

    // Install low-level keyboard hook
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    // Install window event hook for League of Legends detection
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
        isEnabled = true;
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

    // GDI resources (font, brushes) are cleaned up in WM_DESTROY

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
                // Send E only if conditions still met
                if (isEPressed && isEnabled) {
                    SendEKey();
                    // Timer auto-repeats, no need to call SetTimer again
                } else {
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
                return 1;
            } else if (pKeyboard->vkCode == VK_F3) {
                isEnabled = false;
                isEPressed = false;
                KillTimer(hMainWnd, TIMER_ID);
                UpdateUI();
                return 1;
            }
        }

        // Skip injected keys (from SendInput) to avoid re-processing our own output
        if (pKeyboard->flags & LLKHF_INJECTED) {
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }

        // Handle E key up - ALWAYS stop when E is released
        if (pKeyboard->vkCode == 'E' && (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)) {
            isEPressed = false;
            KillTimer(hMainWnd, TIMER_ID);
            if (isEnabled) {
                return 1;
            }
        }

        // Handle E key down only when enabled
        if (isEnabled && pKeyboard->vkCode == 'E' && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
            if (!isEPressed) {
                isEPressed = true;
                SendEKey();
                SetTimer(hMainWnd, TIMER_ID, E_KEY_INTERVAL, NULL);
            }
            return 1;
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
    InvalidateRect(hMainWnd, NULL, TRUE);
    UpdateWindow(hMainWnd);
}

void SendEKey() {
    // SendInput: batch down+up in one call (no Sleep needed, atomic operation)
    // Injected keys are auto-flagged with LLKHF_INJECTED and skipped in hook
    INPUT inputs[2] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 'E';
    inputs[0].ki.dwFlags = 0;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'E';
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
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

// WinEventProc callback - called when foreground window changes
VOID CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD) {
    // Only handle foreground window change events
    if (event != EVENT_SYSTEM_FOREGROUND) return;

    // Only process window objects
    if (idObject != OBJID_WINDOW) return;

    // Check if the new foreground window is League of Legends
    if (IsLeagueOfLegendsWindow(hwnd)) {
        // Auto-enable when LoL gains focus
        if (!isEnabled) {
            isEnabled = true;
            UpdateUI();
        }
    } else {
        // Auto-disable when LoL loses focus
        if (isEnabled) {
            isEnabled = false;
            isEPressed = false;
            KillTimer(hMainWnd, TIMER_ID);
            UpdateUI();
        }
    }
}