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
// The grid is a fixed KEY_COLS x KEY_ROWS slot array. Slots are NOT all keys
// and the slot index is NOT the g_keys index any more: row 1 ends with the
// chain-mode star and row 2 ends with the mouse-mode button. GRID_SLOTS below
// is the single source of that mapping - the painter, the hit test and the
// click handler all branch on it.
constexpr int KEY_COUNT = 10;
constexpr int KEY_COLS = 4;
constexpr int KEY_ROWS = 3;
constexpr int KEY_CELL_W = 18;
constexpr int KEY_CELL_H = 20;
constexpr int GRID_CELL_COUNT = KEY_COLS * KEY_ROWS;   // 12 slots: 10 keys + star + mouse
constexpr int KEY_ROW_W = KEY_COLS * KEY_CELL_W;
constexpr int KEY_GRID_H = KEY_ROWS * KEY_CELL_H;

// What a grid slot does. keyIndex is the g_keys index for Key slots and -1
// otherwise.
struct GridSlot {
    enum Kind { Key, Star, Mouse };
    Kind kind;
    int keyIndex;
};

// Layout:  Q W E R  /  1 2 3 *  /  4 5 6 M
constexpr GridSlot GRID_SLOTS[GRID_CELL_COUNT] = {
    { GridSlot::Key, 0 }, { GridSlot::Key, 1 }, { GridSlot::Key, 2 }, { GridSlot::Key, 3 },
    { GridSlot::Key, 4 }, { GridSlot::Key, 5 }, { GridSlot::Key, 6 }, { GridSlot::Star, -1 },
    { GridSlot::Key, 7 }, { GridSlot::Key, 8 }, { GridSlot::Key, 9 }, { GridSlot::Mouse, -1 },
};

// Status label sits directly above the grid and shares its width
constexpr int LABEL_X = MARGIN;
constexpr int LABEL_Y = MARGIN;
constexpr int LABEL_WIDTH = KEY_ROW_W;
constexpr int LABEL_HEIGHT = 24;

constexpr int KEY_ROW_X = MARGIN;
constexpr int KEY_ROW_Y = LABEL_Y + LABEL_HEIGHT + ROW_GAP;

// Speed slider row, owner drawn in WM_PAINT. A real SCROLLBAR control is not
// usable here: at KEY_ROW_W (72px) its two arrow buttons eat almost the whole
// track, leaving a few pixels to express 35..200ms. So the row is a plain
// track plus a filled portion, driven by SetCapture dragging (coarse) and the
// mouse wheel (one INTERVAL_STEP_MS step per notch).
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

// Every key must own exactly one grid slot, and every Key slot must point at a
// valid, distinct g_keys index. Counted at compile time so adding a key without
// giving it a slot (or giving two slots the same key) fails the build instead of
// producing a cell that silently toggles the wrong thing.
constexpr int CountSlotKind(GridSlot::Kind kind) {
    int n = 0;
    for (int i = 0; i < GRID_CELL_COUNT; i++) {
        if (GRID_SLOTS[i].kind == kind) n++;
    }
    return n;
}
constexpr bool EveryKeyHasOneSlot() {
    for (int k = 0; k < KEY_COUNT; k++) {
        int seen = 0;
        for (int i = 0; i < GRID_CELL_COUNT; i++) {
            if (GRID_SLOTS[i].kind == GridSlot::Key && GRID_SLOTS[i].keyIndex == k) seen++;
        }
        if (seen != 1) return false;
    }
    return true;
}
static_assert(CountSlotKind(GridSlot::Key) == KEY_COUNT,
              "grid Key slot count must equal KEY_COUNT");
