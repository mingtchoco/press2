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
// and the slot index is NOT the g_keys index any more: row 3 ends with the
// chain-mode star and row 4 ends with the two mouse-button cells. GRID_SLOTS
// below is the single source of that mapping - the painter, the hit test and
// the click handler all branch on it.
constexpr int KEY_COUNT = 13;
constexpr int KEY_COLS = 4;
constexpr int KEY_ROWS = 4;
constexpr int KEY_CELL_W = 18;
constexpr int KEY_CELL_H = 20;
// 16 slots: 13 keys + star + left mouse + right mouse
constexpr int GRID_CELL_COUNT = KEY_COLS * KEY_ROWS;
constexpr int KEY_ROW_W = KEY_COLS * KEY_CELL_W;
constexpr int KEY_GRID_H = KEY_ROWS * KEY_CELL_H;

// What a grid slot does. keyIndex is the g_keys index for Key slots and -1
// otherwise. The two mouse buttons are separate kinds (not one cell shared
// between them) because each is an independent cell, exactly like a key.
struct GridSlot {
    enum Kind { Key, Star, MouseLeft, MouseRight };
    Kind kind;
    int keyIndex;
};

// Layout:  Q W E R  /  1 2 3 4  /  5 6 7 *  /  D F L R
constexpr GridSlot GRID_SLOTS[GRID_CELL_COUNT] = {
    { GridSlot::Key, 0 },  { GridSlot::Key, 1 },  { GridSlot::Key, 2 },  { GridSlot::Key, 3 },
    { GridSlot::Key, 4 },  { GridSlot::Key, 5 },  { GridSlot::Key, 6 },  { GridSlot::Key, 7 },
    { GridSlot::Key, 8 },  { GridSlot::Key, 9 },  { GridSlot::Key, 10 }, { GridSlot::Star, -1 },
    { GridSlot::Key, 11 }, { GridSlot::Key, 12 }, { GridSlot::MouseLeft, -1 }, { GridSlot::MouseRight, -1 },
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
static_assert(CountSlotKind(GridSlot::MouseLeft) == 1, "exactly one left-click slot");
static_assert(CountSlotKind(GridSlot::MouseRight) == 1, "exactly one right-click slot");
// The UpdateUI guard packs TWO bits per cell (every cell is a 3-state mode),
// so the width check lives next to the CELL_* constants below - MOUSE_COUNT is
// not declared yet at this point.

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
// never used and no waitable timer is involved. What changed on 2026-11-11 is
// how the loop decides it has waited long enough: it measures ELAPSED TIME with
// QueryPerformanceCounter instead of COUNTING Sleep(1) calls.
//
// Why: counting calls silently hard-codes the timer quantum. The old code
// converted the ms setting into a number of Sleep(1) calls assuming one call
// costs ~15.5ms. On this PC the system timer resolution is now 0.977ms (another
// process raises it and Windows no longer isolates that from us - measured:
// NtQueryTimerResolution current = 0.977ms, Sleep(1) avg 1.83ms in a process
// that never calls timeBeginPeriod), so the same 10 calls that used to be 155ms
// became ~18ms and a 150ms setting repeated at near-zero delay. Measuring the
// elapsed time makes the period correct at ANY resolution; the only thing the
// quantum still decides is how finely the loop can stop (and how often it
// wakes), not how long it waits.
//
// Why not a QPC spin: the loop only READS the clock between Sleep(1) calls, it
// never busy-waits on it. A hybrid Sleep+QPC spin was measured on 2026-08-03 to
// burn a whole core at 97-100% and is a rejected approach for a game utility.
// timeBeginPeriod and a waitable timer are rejected too (see CLAUDE.md).
//
// Why the wait sleeps in SLICES of up to MAX_WAIT_SLICE_MS rather than Sleep(1)
// every time: with the quantum at ~1ms a Sleep(1) loop wakes the worker ~1000
// times a second, 15x what it used to, and this repository has a history of
// losing in-game frames to scheduler pressure (2026-04-08). Asking for the
// whole remaining time in one Sleep, capped at 8ms, keeps the wake count down
// without changing anything else:
//   - at the coarse default (~15.6ms) Sleep(1) and Sleep(8) cost the SAME one
//     quantum, so behaviour, wake rate (~64/s) and the stop-on-release latency
//     (<= one quantum) are all exactly what they were;
//   - at this machine's measured ~1.9ms quantum the wake rate drops from
//     ~521/s to ~130/s, and the release latency becomes at most 8ms, which is
//     BETTER than the ~16ms the coarse quantum used to impose.
// 8 is the cap because it is the largest slice that still rounds down to a
// single quantum at the coarse default (15.6 > 8) while keeping the release
// latency under half of that old ~16ms worst case.
//
// The slice is NOT the remaining time. It is MAX_WAIT_SLICE_MS while the
// deadline is more than TWO slices away, and exactly 1 after that. The reason
// is the half-quantum correction in ShouldStopWaiting, which is handed
// `now - prev`, the measured length of the sleep that just ended. That value is
// only the real timer quantum if the sleep asked for 1ms; a long slice reports
// its own length instead, and crediting half of a long slice when the next
// sleep would have been short makes the loop stop early - measured at -1.2ms
// on a 150ms period when the slice was the remaining time.
// Switching to Sleep(1) two slices out removes that case by construction:
// stopping right after a Sleep(MAX_WAIT_SLICE_MS) would need the remainder to
// be <= MAX_WAIT_SLICE_MS/2, but that sleep only happens while the remainder is
// above 2 * MAX_WAIT_SLICE_MS, so the stop decision always lands just after a
// Sleep(1) and lastSleepMs is always the true quantum. At the coarse default
// every sleep costs one quantum anyway, so the behaviour there is identical to
// the plain Sleep(1) loop.
constexpr int MAX_WAIT_SLICE_MS = 8;

// The user-facing value is the repeat period in milliseconds (g_intervalMs,
// driven by the slider). It is quantised to INTERVAL_STEP_MS, so the value set
// is 35, 40, ... 200 - evenly spaced, no special case at either end.
constexpr int MIN_INTERVAL_MS = 35;
constexpr int MAX_INTERVAL_MS = 200;
constexpr int INTERVAL_STEP_MS = 5;        // slider/wheel granularity
constexpr int DEFAULT_INTERVAL_MS = 150;   // on the step grid
constexpr int JITTER_MS = 6;               // +/- randomisation per send cycle

// Mouse click repeat: the L and R grid cells are two INDEPENDENT cells, not one
// cell cycling between the buttons, so both can be armed and held at the same
// time. Index into g_mouse below; the ini holds one value per button.
constexpr int MOUSE_LEFT = 0;
constexpr int MOUSE_RIGHT = 1;
constexpr int MOUSE_COUNT = 2;

// Every toggle cell (each key and each mouse button) is a 3-state mode, cycled
// by a click: OFF -> SOLO -> CHAIN -> OFF.
//   SOLO  - the cell fires on its OWN hold only.
//   CHAIN - while the star is on, holding ANY chain cell fires EVERY chain cell
//           in the same cycle; with the star off a chain cell behaves like solo.
// "Armed" (the old boolean "selected") is simply mode != CELL_OFF: the hooks,
// the keyup symmetry and the ini all use that meaning.
// All 15 cells (13 keys + the two mouse buttons) paint the SAME colour per
// mode - grey / green / red - so the colour alone identifies the mode; see the
// colour block below and DrawKeyGrid.
constexpr int CELL_OFF = 0;
constexpr int CELL_SOLO = 1;
constexpr int CELL_CHAIN = 2;
constexpr int CELL_MODE_COUNT = 3;

// UpdateUI packs isEnabled into bit 0, then TWO bits per key, then chain mode,
// then two bits per mouse button - 32 bits at the current counts, so the guard
// is an unsigned long long and this is the width that must hold.
static_assert(1 + 2 * KEY_COUNT + 1 + 2 * MOUSE_COUNT <= 64, "UpdateUI guard bits");

// F4 one-shot: pressing F4 while the app is ON types this string ONCE (it is
// not a repeat cell and it has no grid cell, no colour and no ini entry).
// Written with escapes on purpose so the literal does not depend on the source
// file's encoding: C, U+039F GREEK CAPITAL LETTER OMICRON, O, N. The omicron
// looks like a latin O but is a different codepoint - that is the point of the
// string, so do NOT "fix" it to L"COON".
constexpr const wchar_t F4_TEXT[] = L"C\x039FON";
// Two INPUT records per character (down + up). 16 leaves room for a longer
// string without touching BuildUnicodeTypeInputs, which refuses to write a
// partial run if the buffer is ever too small.
constexpr int MAX_TYPE_INPUTS = 16;

// League of Legends process detection
constexpr wchar_t LOL_PROCESS_1[] = L"League of Legends.exe";
constexpr wchar_t LOL_PROCESS_2[] = L"LeagueClient.exe";
constexpr wchar_t LOL_WINDOW_TITLE[] = L"League of Legends (TM) Client";

// Colors
constexpr COLORREF ENABLED_BG_COLOR = RGB(240, 255, 240);   // Soft mint
constexpr COLORREF DISABLED_BG_COLOR = RGB(248, 248, 248);  // Light gray
constexpr COLORREF ENABLED_TEXT_COLOR = RGB(34, 139, 34);   // Forest green
constexpr COLORREF DISABLED_TEXT_COLOR = RGB(128, 128, 128); // Gray
// The 15 toggle cells (13 keys + L + R) all share ONE three-colour scheme, so
// the background alone says which mode a cell is in: grey OFF, green SOLO, red
// CHAIN. Per-cell accent colours are deliberately gone - the ultimate key had
// its own orange and the mouse cells their own blue/red, which at 18px left no
// way to also show three modes. All 15 cells are plain filled rectangles, so
// the mouse L/R cells are told apart from the keys only by their POSITION in
// the grid (the bottom row) - there is no per-kind colour and no per-kind
// shape any more (see DrawKeyGrid).
constexpr COLORREF KEY_OFF_BG_COLOR = RGB(214, 214, 214);   // cell OFF
constexpr COLORREF KEY_ON_BG_COLOR = RGB(34, 139, 34);      // cell SOLO
constexpr COLORREF KEY_CHAIN_BG_COLOR = RGB(200, 40, 40);   // cell CHAIN
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
    volatile int mode;            // CELL_OFF / CELL_SOLO / CELL_CHAIN
    volatile bool pressed;        // physically held right now
    volatile bool weBlockedDown;  // we swallowed the keydown, so we must swallow the keyup
};

