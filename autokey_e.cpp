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
constexpr int MARGIN = 2;
constexpr int ROW_GAP = 2;

// Per-key toggle grid (owner drawn, click a cell to toggle it).
// KEY_COUNT keys fill the grid in array order, left to right then top to
// bottom; the last grid slot is the chain-mode star button, not a key.
constexpr int KEY_COUNT = 7;
constexpr int KEY_COLS = 4;
constexpr int KEY_ROWS = 2;
constexpr int KEY_CELL_W = 18;
constexpr int KEY_CELL_H = 20;
constexpr int GRID_CELL_COUNT = KEY_COLS * KEY_ROWS;   // 8 slots: 7 keys + star
constexpr int STAR_CELL = GRID_CELL_COUNT - 1;         // index of the star button
constexpr int KEY_ROW_W = KEY_COLS * KEY_CELL_W;
constexpr int KEY_GRID_H = KEY_ROWS * KEY_CELL_H;

// Status label sits directly above the grid and shares its width
constexpr int LABEL_X = MARGIN;
constexpr int LABEL_Y = MARGIN;
constexpr int LABEL_WIDTH = KEY_ROW_W;
constexpr int LABEL_HEIGHT = 24;

constexpr int KEY_ROW_X = MARGIN;
constexpr int KEY_ROW_Y = LABEL_Y + LABEL_HEIGHT + ROW_GAP;

// Speed slider row, owner drawn in WM_PAINT. A real SCROLLBAR control is not
// usable here: at KEY_ROW_W (72px) its two arrow buttons eat almost the whole
// track, leaving a few pixels to express 31..200ms. So the row is a plain
// track plus a filled portion, driven by SetCapture dragging (coarse) and the
// mouse wheel (1ms per notch).
constexpr int SLIDER_ROW_Y = KEY_ROW_Y + KEY_GRID_H + ROW_GAP;
constexpr int SLIDER_X = MARGIN;
constexpr int SLIDER_W = KEY_ROW_W;
// There is NO thumb bar: the right edge of the green filled portion IS the
// position indicator. A dark thumb drawn at that edge collided with the black
// readout in the middle of the track.
// The ms readout is drawn dead centre in the track in a font slightly larger
// than the key cells, so the row height is derived from that font size rather
// than picked by eye - otherwise the glyphs would clip vertically.
constexpr int SLIDER_FONT_SIZE = 16;
constexpr int SLIDER_H = SLIDER_FONT_SIZE + 4;

constexpr int CLIENT_WIDTH = KEY_ROW_W + MARGIN * 2;
constexpr int CLIENT_HEIGHT = SLIDER_ROW_Y + SLIDER_H + MARGIN;

// The grid is laid out as "all the keys, then the star in the last slot", so
// the star index and the key count must stay in lock step.
static_assert(KEY_COUNT == STAR_CELL, "last grid slot is the star button");
// UpdateUI packs isEnabled into bit 0 and the per-key selection into bits
// 1..KEY_COUNT, with chain mode just above them, in one unsigned.
static_assert(KEY_COUNT < 31, "UpdateUI guard bits");

// Settings are written to the ini on a debounce timer so that a burst of
// wheel notches does not do one file write per notch on the thread that also
// services the keyboard hook. This timer is UI persistence ONLY - it has
// nothing to do with key sending, which stays in the worker thread (a WM_TIMER
// based send loop is a deliberately abandoned design, see CLAUDE.md).
constexpr UINT_PTR SAVE_TIMER_ID = 1;
constexpr UINT SAVE_DEBOUNCE_MS = 500;

// Window style, shared by CreateWindowExW and the AdjustWindowRectEx call.
// No caption bar: it cost ~29px of height, half of the whole window. The window
// stays draggable (left-drag anywhere outside the key cells) and closable
// (right-click) - see WindowProc. WS_BORDER keeps a 1px outline so the widget
// is still visible against a dark desktop.
constexpr DWORD WINDOW_STYLE = WS_POPUP | WS_BORDER;
constexpr DWORD WINDOW_EX_STYLE = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

// Font settings. SLIDER_FONT_SIZE lives in the layout block above because the
// slider row height is derived from it.
constexpr int FONT_SIZE = 20;
constexpr int KEY_FONT_SIZE = 14;