static_assert(EveryKeyHasOneSlot(), "each key needs exactly one grid slot");
static_assert(CountSlotKind(GridSlot::Star) == 1, "exactly one chain-mode star slot");
static_assert(CountSlotKind(GridSlot::Mouse) == 1, "exactly one mouse-mode slot");
// UpdateUI packs isEnabled into bit 0, the per-key selection into bits
// 1..KEY_COUNT, chain mode into bit KEY_COUNT+1 and the 2-bit mouse mode into
// bits KEY_COUNT+2..KEY_COUNT+3, all in one unsigned.
static_assert(KEY_COUNT < 29, "UpdateUI guard bits");

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
// driven by the slider). It is quantised to INTERVAL_STEP_MS, so the value set
// is 35, 40, ... 200 - evenly spaced, no special case at either end.
// MIN_INTERVAL_MS is ~2.26 ticks: MIN_TICKS (2) stays the hard floor and the
// dithering in IntervalToTicks spreads 35ms over 2 and 3 ticks. 1 tick would be
// the structural cap (~64 sends/sec). 0 ticks is never allowed (endless spam).
constexpr double TICK_MS = 15.5;
constexpr int MIN_TICKS = 2;
constexpr int MIN_INTERVAL_MS = 35;
constexpr int MAX_INTERVAL_MS = 200;
constexpr int INTERVAL_STEP_MS = 5;        // slider/wheel granularity
constexpr int DEFAULT_INTERVAL_MS = 100;   // on the step grid, mid-range
constexpr int JITTER_MS = 6;               // +/- randomisation per send cycle

// Mouse click repeat mode, cycled by the M grid cell: off -> right -> left.
// Stored as an int (not two bools) so the cell is a single 3-state control and
// the ini holds one value.
constexpr int MOUSE_MODE_OFF = 0;
constexpr int MOUSE_MODE_RIGHT = 1;
constexpr int MOUSE_MODE_LEFT = 2;
constexpr int MOUSE_MODE_COUNT = 3;

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
constexpr COLORREF MOUSE_RIGHT_BG_COLOR = RGB(200, 40, 40);  // M cell: right click
constexpr COLORREF MOUSE_LEFT_BG_COLOR = RGB(40, 90, 200);   // M cell: left click
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
// default off on purpose. '1'..'6' are the main keyboard row, not the numpad.
KeyEntry g_keys[KEY_COUNT] = {
    { 'Q', L"Q", false, false, false, false },
    { 'W', L"W", false, false, false, false },
    { 'E', L"E", false, true,  false, false },
    { 'R', L"R", true,  false, false, false },
    { '1', L"1", false, false, false, false },
    { '2', L"2", false, false, false, false },
    { '3', L"3", false, false, false, false },
    { '4', L"4", false, false, false, false },
    { '5', L"5", false, false, false, false },
    { '6', L"6", false, false, false, false },
};

// Global variables
HWND hMainWnd = NULL;
HWND hStatusLabel = NULL;
HHOOK hKeyboardHook = NULL;
HHOOK hMouseHook = NULL;             // WH_MOUSE_LL, same thread as the keyboard hook
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
HBRUSH hBrushMouseRight = NULL; // M cell while cycled to right click
HBRUSH hBrushMouseLeft = NULL;  // M cell while cycled to left click
wchar_t g_iniPath[MAX_PATH] = {0};  // settings file, next to the exe

// Repeat period in ms and chain mode. Both are written ONLY from the main
// thread (WindowProc / WinMain); the worker thread only reads them.
volatile int g_intervalMs = DEFAULT_INTERVAL_MS;
volatile bool g_chainMode = false;

// Mouse click repeat state. Same single-writer rule as the key flags: written
// only from the main thread (WindowProc toggle / LowLevelMouseProc /
// WinEventProc via ClearPressedKeys), read by the worker.
// g_mouseBlockedDown holds WHICH button's down we swallowed (MOUSE_MODE_*),
// not just "some button": it is the mouse twin of KeyEntry::weBlockedDown, and
// the button has to match or a left click taken while the right button is held
// would get its up swallowed and leave that button stuck down in the target app.
volatile int g_mouseMode = MOUSE_MODE_OFF;
volatile bool g_mousePressed = false;
volatile int g_mouseBlockedDown = MOUSE_MODE_OFF;