// E defaults to SOLO to preserve the original behaviour. R, the number row and
// the item keys default off on purpose. '1'..'7' are the main keyboard row, not
// the numpad.
KeyEntry g_keys[KEY_COUNT] = {
    { 'Q', L"Q", CELL_OFF,  false, false },
    { 'W', L"W", CELL_OFF,  false, false },
    { 'E', L"E", CELL_SOLO, false, false },
    { 'R', L"R", CELL_OFF,  false, false },
    { '1', L"1", CELL_OFF,  false, false },
    { '2', L"2", CELL_OFF,  false, false },
    { '3', L"3", CELL_OFF,  false, false },
    { '4', L"4", CELL_OFF,  false, false },
    { '5', L"5", CELL_OFF,  false, false },
    { '6', L"6", CELL_OFF,  false, false },
    { '7', L"7", CELL_OFF,  false, false },
    { 'D', L"D", CELL_OFF,  false, false },
    { 'F', L"F", CELL_OFF,  false, false },
};

// Per-mouse-button state, the exact twin of KeyEntry: same single-writer rule
// (main thread writes, worker only reads) and the same keyup symmetry via a
// per-button weBlockedDown. Both buttons can be armed and held at once, so
// pressed and weBlockedDown have to be PER BUTTON - a single shared flag would
// let one button's release swallow the other's up and leave it stuck down in
// the target app.
struct MouseEntry {
    DWORD downFlag;               // MOUSEEVENTF_* for the injected click
    DWORD upFlag;
    const wchar_t* label;         // text drawn in the grid cell ("L" / "R")
    volatile int mode;            // CELL_OFF / CELL_SOLO / CELL_CHAIN
    volatile bool pressed;        // physically held right now
    volatile bool weBlockedDown;  // we swallowed the down, so we owe the up
};