// Repeat timing.
// The wait between sends is still a run of Sleep(1) calls - timeBeginPeriod is
// never used and no waitable timer is involved, so one Sleep(1) costs a full
// timer tick (TICK_MS, ~15.5ms measured). The worker therefore wakes ~64.7
// times/sec no matter what the interval is; the tick count only decides how
// many wakes pass before a key is sent.
//
// The user-facing value is the repeat period in milliseconds (g_intervalMs,
// driven by the slider). MIN_INTERVAL_MS is 2 ticks, the old E_KEY_INTERVAL = 2
// setting (~32 sends/sec); 1 tick would be the structural cap (~64/sec) but the
// minimum is deliberately 2 ticks. 0 ticks is never allowed (endless spam).
constexpr double TICK_MS = 15.5;
constexpr int MIN_TICKS = 2;
constexpr int MIN_INTERVAL_MS = 31;    // 2 ticks
constexpr int MAX_INTERVAL_MS = 200;
constexpr int DEFAULT_INTERVAL_MS = 115;   // midpoint of the slider range
constexpr int JITTER_MS = 6;               // +/- randomisation per send cycle

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
constexpr COLORREF KEY_WARN_BG_COLOR = RGB(198, 88, 28);    // Selected ultimate (R)
constexpr COLORREF KEY_OFF_BG_COLOR = RGB(214, 214, 214);   // Unselected key
constexpr COLORREF STAR_ON_BG_COLOR = RGB(218, 165, 32);    // Chain mode on (gold)
constexpr COLORREF KEY_ON_TEXT_COLOR = RGB(255, 255, 255);
constexpr COLORREF KEY_OFF_TEXT_COLOR = RGB(120, 120, 120);
constexpr COLORREF SLIDER_TEXT_COLOR = RGB(0, 0, 0);        // ms readout

// Per-key state.
// EVERY field here is written ONLY from the main thread - the hook callback,
// WinEventProc and WindowProc all run on the thread that installed the hook,
// and so does WinMain. The worker thread only ever reads. That single-writer
// rule is what makes plain volatile bool safe; never write these from the worker.
struct KeyEntry {
    DWORD vk;                     // virtual key code
    const wchar_t* label;         // text drawn in the toggle grid, also the .ini key name
    bool isUltimate;              // drawn in the accent colour (fires on cooldown)
    volatile bool selected;       // user turned this key on
    volatile bool pressed;        // physically held right now
    volatile bool weBlockedDown;  // we swallowed the keydown, so we must swallow the keyup
};

// E defaults on to preserve the original behaviour. R and the number row
// default off on purpose. '1'/'2'/'3' are the main keyboard row, not the numpad.
KeyEntry g_keys[KEY_COUNT] = {
    { 'Q', L"Q", false, false, false, false },
    { 'W', L"W", false, false, false, false },
    { 'E', L"E", false, true,  false, false },
    { 'R', L"R", true,  false, false, false },
    { '1', L"1", false, false, false, false },
    { '2', L"2", false, false, false, false },
    { '3', L"3", false, false, false, false },
};

// Global variables
HWND hMainWnd = NULL;
HWND hStatusLabel = NULL;
HHOOK hKeyboardHook = NULL;
HWINEVENTHOOK hWinEventHook = NULL;  // For League of Legends detection
HFONT hFontStatus = NULL;  // Track font resource
HFONT hFontKey = NULL;     // Font for the toggle grid
HFONT hFontSlider = NULL;  // Font for the ms readout on the slider
volatile bool isEnabled = false;
HBRUSH hBrushGreen = NULL;
HBRUSH hBrushDisabled = NULL;   // window/label background while OFF (light gray)
HBRUSH hBrushKeyOn = NULL;      // cached cell brushes, see DrawKeyGrid
HBRUSH hBrushKeyWarn = NULL;
HBRUSH hBrushKeyOff = NULL;
HBRUSH hBrushStarOn = NULL;
wchar_t g_iniPath[MAX_PATH] = {0};  // settings file, next to the exe

// Repeat period in ms and chain mode. Both are written ONLY from the main
// thread (WindowProc / WinMain); the worker thread only reads them.
volatile int g_intervalMs = DEFAULT_INTERVAL_MS;
volatile bool g_chainMode = false;