// Is League of Legends the foreground window? Seeded in WinMain and refreshed
// by WinEventProc on every foreground change (both main thread). Unlike the
// keyboard path, mouse blocking is gated on this: a globally swallowed LEFT
// click would stop the user from even switching windows to escape. The
// deliberate consequence is that turning the app on with F2 outside LoL repeats
// the KEYS but not the mouse.
volatile bool g_lolForeground = false;

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
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);
VOID CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG, DWORD, DWORD);
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData);
DWORD WINAPI WorkerThreadProc(LPVOID lpParam);
bool IsLeagueOfLegendsWindow(HWND hwnd);
bool IsLeagueOfLegendsProcess(DWORD processId);
void UpdateUI();
void SendKey(DWORD vk);
void SendMouseClick(int mode);
void ClearPressedKeys();
bool AnyKeyActive();
bool MouseActive();
bool PointInOwnWindow(POINT pt, const RECT& r);
bool ShouldBlockMouseDown(int mouseMode, bool rightButton, bool enabled,
                          bool lolForeground, bool overOwnWindow);
bool ShouldSwallowInjectedClick(int mouseMode, bool enabled, bool overOwnWindow);
void InitIniPath();
void LoadSettings();
void SaveSettings();
void MarkSettingsDirty(HWND hwnd);
void CommitSettings(HWND hwnd);
RECT GridCellRect(int idx);
RECT SliderRect();
int  HitTestKeyCell(int x, int y);
bool PtInSlider(int x, int y);
int  SnapInterval(int ms);
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
    // Same reasoning for the mouse button: drop the physical hold, keep
    // g_mouseBlockedDown so the swallowed button-down still gets its up.
    g_mousePressed = false;
}

bool AnyKeyActive() {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_keys[i].selected && g_keys[i].pressed) return true;
    }
    return false;
}

// The mouse twin of AnyKeyActive: the M cell is on and its button is held.
bool MouseActive() {
    return g_mouseMode != MOUSE_MODE_OFF && g_mousePressed;
}

// Pure: is pt inside the rect (right/bottom exclusive, same convention as the
// grid hit test)? Kept free of globals so it can be unit tested outside the app.
bool PointInOwnWindow(POINT pt, const RECT& r) {
    return (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom);
}

// Pure: does the low-level mouse hook swallow this PHYSICAL button-down?
// The "over our own window" exception is mandatory: the mouse hook fires before
// the window is activated, so without it a right-click on P2 while LoL is the
// foreground window (mode = right) would be eaten and the app could never be
// closed - and in left mode no grid cell could be clicked either.
// lolForeground is the second mandatory gate - see g_lolForeground.
bool ShouldBlockMouseDown(int mouseMode, bool rightButton, bool enabled,
                          bool lolForeground, bool overOwnWindow) {
    if (!enabled) return false;
    if (!lolForeground) return false;
    if (mouseMode == MOUSE_MODE_OFF) return false;
    if (overOwnWindow) return false;
    return rightButton ? (mouseMode == MOUSE_MODE_RIGHT) : (mouseMode == MOUSE_MODE_LEFT);
}