MouseEntry g_mouse[MOUSE_COUNT] = {
    { MOUSEEVENTF_LEFTDOWN,  MOUSEEVENTF_LEFTUP,  L"L", CELL_OFF, false, false },
    { MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, L"R", CELL_OFF, false, false },
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
HBRUSH hBrushKeyChain = NULL;   // cell in CELL_CHAIN (red)
HBRUSH hBrushKeyOff = NULL;
HBRUSH hBrushStarOn = NULL;
wchar_t g_iniPath[MAX_PATH] = {0};  // settings file, next to the exe

// Repeat period in ms and chain mode. Both are written ONLY from the main
// thread (WindowProc / WinMain); the worker thread only reads them.
volatile int g_intervalMs = DEFAULT_INTERVAL_MS;
volatile bool g_chainMode = false;

// F4 one-shot type requests. The keyboard hook (main thread) only ever
// INCREMENTS this; the worker keeps a stack-local copy of the value it last
// acted on and compares, so it still never writes a global - the single-writer
// invariant holds exactly as it does for every other cross-thread flag. A
// boolean the worker cleared after typing would break it.
// Edge-triggered: the worker reacts to "the counter changed", not to how much
// it changed, so several presses inside one poll collapse into one typing run.
volatile unsigned g_typeRequestSeq = 0;

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

// One cycle's worth of cell state, read off the globals ONCE at the top of a
// worker cycle. Passing a snapshot (instead of letting the policy read globals)
// is what makes the whole SOLO/CHAIN table unit testable outside the app, and it
// also means a mid-cycle toggle cannot change the policy half way through.
struct CycleSnapshot {
    int  keyMode[KEY_COUNT];
    bool keyPressed[KEY_COUNT];
    int  mouseMode[MOUSE_COUNT];
    bool mousePressed[MOUSE_COUNT];
    bool chainOn;                  // the star cell
};

// What that cycle sends: which keys, and which mouse buttons are CANDIDATES.
// Candidates are not clicks - PickMouseClick still reduces them to at most one
// click per cycle.
struct CyclePlan {
    bool sendKey[KEY_COUNT];
    bool mouseCand[MOUSE_COUNT];
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
void SendMouseClick(int index);
void ClearPressedKeys();
bool AnyKeyActive();
bool MouseActive();
bool AnyMouseSelected();
bool PointInOwnWindow(POINT pt, const RECT& r);
bool ShouldBlockMouseDown(bool selected, bool enabled,
                          bool lolForeground, bool overOwnWindow);
bool ShouldSwallowInjectedClick(bool anyMouseSelected, bool enabled, bool overOwnWindow);
int  PickMouseClick(bool candLeft, bool candRight, int& nextMouse);
int  NextCellMode(int mode);
int  ClampCellMode(int mode);
CyclePlan PlanCycle(const CycleSnapshot& s);
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
double IntervalToTargetMs(int intervalMs, double rndJitter);
bool ShouldStopWaiting(double elapsedMs, double lastSleepMs, double targetMs);

// Clear the physical-hold flags. Deliberately leaves weBlockedDown alone:
// if we already swallowed a keydown we still owe the target app its keyup.
void ClearPressedKeys() {
    for (int i = 0; i < KEY_COUNT; i++) {
        g_keys[i].pressed = false;
    }
    // Same reasoning for the mouse buttons: drop the physical hold, keep
    // weBlockedDown so a swallowed button-down still gets its up.
    for (int i = 0; i < MOUSE_COUNT; i++) {
        g_mouse[i].pressed = false;
    }
}

// "Armed and held": armed means mode != CELL_OFF, so SOLO and CHAIN count the
// same here. This is the worker's early-exit test, not its send policy.
bool AnyKeyActive() {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_keys[i].mode != CELL_OFF && g_keys[i].pressed) return true;
    }
    return false;
}

// The mouse twin of AnyKeyActive: some armed button is held right now.
bool MouseActive() {
    for (int i = 0; i < MOUSE_COUNT; i++) {
        if (g_mouse[i].mode != CELL_OFF && g_mouse[i].pressed) return true;
    }
    return false;
}

// Is either mouse cell armed at all? Used by the hook to decide whether the
// cursor-over-P2 test is worth doing, and by the injected-click gate.
bool AnyMouseSelected() {
    for (int i = 0; i < MOUSE_COUNT; i++) {
        if (g_mouse[i].mode != CELL_OFF) return true;
    }
    return false;
}

