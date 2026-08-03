#define UNICODE
#define _UNICODE
#include <windows.h>
#include <commctrl.h>
#include <psapi.h>

// Libraries are linked via build script (-lcomctl32 -luser32 -lpsapi)

// UI layout. Everything below is derived from the content, and the window size
// is computed from CLIENT_WIDTH/HEIGHT via AdjustWindowRectEx in WinMain - so
// the client area is exactly the content with no leftover margin. Do not go
// back to hard-coded window dimensions; the frame metrics differ per system.
constexpr int MARGIN = 3;

// Per-key toggle row (owner drawn, click a cell to toggle it)
constexpr int KEY_COUNT = 4;
constexpr int KEY_CELL_W = 19;
constexpr int KEY_CELL_H = 22;
constexpr int KEY_ROW_W = KEY_COUNT * KEY_CELL_W;

// Status label sits directly above the row and shares its width
constexpr int LABEL_X = MARGIN;
constexpr int LABEL_Y = 2;
constexpr int LABEL_WIDTH = KEY_ROW_W;
constexpr int LABEL_HEIGHT = 28;

constexpr int KEY_ROW_X = MARGIN;
constexpr int KEY_ROW_Y = LABEL_Y + LABEL_HEIGHT + 3;

constexpr int CLIENT_WIDTH = KEY_ROW_W + MARGIN * 2;
constexpr int CLIENT_HEIGHT = KEY_ROW_Y + KEY_CELL_H + MARGIN;

// Window style, shared by CreateWindowExW and the AdjustWindowRectEx call
constexpr DWORD WINDOW_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
constexpr DWORD WINDOW_EX_STYLE = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

// Font settings
constexpr int FONT_SIZE = 22;
constexpr int KEY_FONT_SIZE = 15;

// Number of Sleep(1) calls between key sends while held. NOT milliseconds.
// timeBeginPeriod is never used, so one Sleep(1) costs a full timer tick
// (~15.5ms measured). Repeat period ~= E_KEY_INTERVAL * 15.5ms.
// Measured: 10 -> 6.47/s, 3 -> 21.6/s, 2 -> 32.4/s, 1 -> 64.4/s (structural cap).
// The worker wakes ~64.7 times/sec regardless of this value; it only decides
// how many wakes pass before a key is sent. Never set this to 0 (endless spam).
constexpr int E_KEY_INTERVAL = 2;

// League of Legends process detection
constexpr wchar_t LOL_PROCESS_1[] = L"League of Legends.exe";
constexpr wchar_t LOL_PROCESS_2[] = L"LeagueClient.exe";
constexpr wchar_t LOL_WINDOW_TITLE[] = L"League of Legends (TM) Client";

// Colors
constexpr COLORREF ENABLED_BG_COLOR = RGB(240, 255, 240);   // Soft mint
constexpr COLORREF DISABLED_BG_COLOR = RGB(248, 248, 248);  // Light gray
constexpr COLORREF ENABLED_TEXT_COLOR = RGB(34, 139, 34);   // Forest green
constexpr COLORREF DISABLED_TEXT_COLOR = RGB(128, 128, 128); // Gray
constexpr COLORREF KEY_ON_BG_COLOR = RGB(34, 139, 34);      // Selected key
constexpr COLORREF KEY_WARN_BG_COLOR = RGB(198, 88, 28);    // Selected + needs confirm (R)
constexpr COLORREF KEY_OFF_BG_COLOR = RGB(214, 214, 214);   // Unselected key
constexpr COLORREF KEY_ON_TEXT_COLOR = RGB(255, 255, 255);
constexpr COLORREF KEY_OFF_TEXT_COLOR = RGB(120, 120, 120);

// Per-key state.
// EVERY field here is written ONLY from the main thread - the hook callback,
// WinEventProc and WindowProc all run on the thread that installed the hook,
// and so does WinMain. The worker thread only ever reads. That single-writer
// rule is what makes plain volatile bool safe; never write these from the worker.
struct KeyEntry {
    DWORD vk;                     // virtual key code
    const wchar_t* label;         // text drawn in the toggle row, also the .ini key name
    bool isUltimate;              // drawn in the accent colour (fires on cooldown)
    volatile bool selected;       // user turned this key on
    volatile bool pressed;        // physically held right now
    volatile bool weBlockedDown;  // we swallowed the keydown, so we must swallow the keyup
};