// Slider drag state and the pending-save flag. Touched only inside WindowProc
// (main thread), never by the worker thread, so neither needs volatile.
bool g_sliderCapture = false;
bool g_settingsDirty = false;

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
void MarkSettingsDirty(HWND hwnd);
void CommitSettings(HWND hwnd);
RECT GridCellRect(int idx);
RECT SliderRect();
int  HitTestKeyCell(int x, int y);
bool PtInSlider(int x, int y);
int  SliderXToInterval(int x);
int  IntervalToSliderX(int intervalMs);
void SetIntervalFromX(int x);
void NudgeInterval(HWND hwnd, int delta);
void DrawKeyGrid(HDC hdc);
void DrawSlider(HDC hdc);
int  IntervalToTicks(int intervalMs, double rndJitter, double rndDither);

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

    // Interval is clamped on load: a hand-edited or stale ini must never put
    // the worker below MIN_TICKS or above the slider range.
    UINT ms = GetPrivateProfileIntW(L"Options", L"IntervalMs",
                                    DEFAULT_INTERVAL_MS, g_iniPath);
    if (ms < (UINT)MIN_INTERVAL_MS) ms = (UINT)MIN_INTERVAL_MS;
    if (ms > (UINT)MAX_INTERVAL_MS) ms = (UINT)MAX_INTERVAL_MS;
    g_intervalMs = (int)ms;

    g_chainMode = (GetPrivateProfileIntW(L"Options", L"Chain", 0, g_iniPath) != 0);
}

// Appends "key=value" plus its terminating NUL to a section buffer.
static void AppendIniEntry(wchar_t* buf, size_t cap, size_t& len,
                           const wchar_t* key, const wchar_t* value) {
    const size_t need = wcslen(key) + 1 + wcslen(value) + 1;
    if (len + need + 1 > cap) return;   // +1 for the final terminator
    len += (size_t)wsprintfW(buf + len, L"%s=%s", key, value) + 1;
}

// One WritePrivateProfileSectionW per section instead of one
// WritePrivateProfileStringW per key. The per-key form reparsed and rewrote
// the whole file on every call, so saving cost 9 file rewrites; this costs 2.
// That matters because every caller runs on the thread that also services the
// keyboard hook.
void SaveSettings() {
    g_settingsDirty = false;
    if (!g_iniPath[0]) return;

    wchar_t keys[256];
    size_t klen = 0;
    for (int i = 0; i < KEY_COUNT; i++) {
        AppendIniEntry(keys, 256, klen, g_keys[i].label,
                       g_keys[i].selected ? L"1" : L"0");
    }
    keys[klen] = L'\0';     // section buffers end with a double NUL
    WritePrivateProfileSectionW(L"Keys", keys, g_iniPath);

    wchar_t msBuf[16];
    wsprintfW(msBuf, L"%d", g_intervalMs);
    wchar_t options[128];
    size_t olen = 0;
    AppendIniEntry(options, 128, olen, L"IntervalMs", msBuf);
    AppendIniEntry(options, 128, olen, L"Chain", g_chainMode ? L"1" : L"0");
    options[olen] = L'\0';
    WritePrivateProfileSectionW(L"Options", options, g_iniPath);
}

// Arm (or re-arm, which replaces the pending one) the debounced save.
void MarkSettingsDirty(HWND hwnd) {
    g_settingsDirty = true;
    SetTimer(hwnd, SAVE_TIMER_ID, SAVE_DEBOUNCE_MS, NULL);
}

// Write now and drop any pending debounce. Does nothing when no value changed.
void CommitSettings(HWND hwnd) {
    KillTimer(hwnd, SAVE_TIMER_ID);
    if (g_settingsDirty) {
        SaveSettings();
    }
}

// Single source of the grid geometry: both the painter and the hit test go
// through this, so a layout constant change can never desync the two.
// Returns the FULL cell rect (clicks in the 1px visual gap still count).
RECT GridCellRect(int idx) {
    const int col = idx % KEY_COLS;
    const int row = idx / KEY_COLS;
    RECT r;
    r.left = KEY_ROW_X + col * KEY_CELL_W;
    r.top = KEY_ROW_Y + row * KEY_CELL_H;
    r.right = r.left + KEY_CELL_W;
    r.bottom = r.top + KEY_CELL_H;
    return r;
}

// The slider row, from the same layout constants as the grid above it.
RECT SliderRect() {
    RECT r;
    r.left = SLIDER_X;
    r.top = SLIDER_ROW_Y;
    r.right = SLIDER_X + SLIDER_W;
    r.bottom = SLIDER_ROW_Y + SLIDER_H;
    return r;
}