// Pure: does the keyboard hook consume this key as the F4 one-shot?
// All four conditions are required, and each one matters:
//   isDown   - only the keydown is taken, exactly like F2/F3 (the keyup is left
//              alone and reaches the system).
//   vk       - VK_F4 only.
//   flags    - LLKHF_ALTDOWN must be CLEAR. Swallowing Alt+F4 would kill
//              "close window" system wide, which is not ours to break.
//   enabled  - unlike F2/F3 this is NOT a global consume: with the app OFF the
//              F4 press passes through untouched to whatever has focus.
// Kept free of globals so the truth table can be checked outside the app.
bool ShouldConsumeF4(bool isDown, DWORD vk, DWORD flags, bool enabled) {
    if (!isDown) return false;
    if (vk != VK_F4) return false;
    if (!enabled) return false;
    if (flags & LLKHF_ALTDOWN) return false;
    return true;
}

// Pure: is pt inside the rect (right/bottom exclusive, same convention as the
// grid hit test)? Kept free of globals so it can be unit tested outside the app.
bool PointInOwnWindow(POINT pt, const RECT& r) {
    return (pt.x >= r.left && pt.x < r.right && pt.y >= r.top && pt.y < r.bottom);
}

// Pure: does the low-level mouse hook swallow this PHYSICAL button-down?
// "selected" is the state of the ONE button this message belongs to, so each
// button is gated by its own cell.
// The "over our own window" exception is mandatory: the mouse hook fires before
// the window is activated, so without it a right-click on P2 while LoL is the
// foreground window (R armed) would be eaten and the app could never be
// closed - and with L armed no grid cell could be clicked either.
// lolForeground is the second mandatory gate - see g_lolForeground.
bool ShouldBlockMouseDown(bool selected, bool enabled,
                          bool lolForeground, bool overOwnWindow) {
    if (!enabled) return false;
    if (!lolForeground) return false;
    if (!selected) return false;
    if (overOwnWindow) return false;
    return true;
}

// Pure: does the hook swallow an INJECTED button message?
// Normally injected input passes straight through (same scope as the keyboard
// hook). The one exception is our own worker's click landing on P2 itself: the
// cursor can be parked over the window, and a right click there would close the
// app on the next cycle while a left click would toggle a cell - and rewrite the
// ini - once per cycle. Deliberately NOT gated on lolForeground, because the
// worker only looks at isEnabled when it decides to fire.
// It is gated on "ANY button armed" rather than on the button of this message:
// the worker alternates between the two, so while either cell is on, a click of
// ours can land on P2 as either button.
// The gate stays as narrow as possible (our modes, enabled, over our rect) so
// other programs' injected clicks keep passing through everywhere else.
bool ShouldSwallowInjectedClick(bool anyMouseSelected, bool enabled, bool overOwnWindow) {
    return enabled && anyMouseSelected && overOwnWindow;
}

// Pure: which button does this cycle click? At most ONE click per cycle.
// With both buttons in the running the worker alternates R, L, R, L via its own
// stack-local nextMouse (kept out of the globals so the worker stays a
// read-only observer of them); it starts at MOUSE_RIGHT, so the first click of
// a both-candidate run is the RIGHT button. A cycle with a single candidate
// fires that one and leaves nextMouse untouched, so turning the second button
// on resumes the alternation instead of restarting it. -1 means "no click".
int PickMouseClick(bool candLeft, bool candRight, int& nextMouse) {
    if (candLeft && candRight) {
        const int pick = (nextMouse == MOUSE_RIGHT) ? MOUSE_RIGHT : MOUSE_LEFT;
        nextMouse = (pick == MOUSE_LEFT) ? MOUSE_RIGHT : MOUSE_LEFT;
        return pick;
    }
    if (candLeft) return MOUSE_LEFT;
    if (candRight) return MOUSE_RIGHT;
    return -1;
}

// Pure: the next mode a click on a cell selects (OFF -> SOLO -> CHAIN -> OFF).
// A value from outside the enum (hand-edited ini read before a click) lands on
// OFF rather than wrapping to something the painter does not know.
int NextCellMode(int mode) {
    if (mode < CELL_OFF || mode > CELL_CHAIN) return CELL_OFF;
    return (mode + 1) % CELL_MODE_COUNT;
}

// Pure: sanitise a mode loaded from the ini. Anything out of range is OFF.
int ClampCellMode(int mode) {
    if (mode < CELL_OFF || mode > CELL_CHAIN) return CELL_OFF;
    return mode;
}

// Pure: the whole send policy for one cycle. Takes a snapshot, returns the keys
// to send and the mouse candidates; reads no globals, so every row of the
// policy table can be checked outside the app.
//
// Star OFF: a cell fires on its own hold only - SOLO and CHAIN are identical.
// Star ON : the chain fires when ANY chain cell (key or mouse button) is held;
//           then every chain cell goes out, while SOLO cells still only fire on
//           their own hold. So a solo hold alone never pulls the chain, and a
//           chain hold alone never pulls a solo cell.
CyclePlan PlanCycle(const CycleSnapshot& s) {
    CyclePlan p = {};

    if (!s.chainOn) {
        for (int i = 0; i < KEY_COUNT; i++) {
            p.sendKey[i] = (s.keyMode[i] != CELL_OFF) && s.keyPressed[i];
        }
        for (int j = 0; j < MOUSE_COUNT; j++) {
            p.mouseCand[j] = (s.mouseMode[j] != CELL_OFF) && s.mousePressed[j];
        }
        return p;
    }

    bool chainTriggered = false;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (s.keyMode[i] == CELL_CHAIN && s.keyPressed[i]) chainTriggered = true;
    }
    for (int j = 0; j < MOUSE_COUNT; j++) {
        if (s.mouseMode[j] == CELL_CHAIN && s.mousePressed[j]) chainTriggered = true;
    }

    for (int i = 0; i < KEY_COUNT; i++) {
        p.sendKey[i] = (s.keyMode[i] == CELL_SOLO && s.keyPressed[i]) ||
                       (s.keyMode[i] == CELL_CHAIN && chainTriggered);
    }
    for (int j = 0; j < MOUSE_COUNT; j++) {
        p.mouseCand[j] = (s.mouseMode[j] == CELL_SOLO && s.mousePressed[j]) ||
                         (s.mouseMode[j] == CELL_CHAIN && chainTriggered);
    }
    return p;
}