// E defaults on to preserve the original behaviour. R defaults off on purpose.
KeyEntry g_keys[KEY_COUNT] = {
    { 'Q', L"Q", false, false, false, false },
    { 'W', L"W", false, false, false, false },
    { 'E', L"E", false, true,  false, false },
    { 'R', L"R", true,  false, false, false },
};

// Global variables
HWND hMainWnd = NULL;
HWND hStatusLabel = NULL;
HHOOK hKeyboardHook = NULL;
HWINEVENTHOOK hWinEventHook = NULL;  // For League of Legends detection
HFONT hFontStatus = NULL;  // Track font resource
HFONT hFontKey = NULL;     // Font for the toggle row
volatile bool isEnabled = false;
HBRUSH hBrushGreen = NULL;
HBRUSH hBrushRed = NULL;
wchar_t g_iniPath[MAX_PATH] = {0};  // settings file, next to the exe

// Worker thread for key sending (decoupled from hook callback to prevent freezes)
HANDLE g_workerThread = NULL;
volatile bool g_exitThread = false;

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
DWORD WINAPI WorkerThreadProc(LPVOID lpParam);
bool IsLeagueOfLegendsWindow(HWND hwnd);
bool IsLeagueOfLegendsProcess(DWORD processId);
void UpdateUI();
void SendKey(DWORD vk);
void ClearPressedKeys();
bool AnyKeyActive();
void InitIniPath();
void LoadSettings();
void SaveSettings();
int  HitTestKeyCell(int x, int y);
void DrawKeyRow(HDC hdc);

// Clear the physical-hold flags. Deliberately leaves weBlockedDown alone:
// if we already swallowed a keydown we still owe the target app its keyup.
void ClearPressedKeys() {
    for (int i = 0; i < KEY_COUNT; i++) {
        g_keys[i].pressed = false;
    }
}

bool AnyKeyActive() {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_keys[i].selected && g_keys[i].pressed) return true;
    }
    return false;
}

// Settings live in autokey_e.ini next to the exe.
void InitIniPath() {
    if (GetModuleFileNameW(NULL, g_iniPath, MAX_PATH) == 0) {
        g_iniPath[0] = 0;
        return;
    }
    size_t len = wcslen(g_iniPath);
    if (len > 4 && lstrcmpiW(g_iniPath + len - 4, L".exe") == 0) {
        wcscpy(g_iniPath + len - 4, L".ini");
    } else if (len + 4 < MAX_PATH) {
        wcscat(g_iniPath, L".ini");
    } else {
        g_iniPath[0] = 0;
    }
}

void LoadSettings() {
    if (!g_iniPath[0]) return;
    for (int i = 0; i < KEY_COUNT; i++) {
        UINT v = GetPrivateProfileIntW(L"Keys", g_keys[i].label,
                                       g_keys[i].selected ? 1 : 0, g_iniPath);
        g_keys[i].selected = (v != 0);
    }
}

void SaveSettings() {
    if (!g_iniPath[0]) return;
    for (int i = 0; i < KEY_COUNT; i++) {
        WritePrivateProfileStringW(L"Keys", g_keys[i].label,
                                   g_keys[i].selected ? L"1" : L"0", g_iniPath);
    }
}

// Returns the index of the toggle cell at (x, y) in client coords, or -1.
int HitTestKeyCell(int x, int y) {
    if (y < KEY_ROW_Y || y >= KEY_ROW_Y + KEY_CELL_H) return -1;
    if (x < KEY_ROW_X) return -1;
    int idx = (x - KEY_ROW_X) / KEY_CELL_W;
    if (idx < 0 || idx >= KEY_COUNT) return -1;
    return idx;
}