bool PtInSlider(int x, int y) {
    RECT r = SliderRect();
    return (x >= r.left && x < r.right && y >= r.top && y < r.bottom);
}

// Pixel <-> ms mapping. SLIDER_W - 1 is the number of pixel steps, and both
// conversions round to nearest so the leftmost pixel is exactly
// MIN_INTERVAL_MS and the rightmost pixel is exactly MAX_INTERVAL_MS
// (a truncating map would stop one short of the maximum).
int SliderXToInterval(int x) {
    const int span = SLIDER_W - 1;
    const int range = MAX_INTERVAL_MS - MIN_INTERVAL_MS;
    int rel = x - SLIDER_X;
    if (rel < 0) rel = 0;
    if (rel > span) rel = span;
    return MIN_INTERVAL_MS + (rel * range + span / 2) / span;
}

int IntervalToSliderX(int intervalMs) {
    const int span = SLIDER_W - 1;
    const int range = MAX_INTERVAL_MS - MIN_INTERVAL_MS;
    int v = intervalMs - MIN_INTERVAL_MS;
    if (v < 0) v = 0;
    if (v > range) v = range;
    return SLIDER_X + (v * span + range / 2) / range;
}

// Both of these run on the main thread only (WindowProc), which keeps
// g_intervalMs single-writer. NEITHER writes the ini: they only repaint and
// flag the change. A drag commits once on WM_LBUTTONUP; the wheel leaves it to
// the debounce timer, because one notch per file rewrite would stall the
// thread that services the keyboard hook.
void SetIntervalFromX(int x) {
    const int v = SliderXToInterval(x);
    if (v != g_intervalMs) {
        g_intervalMs = v;
        g_settingsDirty = true;
        UpdateUI();
    }
}

void NudgeInterval(HWND hwnd, int delta) {
    int v = g_intervalMs + delta;
    if (v < MIN_INTERVAL_MS) v = MIN_INTERVAL_MS;
    if (v > MAX_INTERVAL_MS) v = MAX_INTERVAL_MS;
    if (v != g_intervalMs) {
        g_intervalMs = v;
        UpdateUI();
        MarkSettingsDirty(hwnd);
    }
}

// Returns the grid slot at (x, y) in client coords, or -1.
// 0..KEY_COUNT-1 are keys; STAR_CELL is the chain-mode button.
int HitTestKeyCell(int x, int y) {
    for (int i = 0; i < GRID_CELL_COUNT; i++) {
        RECT r = GridCellRect(i);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) {
            return i;
        }
    }
    return -1;
}