// ini key name of a mouse button: "MouseL" / "MouseR". Derived from the grid
// label so the cell text and the ini entry stay a single source.
static void MouseIniKey(int index, wchar_t* buf, size_t cap) {
    if (cap < 8) {
        if (cap > 0) buf[0] = L'\0';
        return;
    }
    wsprintfW(buf, L"Mouse%s", g_mouse[index].label);
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
    // 0/1/2 == OFF/SOLO/CHAIN. An out-of-range hand-edited value becomes OFF,
    // and an ini written before the 3-state cells reads back as SOLO, which is
    // exactly what its "1" used to mean.
    for (int i = 0; i < KEY_COUNT; i++) {
        int v = (int)GetPrivateProfileIntW(L"Keys", g_keys[i].label,
                                           (UINT)g_keys[i].mode, g_iniPath);
        g_keys[i].mode = ClampCellMode(v);
    }

    // Interval is clamped AND snapped on load: a hand-edited or stale ini must
    // never put the worker outside the slider range or off the step grid.
    UINT ms = GetPrivateProfileIntW(L"Options", L"IntervalMs",
                                    DEFAULT_INTERVAL_MS, g_iniPath);
    if (ms > (UINT)MAX_INTERVAL_MS) ms = (UINT)MAX_INTERVAL_MS;
    g_intervalMs = SnapInterval((int)ms);

    g_chainMode = (GetPrivateProfileIntW(L"Options", L"Chain", 0, g_iniPath) != 0);

    // One entry per mouse button, both defaulting off, same 0/1/2 encoding as
    // the keys and the same clamp, so a hand-edited value can never produce a
    // state the painter and the worker do not know. The old single 3-state
    // "Mouse" entry (a different encoding entirely) is deliberately NOT read.
    for (int i = 0; i < MOUSE_COUNT; i++) {
        wchar_t name[16];
        MouseIniKey(i, name, 16);
        int v = (int)GetPrivateProfileIntW(L"Options", name, 0, g_iniPath);
        g_mouse[i].mode = ClampCellMode(v);
    }
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
        wchar_t modeBuf[8];
        wsprintfW(modeBuf, L"%d", g_keys[i].mode);
        AppendIniEntry(keys, 256, klen, g_keys[i].label, modeBuf);
    }
    keys[klen] = L'\0';     // section buffers end with a double NUL
    WritePrivateProfileSectionW(L"Keys", keys, g_iniPath);

    wchar_t msBuf[16];
    wsprintfW(msBuf, L"%d", g_intervalMs);
    wchar_t options[128];
    size_t olen = 0;
    AppendIniEntry(options, 128, olen, L"IntervalMs", msBuf);
    AppendIniEntry(options, 128, olen, L"Chain", g_chainMode ? L"1" : L"0");
    for (int i = 0; i < MOUSE_COUNT; i++) {
        wchar_t name[16];
        MouseIniKey(i, name, 16);
        wchar_t modeBuf[8];
        wsprintfW(modeBuf, L"%d", g_mouse[i].mode);
        AppendIniEntry(options, 128, olen, name, modeBuf);
    }
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

// Background brush and matching colour for a 3-state cell. Shared by the keys
// and both mouse cells so one mode can never look like two different things.
// The star is the only cell that does not use this (it is a 2-state gold
// toggle, not a mode cell).
HBRUSH CellModeBrush(int mode) {
    if (mode == CELL_CHAIN) return hBrushKeyChain;
    if (mode == CELL_SOLO) return hBrushKeyOn;
    return hBrushKeyOff;
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
        } else if (GRID_SLOTS[i].kind == GridSlot::MouseLeft ||
                   GRID_SLOTS[i].kind == GridSlot::MouseRight) {
            // Drawn exactly like a key cell: same three mode colours, same
            // font, same filled rectangle. The consequence is that the mouse R
            // cell and the ultimate key R one row up look IDENTICAL in the same
            // mode - the only cue left is the position (row 4 col 4 vs row 1
            // col 4). That is the requested behaviour, not an oversight.
            const int mi = (GRID_SLOTS[i].kind == GridSlot::MouseLeft)
                           ? MOUSE_LEFT : MOUSE_RIGHT;
            const int mode = g_mouse[mi].mode;
            text = g_mouse[mi].label;
            brush = CellModeBrush(mode);
            if (mode != CELL_OFF) fg = KEY_ON_TEXT_COLOR;
        } else {
            const KeyEntry& key = g_keys[GRID_SLOTS[i].keyIndex];
            const int mode = key.mode;
            text = key.label;
            brush = CellModeBrush(mode);
            if (mode != CELL_OFF) fg = KEY_ON_TEXT_COLOR;
        }

        // Every cell is a filled rectangle - no pen and no brush is selected
        // into the DC, so there is nothing to restore and no GDI object to leak.
        if (brush) {
            FillRect(hdc, &cell, brush);
        }

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