void DrawKeyRow(HDC hdc) {
    HFONT prevFont = NULL;
    if (hFontKey) prevFont = (HFONT)SelectObject(hdc, hFontKey);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < KEY_COUNT; i++) {
        RECT cell;
        cell.left = KEY_ROW_X + i * KEY_CELL_W;
        cell.top = KEY_ROW_Y;
        cell.right = cell.left + KEY_CELL_W - 1;
        cell.bottom = cell.top + KEY_CELL_H;

        COLORREF bg = KEY_OFF_BG_COLOR;
        COLORREF fg = KEY_OFF_TEXT_COLOR;
        if (g_keys[i].selected) {
            bg = g_keys[i].isUltimate ? KEY_WARN_BG_COLOR : KEY_ON_BG_COLOR;
            fg = KEY_ON_TEXT_COLOR;
        }

        HBRUSH cellBrush = CreateSolidBrush(bg);
        if (cellBrush) {
            FillRect(hdc, &cell, cellBrush);
            DeleteObject(cellBrush);
        }
        SetTextColor(hdc, fg);
        DrawTextW(hdc, g_keys[i].label, -1, &cell,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (prevFont) SelectObject(hdc, prevFont);
}

// Monitor enumeration callback to find leftmost monitor
BOOL CALLBACK MonitorEnumProc(HMONITOR, HDC, LPRECT lprcMonitor, LPARAM dwData) {
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

    // Load the key selection before the window exists so the first paint is correct
    InitIniPath();
    LoadSettings();

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

    // Grow the requested client area by the frame metrics so the client ends up
    // exactly CLIENT_WIDTH x CLIENT_HEIGHT - no leftover margin on any edge.
    RECT frame = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
    AdjustWindowRectEx(&frame, WINDOW_STYLE, FALSE, WINDOW_EX_STYLE);

    // Create window - positioned at leftmost monitor's top-left corner
    hMainWnd = CreateWindowExW(
        WINDOW_EX_STYLE,
        CLASS_NAME,
        L"P2",
        WINDOW_STYLE,
        windowX, windowY,
        frame.right - frame.left, frame.bottom - frame.top,
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

    // Font for the Q/W/E/R toggle row (drawn directly in WM_PAINT)
    hFontKey = CreateFontW(
        KEY_FONT_SIZE, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

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

    // Start worker thread for key sending (decoupled from hook callback)
    g_exitThread = false;
    g_workerThread = CreateThread(NULL, 0, WorkerThreadProc, NULL, 0, NULL);

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

    // Signal worker thread to exit and wait for it
    g_exitThread = true;
    ClearPressedKeys();
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 1000);
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
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

        case WM_APP + 1:
            // UI update request from hook callback (F2/F3/focus change)
            UpdateUI();
            break;

        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                DrawKeyRow(hdc);
                EndPaint(hwnd, &ps);
            }
            return 0;

        case WM_LBUTTONDOWN:
            {
                int idx = HitTestKeyCell((short)LOWORD(lParam), (short)HIWORD(lParam));
                if (idx < 0) break;

                KeyEntry& key = g_keys[idx];
                key.selected = !key.selected;
                if (!key.selected) {
                    key.pressed = false;
                }
                SaveSettings();
                UpdateUI();
            }
            return 0;

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

            // Clean up fonts
            if (hFontStatus) {
                DeleteObject(hFontStatus);
                hFontStatus = NULL;
            }
            if (hFontKey) {
                DeleteObject(hFontKey);
                hFontKey = NULL;
            }

            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // CRITICAL: This callback must be extremely fast. NO keybd_event/SendInput
    // calls allowed here - they cause hook chain re-entry and system freeze.
    // All key sending is delegated to g_workerThread via flag polling.
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;

        // Skip injected keys (from our worker thread) - fast path
        if (pKeyboard->flags & LLKHF_INJECTED) {
            return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
        }

        const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

        // Handle F2 and F3 keys for enable/disable (flag-only, no UI calls)
        if (isDown) {
            if (pKeyboard->vkCode == VK_F2) {
                isEnabled = true;
                ClearPressedKeys();
                PostMessage(hMainWnd, WM_APP + 1, 0, 0); // Request UI update
                return 1;
            } else if (pKeyboard->vkCode == VK_F3) {
                isEnabled = false;
                ClearPressedKeys();
                PostMessage(hMainWnd, WM_APP + 1, 0, 0); // Request UI update
                return 1;
            }
        }

        for (int i = 0; i < KEY_COUNT; i++) {
            KeyEntry& key = g_keys[i];
            if (pKeyboard->vkCode != key.vk) continue;

            if (isUp) {
                key.pressed = false;
                // Swallow the keyup ONLY if we swallowed the matching keydown.
                // Deciding this from the current isEnabled instead would let the
                // target app keep the key stuck down whenever the app is turned
                // on while the key is already held.
                if (key.weBlockedDown) {
                    key.weBlockedDown = false;
                    return 1;
                }
                break;
            }

            // Key down: set flag (worker thread starts sending on next poll)
            if (isDown && isEnabled && key.selected) {
                key.pressed = true;
                key.weBlockedDown = true;
                return 1;
            }
            break;
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

void UpdateUI() {
    // Repaint only when the state actually changed. One full repaint costs
    // ~126us (max ~1.2ms measured) and it runs on the same thread that services
    // the keyboard hook, so skipping no-op repaints keeps the hook responsive.
    // The guard must cover EVERY value the window shows - master state and the
    // Q/W/E/R selection - or a change to one of them would be silently swallowed.
    // Starts at -1 so the initial paint always happens.
    static int lastState = -1;
    int state = isEnabled ? 1 : 0;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_keys[i].selected) state |= (2 << i);
    }
    if (state == lastState) {
        return;
    }
    lastState = state;

    SetWindowTextW(hStatusLabel, isEnabled ? L"ON" : L"OFF");
    InvalidateRect(hMainWnd, NULL, TRUE);
    UpdateWindow(hMainWnd);
}

void SendKey(DWORD vk) {
    // Called ONLY from worker thread (never from hook callback)
    // Injected keys are auto-flagged with LLKHF_INJECTED and skipped in hook
    BYTE scanCode = (BYTE)MapVirtualKey(vk, MAPVK_VK_TO_VSC);
    keybd_event((BYTE)vk, scanCode, 0, 0);
    keybd_event((BYTE)vk, scanCode, KEYEVENTF_KEYUP, 0);
}

// Worker thread: polls the per-key flags and sends every selected key that is
// currently held, then waits one interval. Holding two keys therefore sends
// both at the full rate rather than halving each.
// This thread is COMPLETELY decoupled from the hook callback, so hook chain
// re-entry (caused by keybd_event calls) does not freeze the input pipeline.
DWORD WINAPI WorkerThreadProc(LPVOID) {
    while (!g_exitThread) {
        bool sentAny = false;
        if (isEnabled) {
            for (int i = 0; i < KEY_COUNT; i++) {
                if (g_keys[i].selected && g_keys[i].pressed) {
                    SendKey(g_keys[i].vk);
                    sentAny = true;
                }
            }
        }

        if (sentAny) {
            // Sleep for the repeat interval (breaks early if exit requested)
            for (int i = 0; i < E_KEY_INTERVAL && !g_exitThread; i++) {
                Sleep(1);
                if (!isEnabled || !AnyKeyActive()) break; // Stop on release
            }
        } else {
            // Idle poll: one timer tick (~15.6ms) in practice
            Sleep(2);
        }
    }
    return 0;
}

// Check if a process ID belongs to League of Legends
bool IsLeagueOfLegendsProcess(DWORD processId) {
    if (processId == 0) return false;

    // PROCESS_QUERY_LIMITED_INFORMATION succeeds on processes that the older
    // PROCESS_QUERY_INFORMATION | PROCESS_VM_READ pair cannot open, so fewer
    // windows fall through to the title-substring fallback (a false-positive path).
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) return false;

    wchar_t processName[MAX_PATH] = {0};
    DWORD nameLen = MAX_PATH;
    bool isLoL = false;

    // Get the process executable name. PROCESS_NAME_NATIVE skips the
    // device-to-drive-letter translation (measured ~9x faster than
    // GetModuleFileNameExW); the path still uses backslashes so the
    // basename extraction below is unaffected. This runs on the main thread
    // via WinEventProc, which also services the keyboard hook.
    if (QueryFullProcessImageNameW(hProcess, PROCESS_NAME_NATIVE, processName, &nameLen)) {
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
            PostMessage(hMainWnd, WM_APP + 1, 0, 0);
        }
    } else {
        // Auto-disable when LoL loses focus
        if (isEnabled) {
            isEnabled = false;
            ClearPressedKeys();
            PostMessage(hMainWnd, WM_APP + 1, 0, 0);
        }
    }
}