void DrawKeyGrid(HDC hdc) {
    HFONT prevFont = NULL;
    if (hFontKey) prevFont = (HFONT)SelectObject(hdc, hFontKey);
    SetBkMode(hdc, TRANSPARENT);

    for (int i = 0; i < GRID_CELL_COUNT; i++) {
        RECT cell = GridCellRect(i);
        // 1px gap on the right/bottom edges keeps the cells visually separate
        cell.right -= 1;
        cell.bottom -= 1;

        const bool isStar = (i == STAR_CELL);
        const bool on = isStar ? (g_chainMode != false) : g_keys[i].selected;

        HBRUSH brush = hBrushKeyOff;
        COLORREF fg = KEY_OFF_TEXT_COLOR;
        if (on) {
            if (isStar) {
                brush = hBrushStarOn;
            } else {
                brush = g_keys[i].isUltimate ? hBrushKeyWarn : hBrushKeyOn;
            }
            fg = KEY_ON_TEXT_COLOR;
        }

        if (brush) FillRect(hdc, &cell, brush);
        SetTextColor(hdc, fg);
        DrawTextW(hdc, isStar ? L"★" : g_keys[i].label, -1, &cell,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (prevFont) SelectObject(hdc, prevFont);
}

// Owner-drawn speed slider: track, green filled portion whose right edge is
// the position indicator (no thumb bar), and the current ms value drawn last,
// centered on the track in black so it stays readable over both colours.
void DrawSlider(HDC hdc) {
    const RECT track = SliderRect();
    const int fillX = IntervalToSliderX(g_intervalMs);

    // Gauge: unfilled track in the unselected-key grey, filled portion in the
    // selected-key green. The boundary between them is the current value, so
    // there is no extra thumb to collide with the readout drawn on top.
    if (hBrushKeyOff) FillRect(hdc, &track, hBrushKeyOff);

    RECT filled = track;
    // IntervalToSliderX maps MAX to the last PIXEL of the track (track.right-1)
    // because it is a hit-test coordinate, which would leave one grey column at
    // the far right. Only the painted width is special-cased; the mapping stays
    // exactly as the round-trip hit tests verified it.
    filled.right = (g_intervalMs >= MAX_INTERVAL_MS) ? track.right : fillX;
    if (filled.right > track.left && hBrushKeyOn) {
        FillRect(hdc, &filled, hBrushKeyOn);
    }

    // The readout is centred horizontally and vertically in the track and does
    // NOT follow the fill edge, so it stays put as the value changes. Always
    // black on a transparent background, drawn last so it reads over both the
    // green fill and the grey track. Digits only - no "ms" suffix, the track is
    // only SLIDER_W px wide.
    HFONT prevFont = NULL;
    if (hFontSlider) prevFont = (HFONT)SelectObject(hdc, hFontSlider);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, SLIDER_TEXT_COLOR);

    wchar_t buf[16];
    wsprintfW(buf, L"%d", g_intervalMs);
    RECT textRect = track;
    DrawTextW(hdc, buf, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    if (prevFont) SelectObject(hdc, prevFont);
}

// Pure helper: repeat period in ms -> number of Sleep(1) calls to wait.
// rndJitter and rndDither are both uniform in [0, 1).
//   1. jitter the target by +/- JITTER_MS and clamp it to the slider range
//   2. stochastic rounding (dithering) of target / TICK_MS, so the AVERAGE
//      period matches the setting even though a tick is a coarse ~15.5ms
//   3. never fewer than MIN_TICKS ticks
// At the two ends of the range the clamp in step 1 is one-sided, so the mean
// is pulled inwards: a setting of 31 averages ~32.5ms and 200 averages
// ~198.5ms (measured over 100k draws). That is intentional - the alternative
// is letting the jitter push the period below the floor the user asked for.
// In the middle of the range the mean matches the setting (115 -> 114.98ms).
// Kept free of globals so it can be unit tested outside the app.
int IntervalToTicks(int intervalMs, double rndJitter, double rndDither) {
    double target = (double)intervalMs + (rndJitter * 2.0 - 1.0) * (double)JITTER_MS;
    if (target < (double)MIN_INTERVAL_MS) target = (double)MIN_INTERVAL_MS;
    if (target > (double)MAX_INTERVAL_MS) target = (double)MAX_INTERVAL_MS;

    int n = (int)(target / TICK_MS + rndDither);   // positive, so this floors
    if (n < MIN_TICKS) n = MIN_TICKS;
    return n;
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

    // The speed slider is not a child control - it is drawn in WM_PAINT and
    // driven from WindowProc (see DrawSlider / WM_LBUTTONDOWN / WM_MOUSEWHEEL).

    // Set larger font for status
    hFontStatus = CreateFontW(
        FONT_SIZE, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
    if (hFontStatus) {
        SendMessage(hStatusLabel, WM_SETFONT, (WPARAM)hFontStatus, TRUE);
    }

    // Font for the key toggle grid (drawn directly in WM_PAINT)
    hFontKey = CreateFontW(
        KEY_FONT_SIZE, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );

    // Slightly larger bold font for the ms readout on the slider
    hFontSlider = CreateFontW(
        SLIDER_FONT_SIZE, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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

    // Hooks are released AFTER the worker join above. Between the end of the
    // message loop and this point the low-level hook is still installed but
    // nobody pumps messages for it, so every keystroke waits for
    // LowLevelHooksTimeout. Keep that window short: do not add cleanup work
    // before this block (see CLAUDE.md, shutdown order).
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
            // Create brushes once. The per-cell brushes used to be created and
            // destroyed on every cell of every repaint; caching them keeps the
            // paint (which runs on the hook-servicing thread) cheap.
            hBrushGreen = CreateSolidBrush(ENABLED_BG_COLOR);
            hBrushDisabled = CreateSolidBrush(DISABLED_BG_COLOR);
            hBrushKeyOn = CreateSolidBrush(KEY_ON_BG_COLOR);
            hBrushKeyWarn = CreateSolidBrush(KEY_WARN_BG_COLOR);
            hBrushKeyOff = CreateSolidBrush(KEY_OFF_BG_COLOR);
            hBrushStarOn = CreateSolidBrush(STAR_ON_BG_COLOR);
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
                    return (INT_PTR)hBrushDisabled;
                }
            }
            break;

        case WM_ERASEBKGND:
            {
                HDC hdc = (HDC)wParam;
                RECT rect;
                GetClientRect(hwnd, &rect);
                FillRect(hdc, &rect, isEnabled ? hBrushGreen : hBrushDisabled);
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
                DrawKeyGrid(hdc);
                DrawSlider(hdc);
                EndPaint(hwnd, &ps);
            }
            return 0;

        case WM_MOUSEWHEEL:
            {
                // Fine adjustment: the 72px track is ~2.4ms per pixel, so the
                // wheel is the only way to land on an exact value.
                const int delta = (int)(short)HIWORD(wParam);
                if (delta == 0) return 0;
                int step = delta / WHEEL_DELTA;
                if (step == 0) step = (delta > 0) ? 1 : -1;  // partial notch
                NudgeInterval(hwnd, step);
            }
            return 0;

        case WM_TIMER:
            // Debounced settings save ONLY - this timer never sends keys.
            if (wParam == SAVE_TIMER_ID) {
                KillTimer(hwnd, SAVE_TIMER_ID);
                if (g_settingsDirty) {
                    SaveSettings();
                }
                return 0;
            }
            break;

        case WM_MOUSEMOVE:
            if (g_sliderCapture) {
                SetIntervalFromX((short)LOWORD(lParam));
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (g_sliderCapture) {
                g_sliderCapture = false;
                ReleaseCapture();
                CommitSettings(hwnd);   // one ini write per drag, not per pixel
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            // Capture lost to someone else mid-drag: still persist the value.
            if (g_sliderCapture) {
                g_sliderCapture = false;
                CommitSettings(hwnd);
            }
            return 0;

        case WM_LBUTTONDOWN:
            {
                const int mx = (short)LOWORD(lParam);
                const int my = (short)HIWORD(lParam);

                // Slider first: a click inside the track must jump the value,
                // not get handed to the window-move loop below.
                if (PtInSlider(mx, my)) {
                    g_sliderCapture = true;
                    SetCapture(hwnd);
                    SetIntervalFromX(mx);
                    return 0;
                }

                int idx = HitTestKeyCell(mx, my);
                if (idx < 0) {
                    // Outside the grid cells: drag the window. There is no
                    // caption bar to grab, so hand the click to the default
                    // move loop as if it had landed on one.
                    ReleaseCapture();
                    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                    return 0;
                }

                if (idx == STAR_CELL) {
                    // Chain mode: one held key fires every selected key.
                    g_chainMode = !g_chainMode;
                } else {
                    KeyEntry& key = g_keys[idx];
                    key.selected = !key.selected;
                    if (!key.selected) {
                        key.pressed = false;
                    }
                }
                g_settingsDirty = true;
                CommitSettings(hwnd);   // a discrete toggle persists at once
                UpdateUI();
            }
            return 0;

        case WM_RBUTTONDOWN:
            if (g_sliderCapture) {
                // Right-click during a slider drag cancels the drag, it does
                // NOT quit: losing the app to a stray second button while
                // dragging would be the worst possible surprise. The value the
                // drag reached is kept.
                g_sliderCapture = false;
                ReleaseCapture();
                CommitSettings(hwnd);
                return 0;
            }
            // No caption bar means no close button; right-click exits instead.
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;

        case WM_DESTROY:
            // Last chance to persist a debounced change that has not fired yet
            CommitSettings(hwnd);

            // Clean up brushes
            if (hBrushGreen) { DeleteObject(hBrushGreen); hBrushGreen = NULL; }
            if (hBrushDisabled) { DeleteObject(hBrushDisabled); hBrushDisabled = NULL; }
            if (hBrushKeyOn) { DeleteObject(hBrushKeyOn); hBrushKeyOn = NULL; }
            if (hBrushKeyWarn) { DeleteObject(hBrushKeyWarn); hBrushKeyWarn = NULL; }
            if (hBrushKeyOff) { DeleteObject(hBrushKeyOff); hBrushKeyOff = NULL; }
            if (hBrushStarOn) { DeleteObject(hBrushStarOn); hBrushStarOn = NULL; }

            // Clean up fonts
            if (hFontStatus) {
                DeleteObject(hFontStatus);
                hFontStatus = NULL;
            }
            if (hFontKey) {
                DeleteObject(hFontKey);
                hFontKey = NULL;
            }
            if (hFontSlider) {
                DeleteObject(hFontSlider);
                hFontSlider = NULL;
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
    // The guard must cover EVERY value the window shows - master state, the
    // per-key selection, chain mode and the interval readout - or a change to
    // one of them would be silently swallowed.
    // Starts at an impossible value so the initial paint always happens.
    static unsigned lastBits = 0xFFFFFFFFu;
    static int lastInterval = -1;

    unsigned bits = isEnabled ? 1u : 0u;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_keys[i].selected) bits |= (2u << i);
    }
    if (g_chainMode) bits |= (2u << KEY_COUNT);

    const int interval = g_intervalMs;
    if (bits == lastBits && interval == lastInterval) {
        return;
    }
    const bool onlyInterval = (bits == lastBits);
    lastBits = bits;
    lastInterval = interval;

    if (onlyInterval) {
        // Dragging the slider lands here on nearly every mouse move. Erasing
        // and repainting the whole client each time made the window flash, and
        // DrawSlider repaints the entire track itself, so no erase is needed.
        RECT sr = SliderRect();
        InvalidateRect(hMainWnd, &sr, FALSE);
        UpdateWindow(hMainWnd);
        return;
    }

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

// Worker-local PRNG. xorshift32 instead of <random> to keep the statically
// linked binary small, and the state is a stack local so the worker never
// writes to any global (single-writer invariant).
static inline unsigned XorShift32(unsigned& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Uniform double in [0, 1), 24 bits of mantissa.
static inline double Rnd01(unsigned& s) {
    return (double)(XorShift32(s) >> 8) * (1.0 / 16777216.0);
}

// Worker thread: polls the per-key flags and sends keys, then waits out the
// (jittered) repeat interval as a run of Sleep(1) calls.
//
// Parallel mode (default): every selected key that is currently HELD is sent
// once per cycle. Holding two keys therefore sends both at the full rate
// rather than halving each.
//
// Chain mode (the star button): if ANY selected key is held, every SELECTED
// key is injected back to back within the same cycle - a burst, not a spread -
// and then the one interval wait runs as usual. So the cycle rate is identical
// to parallel mode; what changes is that one held key produces the whole burst
// (holding Q with Q/W/E selected gives qwe qwe qwe). The per-cycle injection
// count therefore scales with the number of SELECTED keys, not held ones.
// The hook needs no change for this; only the physically pressed key is
// swallowed and the rest go out as injected input.
//
// This thread is COMPLETELY decoupled from the hook callback, so hook chain
// re-entry (caused by keybd_event calls) does not freeze the input pipeline.
// It only ever READS the globals; all writes happen on the main thread.
DWORD WINAPI WorkerThreadProc(LPVOID) {
    unsigned rngState = (unsigned)GetTickCount() ^ (unsigned)GetCurrentThreadId();
    if (rngState == 0) rngState = 0x9E3779B9u;   // xorshift must never be seeded 0

    while (!g_exitThread) {
        bool sentAny = false;
        if (isEnabled) {
            if (g_chainMode) {
                // One read of the hold state per cycle, then fire the whole chain
                if (AnyKeyActive()) {
                    for (int i = 0; i < KEY_COUNT; i++) {
                        if (g_keys[i].selected) {
                            SendKey(g_keys[i].vk);
                            sentAny = true;
                        }
                    }
                }
            } else {
                for (int i = 0; i < KEY_COUNT; i++) {
                    if (g_keys[i].selected && g_keys[i].pressed) {
                        SendKey(g_keys[i].vk);
                        sentAny = true;
                    }
                }
            }
        }

        if (sentAny) {
            // Wait out the interval. One Sleep(1) == one timer tick (TICK_MS),
            // so the ms setting is converted to a tick count with jitter and
            // stochastic rounding. Re-checking the flags after each Sleep(1)
            // stops within one tick of the key being released instead of
            // sitting out the whole remaining interval.
            const int ticks = IntervalToTicks(g_intervalMs, Rnd01(rngState), Rnd01(rngState));
            for (int i = 0; i < ticks && !g_exitThread; i++) {
                Sleep(1);
                if (!isEnabled || !AnyKeyActive()) break; // Stop on release
            }
        } else {
            // Idle poll: one timer tick (TICK_MS, ~15.5ms) in practice
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