// Pure helper: repeat period in ms -> the wait target for this cycle, in ms.
// rndJitter is uniform in [0, 1): it jitters the target by +/- JITTER_MS and
// the result is clamped to the slider range. No timer quantum appears here at
// all - the target is a time, and the wait loop below measures time.
// At the two ends of the range the clamp is one-sided, so the mean is pulled
// inwards: a setting of 35 averages ~36.5ms and 200 averages ~198.5ms. That is
// intentional - the alternative is letting the jitter push the period below the
// floor the user asked for. Away from the ends the mean matches the setting
// exactly (the default 150 averages 150).
// Kept free of globals so it can be unit tested outside the app.
double IntervalToTargetMs(int intervalMs, double rndJitter) {
    double target = (double)intervalMs + (rndJitter * 2.0 - 1.0) * (double)JITTER_MS;
    if (target < (double)MIN_INTERVAL_MS) target = (double)MIN_INTERVAL_MS;
    if (target > (double)MAX_INTERVAL_MS) target = (double)MAX_INTERVAL_MS;
    return target;
}

// Pure helper: has the wait loop waited long enough?
// elapsedMs    - time since the cycle's sends finished
// lastSleepMs  - how long the Sleep(1) we just returned from actually took,
//                i.e. the timer quantum as measured on this machine right now
// targetMs     - what IntervalToTargetMs asked for
//
// Why the half-quantum: the loop can only stop on a quantum boundary, so a
// plain "elapsed >= target" always overshoots by up to a whole quantum and the
// average period comes out target + quantum/2 (that was +7.8ms at a 15.5ms
// quantum). Adding half of the measured quantum before comparing stops at the
// NEAREST boundary instead of the next one, which leaves the error centred on
// zero and uniform in +/- quantum/2. This is what the old stochastic rounding
// (dithering) against a hard-coded 15.5ms quantum used to buy, except it no
// longer assumes any particular quantum: it works the same at 15.6ms and 1ms.
// Kept free of globals so it can be unit tested outside the app.
bool ShouldStopWaiting(double elapsedMs, double lastSleepMs, double targetMs) {
    return elapsedMs + lastSleepMs * 0.5 >= targetMs;
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
            hBrushKeyChain = CreateSolidBrush(KEY_CHAIN_BG_COLOR);
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
                    // Star: while on, one held CHAIN cell fires every CHAIN cell.
                    g_chainMode = !g_chainMode;
                } else if (GRID_SLOTS[idx].kind == GridSlot::MouseLeft ||
                           GRID_SLOTS[idx].kind == GridSlot::MouseRight) {
                    // OFF -> SOLO -> CHAIN -> OFF, exactly like a key cell.
                    MouseEntry& m = g_mouse[
                        (GRID_SLOTS[idx].kind == GridSlot::MouseLeft)
                        ? MOUSE_LEFT : MOUSE_RIGHT];
                    m.mode = NextCellMode(m.mode);
                    if (m.mode == CELL_OFF) {
                        // Dropping the hold flag of a cell being turned off:
                        // leaving it set would look held without a press.
                        m.pressed = false;
                    }
                } else {
                    KeyEntry& key = g_keys[GRID_SLOTS[idx].keyIndex];
                    key.mode = NextCellMode(key.mode);
                    if (key.mode == CELL_OFF) {
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
            if (hBrushKeyChain) { DeleteObject(hBrushKeyChain); hBrushKeyChain = NULL; }
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

        // F4 one-shot: ask the worker to type F4_TEXT once. Flag only, like
        // every other branch here - the sending itself must stay in the worker.
        // The predicate is pure (ShouldConsumeF4) and gated on isEnabled and on
        // Alt being up, so Alt+F4 and every F4 press while the app is OFF fall
        // through to the system untouched. Only the keydown is swallowed; the
        // keyup is left alone, same as F2/F3. Nothing to update on screen and
        // nothing to persist, so no WM_APP + 1 and no CommitSettings here.
        if (ShouldConsumeF4(isDown, pKeyboard->vkCode, pKeyboard->flags, isEnabled)) {
            g_typeRequestSeq++;      // main thread is the only writer
            return 1;
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
            if (isDown && isEnabled && key.mode != CELL_OFF) {
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
        // physical path, but only ever matters while a button is armed - so the
        // GetWindowRect call is skipped entirely when the app is off or both
        // mouse cells are off. GetWindowRect is cheaper than WindowFromPoint
        // and enough here: the window is a borderless rectangle.
        const bool anyMouseSel = AnyMouseSelected();
        bool overOwn = false;
        if (isEnabled && anyMouseSel) {
            RECT own;
            if (hMainWnd && GetWindowRect(hMainWnd, &own)) {
                overOwn = PointInOwnWindow(pMouse->pt, own);
            }
        }

        if (injected) {
            // Injected input normally passes through (our own repeat clicks and,
            // like the keyboard hook, anyone else's). The sole exception is a
            // click of ours landing on P2 itself - see ShouldSwallowInjectedClick.
            if (ShouldSwallowInjectedClick(anyMouseSel, isEnabled, overOwn)) {
                return 1;
            }
            return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
        }

        const bool isRight = (wParam == WM_RBUTTONDOWN || wParam == WM_RBUTTONUP);
        const bool isDown = (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN);
        MouseEntry& m = g_mouse[isRight ? MOUSE_RIGHT : MOUSE_LEFT];

        if (isDown) {
            if (ShouldBlockMouseDown(m.mode != CELL_OFF, isEnabled,
                                     g_lolForeground, overOwn)) {
                m.pressed = true;
                m.weBlockedDown = true;
                return 1;
            }
        } else {
            m.pressed = false;
            // Swallow the up ONLY if we swallowed this same button's down.
            // Per-button state is what makes holding both buttons safe: the
            // other button's up is never touched here.
            if (m.weBlockedDown) {
                m.weBlockedDown = false;
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
    // The guard must cover EVERY value the window shows - master state, every
    // cell MODE (two bits each, since OFF/SOLO/CHAIN look different), chain
    // mode and the interval readout - or a change to one of them would be
    // silently swallowed.
    // Starts at an impossible value so the initial paint always happens.
    static unsigned long long lastBits = ~0ull;
    static int lastInterval = -1;

    // bit 0: isEnabled | key i: bits 1+2i, 2+2i | star: bit 1+2*KEY_COUNT |
    // mouse j: the two bits above that. Non-overlapping by construction, and
    // the total width is held to 64 by the static_assert at the CELL_* block.
    unsigned long long bits = isEnabled ? 1ull : 0ull;
    for (int i = 0; i < KEY_COUNT; i++) {
        bits |= ((unsigned long long)(g_keys[i].mode & 3)) << (1 + 2 * i);
    }
    if (g_chainMode) bits |= (1ull << (1 + 2 * KEY_COUNT));
    for (int i = 0; i < MOUSE_COUNT; i++) {
        bits |= ((unsigned long long)(g_mouse[i].mode & 3))
                << (1 + 2 * KEY_COUNT + 1 + 2 * i);
    }

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
void SendMouseClick(int index) {
    if (index < 0 || index >= MOUSE_COUNT) return;
    mouse_event(g_mouse[index].downFlag, 0, 0, 0, 0);
    mouse_event(g_mouse[index].upFlag, 0, 0, 0, 0);
}

// Pure: fill out[] with the keyboard INPUT records that type `text`, two per
// character (down, then up). KEYEVENTF_UNICODE with wVk = 0 and the codepoint
// in wScan is the only way to send a character that has no virtual key, which
// is exactly the case for U+039F in F4_TEXT - MapVirtualKey/keybd_event cannot
// express it at all.
// Returns the number of records written. It returns 0 and writes NOTHING when
// there is nothing to send or the buffer is too small: all-or-nothing, so a
// caller can never emit half a string.
int BuildUnicodeTypeInputs(const wchar_t* text, INPUT* out, int cap) {
    if (!text || !out || cap <= 0) return 0;
    int len = 0;
    while (text[len] != L'\0') len++;
    const int need = len * 2;
    if (need == 0 || need > cap) return 0;
    for (int i = 0; i < len; i++) {
        for (int half = 0; half < 2; half++) {
            INPUT& rec = out[i * 2 + half];
            rec = INPUT{};
            rec.type = INPUT_KEYBOARD;
            rec.ki.wVk = 0;                      // unicode records carry no VK
            rec.ki.wScan = (WORD)text[i];        // the codepoint itself
            rec.ki.dwFlags = KEYEVENTF_UNICODE |
                             (half ? KEYEVENTF_KEYUP : 0u);
        }
    }
    return need;
}

// Type a string ONCE. Called ONLY from the worker thread, exactly like SendKey
// and SendMouseClick - never from a hook callback.
// SendInput is listed in CLAUDE.md as an abandoned approach, but that verdict
// is about calling it from INSIDE the hook callback, where the injected input
// was simply ignored; from the worker thread the premise is different and this
// is the only call that can carry a character without a virtual key. The key
// REPEAT path is untouched and still uses keybd_event (SendKey).
// The injected characters come back into LowLevelKeyboardProc as VK_PACKET with
// LLKHF_INJECTED, which the very top of that callback already passes straight
// through, so no extra hook handling is needed.
// Touches no GDI object and no HWND: the worker outlives both.
void TypeText(const wchar_t* text) {
    INPUT records[MAX_TYPE_INPUTS];
    const int n = BuildUnicodeTypeInputs(text, records, MAX_TYPE_INPUTS);
    if (n > 0) SendInput((UINT)n, records, sizeof(INPUT));
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

// Worker thread: snapshots the per-cell flags, asks PlanCycle what to send,
// sends it, then waits out the (jittered) repeat interval as a run of Sleep(1)
// calls.
//
// The policy itself lives in PlanCycle (pure, unit tested). In short:
// star OFF - every armed cell fires on its own hold, SOLO and CHAIN alike, so
// holding two keys sends both at the full rate rather than halving each.
// star ON  - holding any CHAIN cell (key or mouse button) fires every CHAIN
// cell back to back inside the same cycle, a burst and not a spread, while SOLO
// cells keep firing only on their own hold. The cycle rate is unchanged; what
// grows is the per-cycle injection count, which scales with the number of CHAIN
// cells rather than held ones. The hook needs no change for any of this: only
// the physically pressed key is swallowed and the rest go out as injected input.
//
// Mouse clicks: AT MOST ONE click per cycle in either mode, so arming the
// second button does not double the per-cycle injection count. With both
// buttons in the running the worker alternates R, L, R, L (PickMouseClick plus
// the stack-local nextMouse below). The click always closes the cycle, after
// every key that cycle sends.
//
// This thread is COMPLETELY decoupled from the hook callback, so hook chain
// re-entry (caused by keybd_event calls) does not freeze the input pipeline.
// It only ever READS the globals; all writes happen on the main thread.
DWORD WINAPI WorkerThreadProc(LPVOID) {
    unsigned rngState = (unsigned)GetTickCount() ^ (unsigned)GetCurrentThreadId();
    if (rngState == 0) rngState = 0x9E3779B9u;   // xorshift must never be seeded 0

    // QPC ticks per second, read once. Stack local for the same reason as
    // nextMouse below: the worker must not write to any global. QPC lives in
    // KERNEL32, which the exe already imports, so no new DLL dependency.
    LARGE_INTEGER qpcFreqRaw;
    qpcFreqRaw.QuadPart = 0;
    QueryPerformanceFrequency(&qpcFreqRaw);
    const double qpcFreq = (qpcFreqRaw.QuadPart > 0) ? (double)qpcFreqRaw.QuadPart
                                                     : 10000000.0;

    // Which button gets the next click when both are in the running; the right
    // button goes first. Stack local on purpose: a global would make the worker
    // a writer of shared state and break the single-writer invariant.
    int nextMouse = MOUSE_RIGHT;

    // Last F4 type request this thread has acted on. Seeded from the current
    // value so a counter that somehow already moved before the thread started
    // is not replayed as a spurious first type. Stack local for the same reason
    // as nextMouse: the worker must not write to any global.
    unsigned seenTypeSeq = g_typeRequestSeq;

    while (!g_exitThread) {
        // One-shot type request (F4). Deliberately NOT gated on isEnabled: the
        // hook only raises the counter while the app is ON, so a pending
        // request is one the user really made, and dropping it because an F3
        // landed between the press and this poll would be a silent loss. The
        // comparison is "did it change", not "how much", so a burst of presses
        // inside one poll types once.
        if (g_typeRequestSeq != seenTypeSeq) {
            seenTypeSeq = g_typeRequestSeq;
            TypeText(F4_TEXT);
        }

        bool sentAny = false;
        if (isEnabled) {
            // One read of the shared state per cycle, then a pure decision.
            CycleSnapshot snap;
            for (int i = 0; i < KEY_COUNT; i++) {
                snap.keyMode[i] = g_keys[i].mode;
                snap.keyPressed[i] = g_keys[i].pressed;
            }
            for (int j = 0; j < MOUSE_COUNT; j++) {
                snap.mouseMode[j] = g_mouse[j].mode;
                snap.mousePressed[j] = g_mouse[j].pressed;
            }
            snap.chainOn = g_chainMode;

            const CyclePlan plan = PlanCycle(snap);

            // Keys in array order (Q W E R 1..7 D F), then at most one click.
            for (int i = 0; i < KEY_COUNT; i++) {
                if (plan.sendKey[i]) {
                    SendKey(g_keys[i].vk);
                    sentAny = true;
                }
            }
            const int click = PickMouseClick(plan.mouseCand[MOUSE_LEFT],
                                             plan.mouseCand[MOUSE_RIGHT],
                                             nextMouse);
            if (click >= 0) {
                SendMouseClick(click);
                sentAny = true;
            }
        }

        if (sentAny) {
            // Wait out the interval. The loop sleeps in Sleep(1) steps as it
            // always has, but it decides when to stop from the MEASURED elapsed
            // time, not from a count of Sleep calls - counting calls bakes in a
            // timer quantum, and this machine's quantum changed from ~15.5ms to
            // ~1.8ms underneath us (see the "Repeat timing" comment). QPC is
            // only read between sleeps; nothing spins on it.
            //
            // At least one Sleep always happens (do/while) and the slice is
            // never below 1ms: that is the old minimum-wait guard in its new
            // form, the floor that keeps a cycle from degenerating into an
            // unbounded send loop even if the target were somehow zero. A
            // slice of 0 would be Sleep(0), which yields instead of waiting -
            // a spin, which this project rejects outright.
            //
            // Re-checking the flags after each slice stops within one slice
            // (<= MAX_WAIT_SLICE_MS, and <= one quantum when the quantum is
            // coarser than that) of the key being released, instead of sitting
            // out the whole remaining interval.
            const double targetMs = IntervalToTargetMs(g_intervalMs, Rnd01(rngState));
            LARGE_INTEGER start, prev, now;
            QueryPerformanceCounter(&start);
            prev = start;
            now = start;
            do {
                // Coarse approach, fine landing: sleep in MAX_WAIT_SLICE_MS
                // chunks while the deadline is more than two chunks away, then
                // switch to Sleep(1) for the rest. See the "Repeat timing"
                // comment for why the switch point is 2 * MAX_WAIT_SLICE_MS and
                // not the remaining time itself.
                const double doneMs =
                    (double)(now.QuadPart - start.QuadPart) * 1000.0 / qpcFreq;
                const double remainMs = targetMs - doneMs;
                const int slice = (remainMs > 2.0 * (double)MAX_WAIT_SLICE_MS)
                                      ? MAX_WAIT_SLICE_MS : 1;

                // slice is 1 or MAX_WAIT_SLICE_MS by construction, so it can
                // never be <= 0 here. Keep it that way: Sleep takes a DWORD, so
                // a negative slice would not spin, it would wrap to ~4.29e9 ms
                // and park the worker for 49.7 DAYS. Any future change to the
                // line above has to preserve slice >= 1.
                Sleep((DWORD)slice);
                QueryPerformanceCounter(&now);
                if (g_exitThread) break;
                // Stop on release (key or mouse button)
                if (!isEnabled || (!AnyKeyActive() && !MouseActive())) break;
                const double elapsedMs =
                    (double)(now.QuadPart - start.QuadPart) * 1000.0 / qpcFreq;
                const double lastSleepMs =
                    (double)(now.QuadPart - prev.QuadPart) * 1000.0 / qpcFreq;
                if (ShouldStopWaiting(elapsedMs, lastSleepMs, targetMs)) break;
                prev = now;
            } while (true);
        } else {
            // Idle poll: one timer quantum in practice (2-16ms depending on the
            // system timer resolution - Sleep(1) and Sleep(2) measure the same
            // at the coarse default).
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