// Pure: does the hook swallow an INJECTED button message?
// Normally injected input passes straight through (same scope as the keyboard
// hook). The one exception is our own worker's click landing on P2 itself: the
// cursor can be parked over the window, and a right click there would close the
// app on the next cycle while a left click would toggle a cell - and rewrite the
// ini - once per cycle. Deliberately NOT gated on lolForeground, because the
// worker only looks at isEnabled when it decides to fire.
// The gate stays as narrow as possible (our modes, enabled, over our rect) so
// other programs' injected clicks keep passing through everywhere else.
bool ShouldSwallowInjectedClick(int mouseMode, bool enabled, bool overOwnWindow) {
    return enabled && mouseMode != MOUSE_MODE_OFF && overOwnWindow;
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

    // Interval is clamped AND snapped on load: a hand-edited or stale ini must
    // never put the worker outside the slider range or off the step grid.
    UINT ms = GetPrivateProfileIntW(L"Options", L"IntervalMs",
                                    DEFAULT_INTERVAL_MS, g_iniPath);
    if (ms > (UINT)MAX_INTERVAL_MS) ms = (UINT)MAX_INTERVAL_MS;
    g_intervalMs = SnapInterval((int)ms);

    g_chainMode = (GetPrivateProfileIntW(L"Options", L"Chain", 0, g_iniPath) != 0);

    // Mouse mode is clamped the same way: a hand-edited value must never put
    // the M cell into a state the painter and the worker do not know.
    UINT mm = GetPrivateProfileIntW(L"Options", L"Mouse", MOUSE_MODE_OFF, g_iniPath);
    if (mm >= (UINT)MOUSE_MODE_COUNT) mm = MOUSE_MODE_OFF;
    g_mouseMode = (int)mm;
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
    wchar_t mouseBuf[16];
    wsprintfW(mouseBuf, L"%d", g_mouseMode);
    AppendIniEntry(options, 128, olen, L"Mouse", mouseBuf);
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

// Pure: quantise a period to the INTERVAL_STEP_MS grid, then clamp it to the
// slider range. Both bounds are multiples of the step, so the reachable set is
// exactly 35, 40, ... 200 with no end special case. Kept free of globals so it
// can be unit tested outside the app.
int SnapInterval(int ms) {
    if (ms < 0) ms = 0;
    int snapped = ((ms + INTERVAL_STEP_MS / 2) / INTERVAL_STEP_MS) * INTERVAL_STEP_MS;
    if (snapped < MIN_INTERVAL_MS) snapped = MIN_INTERVAL_MS;
    if (snapped > MAX_INTERVAL_MS) snapped = MAX_INTERVAL_MS;
    return snapped;
}

// Pixel <-> ms mapping. SLIDER_W - 1 is the number of pixel steps, and both
// conversions round to nearest so the leftmost pixel is exactly
// MIN_INTERVAL_MS and the rightmost pixel is exactly MAX_INTERVAL_MS
// (a truncating map would stop one short of the maximum). The linear map is
// unchanged; SnapInterval only quantises its output, and because both ends of
// the range are on the step grid the two end pixels still land exactly on
// MIN/MAX. Several adjacent pixels therefore share one value (~2.3ms per pixel
// against a 5ms step) - that is the point of the step.
int SliderXToInterval(int x) {
    const int span = SLIDER_W - 1;
    const int range = MAX_INTERVAL_MS - MIN_INTERVAL_MS;
    int rel = x - SLIDER_X;
    if (rel < 0) rel = 0;
    if (rel > span) rel = span;
    return SnapInterval(MIN_INTERVAL_MS + (rel * range + span / 2) / span);
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
    // One wheel notch moves one step on the grid; the snap then also absorbs a
    // value left off-grid by an older ini.
    const int v = SnapInterval(g_intervalMs + delta * INTERVAL_STEP_MS);
    if (v != g_intervalMs) {
        g_intervalMs = v;
        UpdateUI();
        MarkSettingsDirty(hwnd);
    }
}

// Returns the grid SLOT index at (x, y) in client coords, or -1. What the slot
// means is in GRID_SLOTS[idx] - it is not a g_keys index.
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

        // Everything the cell shows comes from its slot kind - no slot index
        // arithmetic, so a layout change only touches GRID_SLOTS.
        HBRUSH brush = hBrushKeyOff;
        COLORREF fg = KEY_OFF_TEXT_COLOR;
        const wchar_t* text = L"";

        if (GRID_SLOTS[i].kind == GridSlot::Star) {
            text = L"★";
            if (g_chainMode) {
                brush = hBrushStarOn;
                fg = KEY_ON_TEXT_COLOR;
            }
        } else if (GRID_SLOTS[i].kind == GridSlot::Mouse) {
            // The letter itself reports the mode: M off, R right, L left.
            const int mode = g_mouseMode;
            if (mode == MOUSE_MODE_RIGHT) {
                text = L"R";
                brush = hBrushMouseRight;
                fg = KEY_ON_TEXT_COLOR;
            } else if (mode == MOUSE_MODE_LEFT) {
                text = L"L";
                brush = hBrushMouseLeft;
                fg = KEY_ON_TEXT_COLOR;
            } else {
                text = L"M";
            }
        } else {
            const KeyEntry& key = g_keys[GRID_SLOTS[i].keyIndex];
            text = key.label;
            if (key.selected) {
                brush = key.isUltimate ? hBrushKeyWarn : hBrushKeyOn;
                fg = KEY_ON_TEXT_COLOR;
            }
        }

        if (brush) FillRect(hdc, &cell, brush);
        SetTextColor(hdc, fg);
        DrawTextW(hdc, text, -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
// is pulled inwards: a setting of 35 averages ~36.5ms and 200 averages
// ~198.5ms (measured over 100k draws). That is intentional - the alternative
// is letting the jitter push the period below the floor the user asked for.
// In the middle of the range the mean matches the setting (100 -> ~100ms).
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

    // Install low-level mouse hook (same thread, same flag-only contract)
    hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, hInstance, 0);

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
    const bool lolAtStart = IsLeagueOfLegendsWindow(foregroundWindow);
    g_lolForeground = lolAtStart;   // seed the mouse hook's foreground gate
    if (lolAtStart) {
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

    // Signal worker thread to exit
    g_exitThread = true;
    ClearPressedKeys();

    // Hooks come off FIRST, before the worker join. A low-level hook callback is
    // delivered to the thread that installed it, so from the moment the message
    // loop ends until the hook is released nobody services it and every
    // keystroke/click costs a full LowLevelHooksTimeout (~300ms) system wide.
    // Releasing here makes that window as short as it can be: the join below can
    // take up to 1000ms. Both LL callbacks only touch flags, so unhooking them
    // while the worker is still finishing its last cycle is safe - the worker
    // reads flags and sends input, it never needs the hooks.
    if (hKeyboardHook) {
        UnhookWindowsHookEx(hKeyboardHook);
        hKeyboardHook = NULL;
    }

    if (hMouseHook) {
        UnhookWindowsHookEx(hMouseHook);
        hMouseHook = NULL;
    }

    if (hWinEventHook) {
        UnhookWinEvent(hWinEventHook);
        hWinEventHook = NULL;
    }

    // Now wait for the worker to notice g_exitThread and finish.
    if (g_workerThread) {
        WaitForSingleObject(g_workerThread, 1000);
        CloseHandle(g_workerThread);
        g_workerThread = NULL;
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
            hBrushMouseRight = CreateSolidBrush(MOUSE_RIGHT_BG_COLOR);
            hBrushMouseLeft = CreateSolidBrush(MOUSE_LEFT_BG_COLOR);
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
                // Step adjustment: the 72px track is ~2.3ms per pixel, so the
                // wheel is the only way to walk the step grid one value at a
                // time (a drag can skip a step between two pixels).
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

                if (GRID_SLOTS[idx].kind == GridSlot::Star) {
                    // Chain mode: one held key fires every selected key.
                    g_chainMode = !g_chainMode;
                } else if (GRID_SLOTS[idx].kind == GridSlot::Mouse) {
                    // Three-state cycle: off -> right -> left -> off.
                    int mode = g_mouseMode + 1;
                    if (mode >= MOUSE_MODE_COUNT) mode = MOUSE_MODE_OFF;
                    g_mouseMode = mode;
                    // The held flag belongs to the mode that is going away.
                    g_mousePressed = false;
                } else {
                    KeyEntry& key = g_keys[GRID_SLOTS[idx].keyIndex];
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
            if (hBrushMouseRight) { DeleteObject(hBrushMouseRight); hBrushMouseRight = NULL; }
            if (hBrushMouseLeft) { DeleteObject(hBrushMouseLeft); hBrushMouseLeft = NULL; }

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

// Mouse twin of LowLevelKeyboardProc, delivered to the SAME thread, so the same
// rules apply: flags only, no sending, no UI, no Sleep, no file I/O.
// This callback is on an even hotter path than the keyboard one - WM_MOUSEMOVE
// arrives hundreds of times a second - so the very first thing it does is bail
// out on anything that is not one of the four button messages.
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (wParam != WM_LBUTTONDOWN && wParam != WM_LBUTTONUP &&
        wParam != WM_RBUTTONDOWN && wParam != WM_RBUTTONUP) {
        return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
    }

    if (nCode >= 0) {
        MSLLHOOKSTRUCT* pMouse = (MSLLHOOKSTRUCT*)lParam;
        const bool injected = (pMouse->flags & LLMHF_INJECTED) != 0;

        // "Is the cursor over P2?" is needed by both the injected and the
        // physical path, but only ever matters while a mode is armed - so the
        // GetWindowRect call is skipped entirely when the app is off or the M
        // cell is off. GetWindowRect is cheaper than WindowFromPoint and enough
        // here: the window is a borderless rectangle.
        bool overOwn = false;
        if (isEnabled && g_mouseMode != MOUSE_MODE_OFF) {
            RECT own;
            if (hMainWnd && GetWindowRect(hMainWnd, &own)) {
                overOwn = PointInOwnWindow(pMouse->pt, own);
            }
        }

        if (injected) {
            // Injected input normally passes through (our own repeat clicks and,
            // like the keyboard hook, anyone else's). The sole exception is a
            // click of ours landing on P2 itself - see ShouldSwallowInjectedClick.
            if (ShouldSwallowInjectedClick(g_mouseMode, isEnabled, overOwn)) {
                return 1;
            }
            return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
        }

        const bool isRight = (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP);
        const bool isDown = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN);
        const int button = isRight ? MOUSE_MODE_RIGHT : MOUSE_MODE_LEFT;

        if (isDown) {
            if (ShouldBlockMouseDown(g_mouseMode, isRight, isEnabled,
                                     g_lolForeground, overOwn)) {
                g_mousePressed = true;
                g_mouseBlockedDown = button;
                return 1;
            }
        } else {
            // Releasing the button that drives the current mode ends the hold.
            if (button == g_mouseMode) {
                g_mousePressed = false;
            }
            // Swallow the up ONLY for the button whose down we swallowed.
            if (g_mouseBlockedDown == button) {
                g_mouseBlockedDown = MOUSE_MODE_OFF;
                return 1;
            }
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
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
    // Mouse mode is 3 states, so it needs two bits of its own above chain mode.
    bits |= ((unsigned)(g_mouseMode & 3) << (KEY_COUNT + 2));

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

// Called ONLY from the worker thread, exactly like SendKey. No MOUSEEVENTF_MOVE
// and no coordinates, so the click lands wherever the cursor already is and the
// cursor is never moved. Touches no GDI object and no HWND: the worker outlives
// the brushes and fonts freed in WM_DESTROY.
void SendMouseClick(int mode) {
    if (mode == MOUSE_MODE_RIGHT) {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    } else if (mode == MOUSE_MODE_LEFT) {
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    }
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
                // One read of the hold state per cycle, then fire the whole
                // chain. A held mouse button counts as a trigger too, so the
                // chain can be driven from the mouse alone.
                if (AnyKeyActive() || MouseActive()) {
                    for (int i = 0; i < KEY_COUNT; i++) {
                        if (g_keys[i].selected) {
                            SendKey(g_keys[i].vk);
                            sentAny = true;
                        }
                    }
                    // The click closes the burst, after every selected key.
                    const int mode = g_mouseMode;
                    if (mode != MOUSE_MODE_OFF) {
                        SendMouseClick(mode);
                        sentAny = true;
                    }
                }
            } else {
                for (int i = 0; i < KEY_COUNT; i++) {
                    if (g_keys[i].selected && g_keys[i].pressed) {
                        SendKey(g_keys[i].vk);
                        sentAny = true;
                    }
                }
                // Standalone: only while its own button is physically held.
                if (MouseActive()) {
                    SendMouseClick(g_mouseMode);
                    sentAny = true;
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
                // Stop on release (key or mouse button)
                if (!isEnabled || (!AnyKeyActive() && !MouseActive())) break;
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

    // Check if the new foreground window is League of Legends. The result is
    // also cached for the mouse hook's foreground gate (g_lolForeground) - it
    // must be refreshed on EVERY change, not only when isEnabled flips, or a
    // manual F2/F3 would leave the gate stale.
    const bool isLoL = IsLeagueOfLegendsWindow(hwnd);
    g_lolForeground = isLoL;

    if (isLoL) {
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
