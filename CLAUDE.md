# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 필수 규칙
1. **에러 발생 시** 이 파일의 "에러 로그" 섹션에 기록 (원인, 해결책, 날짜)
2. **새로운 패턴 발견 시** "패턴 기록" 섹션에 문서화
3. **세션 종료 전** "학습 사항" 섹션에 정리

---

## 프로젝트 개요
- **이름**: Press2 (AutoKey E) — 창 클래스명/타이틀은 모두 `P2`
- **목적**: League of Legends 스킬/아이템 키 자동 반복 입력 유틸리티
- **언어**: C++17 / Win32 API, 로직 전체가 단일 소스 파일 (`autokey_e.cpp`, 1137줄)
- **출력**: `autokey_e.exe` (git에 추적됨 — "빌드 산출물 커밋" 참조)

### 동작
- **Q/W/E/R/1/2/3 중 선택한 키**를 누르고 있는 동안 worker 스레드가 그 키를 반복 전송 (원본 키는 hook에서 차단). 여러 키를 동시에 누르면 각각 **전부 같은 주기로** 나간다(분할 아님). `1`/`2`/`3`은 메인 키보드 상단 숫자키이며 넘패드가 아니다(autokey_e.cpp:138-146)
- 기본 선택은 E 하나. R과 1/2/3은 기본 꺼짐이고, R은 쿨다운이 도는 순간 궁극기가 나가버리므로 켜면 다른 키와 구분되는 강조색으로 표시된다(`isUltimate`, autokey_e.cpp:142)
- **반복 주기는 창의 속도 슬라이더로 31~200ms에서 조절**한다(기본 115ms). 과거의 `E_KEY_INTERVAL` 상수는 제거됐다 — 수치와 지터는 "타이밍" 절을 먼저 읽을 것
- **★(연동) 모드**: 켜면 선택 키 중 하나만 눌러도 선택된 키 전부가 매 사이클 연속 전송된다(`g_chainMode`, autokey_e.cpp:1016-1025). 끄면 실제로 누른 키만 나간다
- F2: 활성화 / F3: 비활성화. 두 키는 **무조건·전역으로 소비**되어 다른 앱에 전달되지 않는다 ("전역 키 소비 범위" 참조)
- LoL 포그라운드 진입 시 자동 ON, 이탈 시 자동 OFF. **F2/F3 수동 토글은 다음 포그라운드 전환까지만 유지된다** (아래 "함정" 참조)
- 항상 위(topmost) 소형 창, 가장 왼쪽 모니터 좌상단, 작업표시줄 미표시. **제목표시줄이 없다** — 2행 그리드(`Q W E R` / `1 2 3 ★`) 칸을 클릭해 토글하고, 그 아래 슬라이더를 클릭·드래그하거나 휠로 주기를 바꾸며, 그 밖의 영역을 좌클릭 드래그해 창을 옮기고 **우클릭으로 종료**한다(슬라이더 드래그 중 우클릭은 종료가 아니라 드래그 취소, autokey_e.cpp:821-831)
- **휠 조절은 인게임 중에 불가능하다**: 휠 메시지는 포커스 창에만 가므로 P2를 클릭해야 하고, 그러면 포그라운드가 LoL을 벗어나 `WinEventProc`가 자동 OFF시킨다(기존 설계). 주기는 게임에 들어가기 전에 맞춰 둘 것

### 키 선택과 설정 파일
- 상태는 `g_keys[KEY_COUNT]`(`KeyEntry` 배열, autokey_e.cpp:138-146, 현재 `KEY_COUNT = 7`) 하나에 모여 있다. 키를 추가하려면 이 배열에 항목을 넣고 `KEY_COUNT`를 늘리면 되며, hook·worker·그리기 모두 배열을 순회하므로 다른 곳은 손댈 필요가 없다. 단 **그리드는 `KEY_COLS x KEY_ROWS` 고정 슬롯(현재 4x2 = 8)이고 마지막 슬롯은 ★ 버튼**이므로, 키를 늘리면 `KEY_COLS`/`KEY_ROWS`도 함께 늘려야 한다. `static_assert(KEY_COUNT == STAR_CELL)`(autokey_e.cpp:60)가 이 관계를 컴파일 타임에 지키고, `static_assert(KEY_COUNT < 31)`(autokey_e.cpp:63)은 `UpdateUI` 가드 비트가 넘치지 않음을 보장한다
- 설정은 exe 옆 `autokey_e.ini`에 저장되고 시작 시 창 생성 전에 읽는다(`LoadSettings`, autokey_e.cpp:246-263). 섹션은 두 개다 — `[Keys]`: `Q`/`W`/`E`/`R`/`1`/`2`/`3` = `0`/`1` (키 이름은 `KeyEntry::label`을 그대로 쓴다), `[Options]`: `IntervalMs`(로드 시 31~200으로 클램프, autokey_e.cpp:258-259) / `Chain`. `.gitignore:8`에 등록돼 있으니 커밋되지 않는다
- 토글 클릭은 `WM_LBUTTONDOWN`(autokey_e.cpp:781-819)에서 처리한다. 확인 대화상자는 없다(2026-08-03 제거)
- **저장 타이밍이 입력 종류마다 다르다**: 셀/★ 토글과 슬라이더 드래그 종료는 즉시 쓰고(`CommitSettings`, autokey_e.cpp:308-313), 마우스 휠은 500ms 디바운스 타이머로 모아 쓴다(`MarkSettingsDirty`, autokey_e.cpp:302-305). 이유는 아래 "에러 로그 2026-10-27" 참조
- 그리드는 STATIC 컨트롤이 아니라 `WM_PAINT`에서 직접 그린다(`DrawKeyGrid`, autokey_e.cpp:403-435). **셀 좌표의 단일 출처는 `GridCellRect`(autokey_e.cpp:318-327)** 하나이며 그리기와 히트테스트(`HitTestKeyCell`, autokey_e.cpp:393-401)가 둘 다 이것을 통과하므로, 레이아웃 상수를 바꿔도 두 경로가 어긋날 수 없다. 슬라이더도 같은 구조다(`SliderRect`, autokey_e.cpp:330-337 → `DrawSlider` / `PtInSlider`)

### 레이아웃 — 창 크기는 콘텐츠에서 역산한다
하드코딩된 창 크기 상수는 **없다**. `MARGIN` / `ROW_GAP` / `KEY_CELL_W` / `KEY_CELL_H` / `LABEL_HEIGHT` / `SLIDER_FONT_SIZE`에서 `CLIENT_WIDTH` · `CLIENT_HEIGHT`를 계산하고(autokey_e.cpp:13-56), `WinMain`이 `AdjustWindowRectEx(WINDOW_STYLE, WINDOW_EX_STYLE)`(autokey_e.cpp:567-568)로 프레임 두께를 더해 `CreateWindowExW`에 넘긴다. 그 결과 클라이언트 영역이 정확히 콘텐츠 크기(실측 **76x92**, 창 **78x94**)가 되고 여백이 남지 않는다.

콘텐츠는 위에서 아래로 상태 라벨(`LABEL_HEIGHT` 24) → 키 그리드 2행(`KEY_GRID_H` 40) → 슬라이더 행(`SLIDER_H` = `SLIDER_FONT_SIZE + 4` = 20)이다. 슬라이더 행 높이를 눈대중으로 정하지 말 것 — ms 숫자를 트랙 가운데에 그리므로 폰트 크기에서 역산해야 글리프가 세로로 잘리지 않는다(autokey_e.cpp:49-53).

`WINDOW_STYLE`은 `WS_POPUP | WS_BORDER`다 — **제목표시줄이 없다.** `WS_CAPTION`은 세로 29px를 먹었고 그건 전체 창 높이의 3분의 1이었다. 제목표시줄을 없앤 대가로 두 가지를 직접 구현했으므로 스타일을 바꿀 때 함께 봐야 한다:
- **이동**: `WM_LBUTTONDOWN`이 슬라이더·키 셀 바깥이면 `ReleaseCapture()` 후 `SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0)`로 기본 이동 루프에 넘긴다(autokey_e.cpp:796-803). 이 루프는 메시지를 계속 펌핑하므로 드래그 중에도 키보드 훅은 정상 동작한다. **판정 순서는 슬라이더 → 키 셀 → 창 이동**이며, 슬라이더가 먼저여야 트랙 클릭이 창 이동으로 새지 않는다.
- **종료**: 닫기 버튼이 없으므로 `WM_RBUTTONDOWN`이 `WM_CLOSE`를 보낸다(autokey_e.cpp:833). 단 슬라이더 드래그 중이면 종료가 아니라 드래그만 취소한다 — 드래그 도중 잘못 누른 두 번째 버튼으로 앱이 사라지는 것이 최악의 결과이기 때문이다(autokey_e.cpp:821-831).

- 예전에는 `WINDOW_WIDTH/HEIGHT`를 직접 넘겨서 클라이언트가 84x96이 되고 우측 14px·하단 24px가 비어 있었다. **창 크기를 다시 하드코딩하지 말 것** — 프레임 두께는 시스템/테마마다 다르다.
- 폭은 키 개수가 아니라 **열 수**에서 나온다(`KEY_ROW_W = KEY_COLS * KEY_CELL_W`, autokey_e.cpp:26). 그래서 키를 4개에서 7개로 늘렸는데도 창 폭은 그대로이고 높이만 한 행 늘었다. 열을 늘리면 `CLIENT_WIDTH`가 자동으로 따라 넓어지므로 창 크기는 손댈 필요가 없다.
- 상태 라벨 · 키 그리드 · 슬라이더는 폭을 공유한다(`LABEL_WIDTH = SLIDER_W = KEY_ROW_W`, autokey_e.cpp:32,45). 셋의 우측 끝이 어긋나면 이 관계가 깨진 것이다.
- 슬라이더는 SCROLLBAR 컨트롤이 아니라 owner-drawn이다. 72px 트랙에 실제 스크롤바를 쓰면 화살표 버튼 두 개가 트랙을 거의 다 먹어 31~200ms를 표현할 픽셀이 몇 개 남지 않는다(autokey_e.cpp:38-42). **썸 바도 없다** — 초록 채움의 오른쪽 끝이 곧 현재값 표시이며, 그 자리에 어두운 썸을 그리면 트랙 중앙의 검은 숫자와 겹친다(autokey_e.cpp:46-48).

---

## 빌드

### 툴체인 — PATH에 없다
MinGW는 이 PC의 PATH에 등록되어 있지 않다(Machine/User PATH 모두 mingw/msys/gcc 항목 0건). 실체는 `C:\msys64\mingw64\bin`이며 `g++` 15.2.0 / `windres` / `strip` / `objdump`가 전부 여기 있다. PATH 설정 없이 `build.bat`을 돌리면 맨 앞 `where g++` 가드(build.bat:10-16)에 걸려 컴파일에 도달조차 못 하고 `[ERROR] g++ compiler not found!` 후 `pause`에서 멈춘다.

```batch
set PATH=C:\msys64\mingw64\bin;%PATH%
build.bat
```
Bash 도구에서는 `export PATH="/c/msys64/mingw64/bin:$PATH"`.

### 파이프라인
`build.bat`의 3단계:
1. `windres resource.rc -O coff -o resource.res` — 아이콘/버전정보 리소스 컴파일
2. `g++ -Wall -Wextra -O3 -mwindows -static autokey_e.cpp resource.res -o autokey_e.exe -lcomctl32 -luser32 -lpsapi -std=c++17`
3. `strip autokey_e.exe` (strip 미존재 시 경고만 내고 계속)

`build.bat`은 실패 시 `pause`로 입력을 대기하므로 비대화형 실행에서 멈춘다. 컴파일 오류만 빠르게 확인하려면 2단계 g++ 명령을 직접 실행할 것.

3단계 `strip`은 `where strip`이 실패하면 **경고만 내고 조용히 건너뛴다**(build.bat:44-50). 실제로 그 일이 벌어져 f6172b6부터 2026-08-03까지 커밋된 exe가 strip 안 된 148,561바이트로 남아 있었다. 현행 소스의 정상 크기는 **33,792바이트**(2026-10-27 실측, strip 적용)이며 2026-08-03 당시는 25,088바이트였다. 빌드 후 크기가 100KB를 넘으면 strip이 건너뛰어진 것이다 — `/c/msys64/mingw64/bin/objdump.exe -h autokey_e.exe | grep -c debug`가 0인지 확인할 것(2026-10-27 실측: 0).

### 링크 플래그 vs 실제 import
`objdump -p autokey_e.exe | grep "DLL Name"` → 현재 import는 **GDI32 / KERNEL32 / msvcrt / USER32 4개뿐**이며 winmm.dll은 없다(2026-10-27 재확인). "타이밍 제약"의 winmm 미링크는 실측 확인된 사실이며, 이 명령이 회귀 감시 수단이다.

- **주의**: `objdump`도 PATH에 없다. `/c/msys64/mingw64/bin/objdump.exe`를 직접 쓸 것. bare `objdump`는 `command not found`로 죽고 그러면 winmm grep도 exit 1이라 **"winmm 없음"과 신호가 구분되지 않는다**. `DLL Name` 4줄이 실제로 출력되는지 먼저 확인할 것.
- `-lpsapi`: 현행 소스는 PSAPI 함수를 **하나도 호출하지 않으므로**(`#include <psapi.h>`만 autokey_e.cpp:5에 남아 있다) import 목록에 PSAPI.DLL이 안 보이는 것이 정상이다. PSAPI가 없다고 LoL 감지가 빠진 게 아니다 — 감지는 KERNEL32의 `QueryFullProcessImageNameW`로 한다("LoL 감지" 참조).
- `-lcomctl32`: 실제 호출이 없어 COMCTL32 import가 생기지 않는 사문화된 플래그다(autokey_e.cpp:4의 `#include <commctrl.h>`도 미사용). 무해하므로 제거하지 말 것 — exe가 git에 추적되므로 플래그 변경은 재빌드·재커밋까지 동반되는 검증되지 않은 변경이 된다.

### 매니페스트는 저장소에 없다
`resource.rc`에는 매니페스트 리소스가 없고(전체 27줄이 `1 ICON` + `1 VERSIONINFO`뿐) `build.bat:36`에도 지정이 없다. exe에 박혀 있는 `requestedExecutionLevel level="asInvoker"` + supportedOS 매니페스트는 MinGW가 자동 링크하는 `C:\msys64\mingw64\lib\default-manifest.o`에서 온다 — 그래서 저장소를 grep해도 안 나온다. 이 기본 매니페스트에는 `dpiAware`가 없고 소스에도 `SetProcessDPIAware` 호출이 없어 앱은 **DPI 비인식**이다(`MARGIN` / `KEY_CELL_W` / `FONT_SIZE` 등 소스 상단의 레이아웃 상수가 전부 100% 배율 기준이며, 배율 디스플레이에서는 Windows가 창을 비트맵 확대해 흐릿해진다). UAC 수준이나 DPI 인식을 바꾸려면 `resource.rc`에 `1 24 "app.manifest"` 한 줄과 매니페스트 파일을 추가하면 되고, 기본 매니페스트를 충돌 없이 대체한다(실측 검증).

### 아이콘 재생성
`icon.ico` / `icon.png`는 `create_icon.py`(Pillow)가 생성한다:
```
py create_icon.py
build.bat
```
`resource.rc:1`의 `1 ICON "icon.ico"`가 리소스 ID 1로 등록하고 `autokey_e.cpp:553`의 `LoadIcon(hInstance, MAKEINTRESOURCE(1))`이 참조한다 — ID 변경 시 양쪽 모두 수정.

### 테스트
**키 처리 경로는 원리상 자동화가 불가능하다**: hook 최상단의 `LLKHF_INJECTED` 통과(autokey_e.cpp:876-878)가 주입 출처를 구분하지 않으므로 `SendInput`/`keybd_event`로 보낸 F2/F3/키는 훅에 아무 영향을 주지 못한다.

자동 검증이 가능한 범위는 둘이다:
- **UI 경로**: `PostMessage(WM_LBUTTONDOWN/WM_MOUSEWHEEL)`로 셀·슬라이더 좌표를 찍고 `GetClientRect`·ini 내용으로 확인한다(2026-10-27에 이 방식으로 62항목 검증).
- **순수 함수**: `IntervalToTicks`(autokey_e.cpp:488-496)는 전역을 읽지 않으므로 앱 밖에서 그대로 단위 테스트/분포 측정이 된다. 새 판정 로직도 이렇게 분리하면 검증 가능해진다.

**함정 — `FindWindow` 성공은 메시지 루프 준비 완료가 아니다.** 창은 `CreateWindowExW` 직후부터 찾히지만 메시지 루프는 그보다 뒤에 시작하므로(autokey_e.cpp:571 vs :650), 찾은 직후 `PostMessage`를 쏘면 큐에 들어간 메시지가 유실되고 "토글이 안 된다"로 오진된다. 검증 스크립트는 창을 찾은 뒤 **1초 settle**을 두고 보낼 것.

수동 검증(자동화 불가 항목): F2/F3 토글, LoL 포커스 시 ON/OFF 자동 전환, 키 홀드 시 반복 입력, **장시간 홀드 시 프리즈 없음**(짧게 누르면 과거 프리즈 버그가 재현되지 않으므로 반드시 길게), 실행 시 LoL이 이미 떠 있으면 F2 없이 ON으로 시작, ★ 연동 모드의 실제 출력, 물리 휠로 주기 조절.

---

## 아키텍처 — 핵심 불변식

`autokey_e.cpp`에는 4개의 **코드 컨텍스트**(hook 콜백 / worker 스레드 / WinEvent 콜백 / WindowProc)가 있지만 **실제 스레드는 2개뿐**이다. 이 구분이 이 프로젝트의 유일하고 핵심적인 설계다.

- hook 콜백(`SetWindowsHookEx`, autokey_e.cpp:622, WH_KEYBOARD_LL)과 WinEvent 콜백(`SetWinEventHook`, autokey_e.cpp:625-633, WINEVENT_OUTOFCONTEXT)은 둘 다 훅을 설치한 **WinMain 메인 스레드**로 배달되며, `autokey_e.cpp:650-653`의 메시지 루프가 계속 펌핑돼야 호출된다. 따라서 hook 콜백 · WinEventProc · WindowProc(autokey_e.cpp:680)은 같은 스레드에서 **직렬 실행**된다.
- 진짜로 분리된 스레드는 worker(`CreateThread`, autokey_e.cpp:637, `WorkerThreadProc`:1009) 하나뿐이고, **유일한 키 전송 지점**이다(`SendKey`:967-973).

여기서 나오는 두 가지 함정:
1. 메인 스레드 쪽 셋 사이에는 경합이 없으므로 락/동기화를 추가하지 말 것(실제로 파일 전체에 크리티컬 섹션·뮤텍스가 0건). 반대로 셋 중 하나가 느려지면 나머지가 같이 밀린다 — 특히 `WinEventProc`는 `IsLeagueOfLegendsWindow` → `OpenProcess` + `QueryFullProcessImageNameW`(autokey_e.cpp:1062,1074)라는 무거운 호출을 하므로 그동안 키보드 훅 처리가 지연되고, `LowLevelHooksTimeout`(기본 ~300ms)을 넘기면 해당 키의 차단이 조용히 건너뛰어지거나 훅 자체가 해제될 수 있다. **같은 이유로 이 스레드에서 하는 파일 I/O도 위험하다** — 2026-10-27 에러 로그가 그 실례다.
2. 진짜 경계는 "메인 스레드 ↔ worker 스레드"이며, `volatile bool` 플래그가 유일한 교차 스레드 통신 수단이다. 키 전송이 worker에만 있어야 하는 이유가 이 경계다 — 넘으면 hook chain 재진입으로 입력 파이프라인이 프리즈한다.

### volatile 플래그가 안전한 실제 이유
교차 스레드 상태는 전역 `isEnabled` / `g_exitThread` / `g_intervalMs` / `g_chainMode`(autokey_e.cpp:156,167-168,177)와 `g_keys[]` 각 항목의 `selected` / `pressed` / `weBlockedDown`이 전부다. 모두 `volatile`일 뿐 C++ 표준의 스레드 동기화 수단이 아니다(MinGW g++ 빌드라 MSVC의 `/volatile:ms` 획득/해제 의미론도 없다). 그럼에도 깨지지 않는 이유는 **모든 쓰기가 메인 스레드에서만 일어나는 단일 writer 구조**이기 때문이다 — 쓰기 지점은 `WinMain` / `LowLevelKeyboardProc` / `WinEventProc` / `WindowProc`(토글 클릭 · 슬라이더 · 휠)뿐이고 넷 다 메인 스레드에서 실행된다. `WorkerThreadProc`는 읽기만 한다.

슬라이더 드래그 상태(`g_sliderCapture`)와 저장 대기 플래그(`g_settingsDirty`)는 `WindowProc` 안에서만 쓰이고 worker가 보지 않으므로 `volatile`이 아니다(autokey_e.cpp:172-173). worker가 읽어야 하는 값을 새로 추가할 때만 `volatile`을 붙이고, 그 경계를 흐리지 말 것.

worker는 난수도 **스택 로컬 상태**로 돌린다(`XorShift32` + `rngState`, autokey_e.cpp:978-988,1010). `<random>`을 안 쓴 것은 정적 링크 크기 때문이고, 전역 시드를 두지 않은 것은 단일 writer 불변식을 지키기 위해서다 — 지터용 난수 상태를 전역으로 올리면 worker가 전역에 쓰는 첫 사례가 된다.

지켜야 할 불변식:
- worker 스레드에서 이 플래그들에 **쓰지 말 것**. 전송 후 정리 목적으로 `pressed = false`를 worker에 넣으면 단일 writer 전제가 깨진다.
- 여러 플래그를 원자적으로 함께 읽어야 하는 복합 불변식을 만들지 말 것. worker의 `selected && pressed` 검사는 순간적으로 불일치 조합을 볼 수 있으나 최악이 키 1회 추가 전송이라 무해하다.
- 어겨야 하면 임시방편 대신 `std::atomic<bool>`로 교체하는 것이 정답이다.

### 재진입 차단
worker가 보낸 키는 hook 최상단(autokey_e.cpp:876-878)의 `LLKHF_INJECTED` 체크로 즉시 통과시킨다. **이 체크를 빼면 생기는 문제는 무한 재귀가 아니라 기능 무력화다** — injected keydown이 autokey_e.cpp:916-920에서 다시 `return 1`로 차단돼 게임에 닿지 않고, 뒤이은 injected keyup이 autokey_e.cpp:902-903에서 `pressed`를 지워 반복이 매 tick 끊긴다. 재귀·프리즈는 hook 콜백 **안에서** `keybd_event`를 부르던 과거 구조의 증상이며, 현재는 키 전송이 worker에만 있어 재귀 경로 자체가 없다.

이 체크는 주입 출처를 구분하지 않고 F2/F3·키 처리보다 위에 있으므로, **다른 프로그램이 주입한 키도 전부 무시된다**(AutoHotkey·화상 키보드·원격 데스크톱 등). 소스 주석 autokey_e.cpp:875의 "from our worker thread"는 실제 범위보다 좁게 읽히니 주의.

★ 연동 모드도 이 구조 위에 그대로 얹혀 있다 — **hook은 전혀 손대지 않았다.** 물리적으로 누른 키 하나만 hook이 삼키고, 같은 사이클에 나가는 나머지 선택 키들은 injected 입력이라 위 체크로 통과한다(autokey_e.cpp:1003-1004). 연동 모드를 고칠 때 hook 쪽에 조건을 추가하려는 충동이 들면 이 설계를 먼저 볼 것.

### 원본 키 차단과 전역 소비 범위
hook이 `return 1`을 반환하면 원본 키가 시스템으로 전달되지 않는다 (F2: autokey_e.cpp:**889** / F3: **894** / 선택 키 up: **910** / down: **919**). 888·893은 `PostMessage(WM_APP+1)` 줄이므로 혼동하지 말 것.

**F2/F3 분기(autokey_e.cpp:884-896)에는 isEnabled 게이트도 LoL 포그라운드 게이트도 없고**, 훅은 전역 설치(autokey_e.cpp:622, `dwThreadId=0`)다. 따라서 앱이 OFF이고 LoL이 아닌 창을 쓰고 있어도 프로세스가 떠 있는 동안 F2/F3는 어떤 앱에도 도달하지 않는다 — 탐색기 이름 바꾸기(F2), 브라우저·에디터 찾기(F3), Excel 셀 편집(F2)이 전부 무반응이 되고 해제 방법은 앱 종료뿐이다. 선택 키는 isEnabled 게이트는 있지만(down:916 / up:902-911) 포그라운드 게이트가 없어, F2로 수동 ON한 뒤에는 다음 포그라운드 변경이 자동 OFF시킬 때까지 비-LoL 앱에서도 그 키가 삼켜진다.

**이 범위는 키가 7개로 늘면서 그만큼 넓어졌다.** 1/2/3을 켜 두면 비-LoL 앱에서도 숫자 입력이 사라질 수 있으므로, "메모장에 숫자가 안 찍힌다" 같은 신고가 오면 선택 상태(ini `[Keys]`)와 ON/OFF를 먼저 확인할 것. 핫키를 바꾸거나 게이트를 추가할 때도 이 범위를 먼저 볼 것.

### 시작 시 초기 상태 시드
`SetWinEventHook`은 포그라운드 **변경** 이벤트만 구독하므로 이미 LoL이 떠 있는 상태로 실행하면 훅이 발화하지 않는다. 이 공백을 autokey_e.cpp:640-643의 시작 시 `GetForegroundWindow()` 1회 검사가 메운다. 이 검사를 지우면 "실행 후 alt-tab을 한 번 해야 켜지는" 회귀가 생기며, 포커스 전환 위주의 수동 검증으로는 잡히지 않는다.

`UpdateUI()` 직접 호출 지점은 초기화 경로(autokey_e.cpp:645) · `WM_APP+1` 핸들러(autokey_e.cpp:721) · 토글 클릭(:817) · 슬라이더 값 변경(`SetIntervalFromX`:376, `NudgeInterval`:386)이다. 앞의 둘만 `isEnabled`를 반영하므로, **`isEnabled`를 바꾸는 코드를 새로 추가할 때는 `PostMessage(hMainWnd, WM_APP + 1, 0, 0)`을 반드시 함께 넣어야** UI가 따라간다(hook 콜백에서 `UpdateUI()`를 직접 부르는 것은 재진입 금지 대상이다).

`UpdateUI`에는 상태 변화 가드가 있다(`static unsigned lastBits` / `static int lastInterval`, autokey_e.cpp:935-936, 초기값은 둘 다 불가능한 값이라 첫 그리기는 항상 실행된다). 전체 다시 그리기 1회가 실측 ~126us(최대 ~1.2ms)이고 그 시간만큼 훅을 서비스하는 메인 스레드를 점유하므로, 표시가 실제로 바뀔 때만 그린다. 현재 가드는 두 값이다:
- `lastBits` = `isEnabled` 1비트 + 키 선택 7비트 + 연동 모드 1비트(autokey_e.cpp:938-942)
- `lastInterval` = 슬라이더 ms 값

둘 다 같으면 그리지 않고, **interval만 바뀌었으면 슬라이더 RECT만 무효화한다**(autokey_e.cpp:948-960). 드래그 중에는 거의 매 `WM_MOUSEMOVE`마다 여기로 들어오는데, 전체 클라이언트를 지우고 다시 그리면 창이 번쩍였고 `DrawSlider`가 트랙 전체를 직접 칠하므로 erase도 필요 없다.

**창에 새 표시를 추가하면 이 가드가 그 변화를 삼키므로 반드시 가드 값에 포함시킬 것.** 비트를 더 쓰려면 `static_assert(KEY_COUNT < 31)`(autokey_e.cpp:63)이 지키는 폭도 함께 볼 것.

### 종료 순서 (훅 해제가 뒤에 있다)
종료 경로는 메시지 루프 종료(autokey_e.cpp:650-653) → worker 조인(:656-662, 상한 1000ms) → 훅 해제(:665-673) 순이다. `WH_KEYBOARD_LL` 콜백은 설치 스레드의 메시지 펌프로 배달되므로 이 구간은 **훅이 걸려 있는데 아무도 서비스하지 않는** 상태다 — 키 입력마다 `LowLevelHooksTimeout`을 기다린 뒤에야 무시되며 그동안 시스템 전체 입력이 지연된다. 지금은 worker가 매 tick `g_exitThread`를 확인해 금방 빠져나가므로 체감되지 않을 뿐이다. 종료 경로에 대기·정리 작업을 추가하면 즉시 "끌 때 키보드가 멈춘다"로 나타나므로 새 정리 코드는 `UnhookWindowsHookEx` **뒤**에 넣거나 훅 해제를 조인 앞으로 옮길 것. (autokey_e.cpp:664의 `Cleanup resources in proper order` 주석은 이 점에서 신뢰하지 말 것.)

`WM_DESTROY`는 그보다 먼저 `CommitSettings`(autokey_e.cpp:838)로 아직 안 쓴 디바운스 값을 기록한다. 이 호출은 메시지 루프가 끝나기 **전**이라 위의 무서비스 구간에 들어가지 않지만, **ini 쓰기 한 번이 수 ms**이므로 종료 경로에 파일 I/O를 더 얹지 말 것.

**worker 수명 > GDI 수명**: `WM_DESTROY`가 브러시·폰트를 해제한 뒤(autokey_e.cpp:841-860)에야 `PostQuitMessage`(:862)가 루프를 끝내므로 worker는 GDI 객체 파괴 이후까지 살아 있다. 지금 안전한 이유는 `SendKey`가 GDI/HWND를 전혀 만지지 않기 때문뿐이다. "worker는 UI/GDI를 만지지 않는다"는 취향이 아니라 수명 불변식이다. 슬라이더를 owner-drawn으로 만든 뒤에도 worker는 `g_intervalMs`라는 **정수 하나만 읽는다** — 그리기 자원에 접근하지 않으므로 이 불변식이 유지된다.

### 타이밍 (실측)
- **`timeBeginPeriod` 사용 금지** — LoL 인게임 프레임 렉 유발(에러 로그 2026-04-08). winmm 링크도 하지 않는다.
- **대기 단위는 여전히 `Sleep(1)` 호출 횟수(tick)다**(autokey_e.cpp:1043-1046). `timeBeginPeriod` 미적용 시 타이머 tick은 ~15.5ms이고, 이 루프처럼 `Sleep(1)`을 **연속 호출**하면 직전 반환이 tick 경계에 정렬되어 매 호출이 꼬박 1 tick을 소모한다. 따라서 **실제 주기 ≈ tick 수 x 15.5ms**이며, 주기는 연속값이 아니라 ~15.5ms 격자 위에서만 실현된다.
- 사용자가 만지는 값은 ms다: `g_intervalMs`(autokey_e.cpp:167, 31~200, 기본 115)를 슬라이더가 쓰고 worker가 읽는다. 과거의 `E_KEY_INTERVAL` 상수는 **제거됐다** — tick 환산은 `IntervalToTicks`(autokey_e.cpp:488-496)가 한다.
- **`IntervalToTicks`의 3단계** (`TICK_MS = 15.5`, `JITTER_MS = 6`, `MIN_TICKS = 2`, autokey_e.cpp:97-102):
  1. 목표 ms에 `±JITTER_MS` 균등 난수를 더하고 31~200으로 클램프
  2. `목표 / TICK_MS`를 **확률적 반올림(디더링)** — 소수부 확률로 위/아래 tick을 고른다. 격자가 거친데도 **평균 주기가 설정값과 일치하는** 이유가 이것이다
  3. `MIN_TICKS`(2) 미만으로는 내려가지 않는다
- **양 끝은 평균이 안쪽으로 밀린다**: 1단계 클램프가 한쪽만 걸리므로 31 설정은 평균 ~32.5ms, 200 설정은 평균 ~198.5ms가 된다(디더링 하니스 10만회 실측; 범위 중앙 115는 115.0ms로 일치). 사용자가 요청한 하한보다 빨라지지 않게 하는 쪽을 택한 의도적 결과다.
- **wake 불변식 (속도 조절의 핵심)**: worker 스레드가 깨어나는 횟수는 설정 주기와 **무관하게 항상 ~64.7회/초로 고정**이다. 주기는 "몇 번 깨어날 때마다 키를 쏘느냐"만 정한다. 즉 값을 낮춰도 스케줄러 부하는 전혀 늘지 않고 주입 이벤트 수만 는다 — 2026-04-08 렉 기전(글로벌 타이머 틱 상승)과 인과 경로가 겹치지 않는 이유가 이것이다. idle 상태(`Sleep(2)`)의 wake도 동일한 ~64회/초다.
- tick 수 대비 실측값(앱 구조 충실 복제 하니스 = 메인 스레드 자체 WH_KEYBOARD_LL + 메시지 펌프 + worker 실주입, 8초씩, 2026-08-03): 10 tick → 154.6ms(6.47회/초), 5 → 77.3ms(12.94), 3 → 46.4ms(21.56), **2 → 30.9ms(32.37회/초 = 현재 슬라이더 하한)**, 1 → 15.5ms(64.40회/초). 전 구간 wake 64.4~64.7회/초. 완전한 선형. 슬라이더 범위 31~200ms는 이 격자에서 **2~13 tick**에 대응한다.
- **속도 상한**: 1 tick의 ~64회/초가 이 Sleep 기반 구조의 물리적 한계이고, 슬라이더 하한을 31ms(2 tick)로 둔 것은 그 한계 직전에서 멈추기 위한 의도적 선택이다. 0 tick은 대기 루프를 통째로 없애 `SendKey()`가 무한 연타되므로 **금지** — `MIN_TICKS` 가드를 없애지 말 것.
- **★ 연동 모드는 주기를 바꾸지 않고 사이클당 주입 횟수를 늘린다**(autokey_e.cpp:996-1002). 선택 키 3개면 같은 주기에 3개가 연속으로 나간다. 즉 게임 입력 파이프라인 부하는 "주기"가 아니라 **선택 키 수 x 1/주기**로 봐야 한다.
- 64회/초를 넘겨야 한다면 `timeBeginPeriod`가 아니라 `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`(Win10 1803+, MSYS2 헤더에 상수 있음)를 쓸 것. 같은 하니스 실측: 30ms→32.0회/초, 15ms→62.1, **10ms→90.6회/초**, 5ms→171회/초. 시스템 타이머 해상도를 올리지 않고 15.6ms 장벽을 넘으며, **같은 전송률에서 wake는 오히려 절반**이다(32회/초 기준 sleep-loop 64.6 wake vs HR 타이머 32.0 wake). 단 인게임 검증은 미실시이며, one-shot 재arm으로 쓸 것(periodic 모드는 지터 3배).
- 다른 앱이 시스템 타이머를 1ms로 올려도 **소용없다**. Windows 10 2004+/11에서 타이머 해상도는 프로세스 단위이고 `autokey_e.exe`는 `timeBeginPeriod`를 호출하지 않는다(실측: 시스템 current=1.0ms 상태에서도 미호출 프로세스의 `Sleep(1)`은 avg 15.56ms).
- idle 폴링은 `Sleep(2)`(autokey_e.cpp:1049)로 적혀 있지만 **실제 주기는 1 tick ~15.6ms**다. 기본 해상도에서 `Sleep(1)`과 `Sleep(2)`는 실측 동일(15.56 vs 15.58ms)이므로 이 값 조정은 아무 효과가 없다. 키를 누른 뒤 첫 전송까지 0~16ms 지연이 있다.
- 반복 대기 루프는 `Sleep(1)` **뒤에** 플래그를 재확인하므로(autokey_e.cpp:1043-1046) 키를 뗀 뒤 멈출 때까지 최대 1 tick(~16ms) 지연이 있다 — "즉시"가 아니다. 그래도 남은 주기 전체(최대 ~200ms)를 기다리는 것보다 훨씬 빠르므로 이 체크는 유지할 것.

### 단일 인스턴스
`FindWindowW(L"P2", L"P2")` → `SendMessage(WM_CLOSE)` → `IsWindow` 폴링 2초 → `TerminateProcess` 폴백(autokey_e.cpp:520-544). **창 클래스명과 타이틀이 둘 다 `"P2"`**여야 동작하므로 어느 한쪽만 바꾸면 중복 실행된다.

함정: 2초 상한은 폴링 루프(autokey_e.cpp:526-529)에만 적용되고 그 앞의 `SendMessage`(:523)에는 타임아웃이 없다. 기존 인스턴스의 메시지 펌프가 멈춰 있으면 새 인스턴스는 창을 만들기도 전에 무기한 블로킹되고 TerminateProcess 폴백에도 도달하지 못한다 — 이 경우 작업 관리자로 강제 종료해야 한다. (내성이 필요하면 `SendMessageTimeout(..., SMTO_ABORTIFHUNG, 2000, ...)`으로 교체.)

### LoL 감지
프로세스명 우선(`OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `QueryFullProcessImageNameW(PROCESS_NAME_NATIVE)` → `League of Legends.exe` / `LeagueClient.exe`), 실패 시 창 제목 부분일치(`League of Legends (TM) Client`)로 폴백(autokey_e.cpp:1056-1112). 제목 폴백은 프로세스 핸들 열기가 권한 문제로 실패하는 경우를 커버한다.

2026-08-03에 구 방식(`PROCESS_QUERY_INFORMATION | PROCESS_VM_READ` + PSAPI `GetModuleFileNameExW`)에서 교체했다. 실측(전체 399 프로세스, 2회 재현): 이름 조회 성공 196 → 215건(+19), 양쪽 성공한 196건은 **이름 100% 동일**(불일치 0건), 호출당 6.6us → 1.05us. `PROCESS_NAME_NATIVE`는 device 경로를 드라이브 문자로 변환하는 단계를 생략하는 것이 이득의 대부분이며, 반환 경로도 백슬래시 구분자라 basename 추출 로직은 그대로다. 이 경로는 `WinEventProc`를 통해 **메인 스레드**에서 실행되고 그 스레드가 키보드 훅도 서비스하므로 비용이 곧 훅 지연이다.

`-lpsapi`(build.bat:36)는 이 교체로 실제 호출이 사라져 `-lcomctl32`와 마찬가지로 사문화됐다. 무해하므로 제거하지 말 것(exe가 추적되므로 링크 플래그 변경은 재빌드·재커밋을 동반하는 미검증 변경이 된다).

**단, 오탐 방향이 있다.** `wcsstr` 부분일치(autokey_e.cpp:1106)는 프로세스를 전혀 확인하지 않으므로, 제목에 그 문자열이 들어간 아무 창(브라우저 탭·스트림 창 등)이 포그라운드가 되면 자동 ON된다. ON 상태에서는 그 앱에서 선택 키의 down/up이 모두 차단되므로, "특정 앱에서만 키 입력이 이상하다"는 증상이 나오면 그 창의 제목부터 확인할 것.

### 창 배치
`EnumDisplayMonitors`로 가장 왼쪽 모니터의 좌상단을 찾아 배치, 열거 실패 시 (0,0) 폴백(autokey_e.cpp:557-563). `WINDOW_EX_STYLE = WS_EX_TOPMOST | WS_EX_TOOLWINDOW`이라 작업표시줄과 alt-tab에 나오지 않는다 — 제목표시줄도 없으므로 창을 잃어버리면 되찾을 UI가 없다. 위치는 매번 계산되고 저장하지 않으므로 재실행하면 좌상단으로 돌아온다.

---

## 알려진 함정과 진단

### F2/F3 수동 토글은 다음 포그라운드 전환까지만 유지된다
`WinEventProc`(autokey_e.cpp:1115-1137)가 창이 바뀔 때마다 LoL 여부만 보고 `isEnabled`를 무조건 재설정하고, F2/F3(:885-887, :890-892)는 수동 오버라이드 플래그를 남기지 않는다. 따라서 LoL에서 F3로 꺼도 다른 창에 갔다 돌아오면 다시 ON이 되고, LoL이 아닌 앱에서 F2로 켜면 다음 전환에서 OFF로 돌아간다. "F3로 껐는데 다시 켜진다"는 버그가 아니라 설계 결과이며, 영구 수동 오버라이드가 필요하면 별도 플래그를 도입해야 한다.

### keyup 차단 비대칭 (stuck key) — 2026-08-03 해결됨
과거에는 keydown 차단이 "누른 시점의 `isEnabled`", keyup 차단이 "뗀 시점의 `isEnabled`"로 각각 결정돼, 키를 누르고 있는 도중 앱이 켜지면(F2 또는 LoL 포그라운드 진입) 원본 down은 이미 앱에 전달됐는데 up만 삼켜져 대상 앱이 키를 눌린 채로 인식할 수 있었다.

다중 키 확장과 함께 `KeyEntry::weBlockedDown`으로 해결했다(autokey_e.cpp:902-913) — **우리가 그 keydown을 삼켰을 때만 대응하는 keyup을 삼킨다.** 이 플래그는 `ClearPressedKeys()`(autokey_e.cpp:217-221)가 건드리지 않는다(이미 삼킨 down에 대한 up은 여전히 우리가 갚아야 하므로). 키 처리 로직을 고칠 때 이 대칭을 깨지 말 것.

주의: 이 경로는 **자동 테스트가 불가능하다**. hook이 `LLKHF_INJECTED`를 최상단에서 통과시키므로 합성 입력으로는 재현되지 않으며, 물리적으로 키를 누른 채 F2를 눌러야 검증된다.

### 실패는 무증상이다
`RegisterClassW`(:555), `SetWindowsHookEx`(:622), `SetWinEventHook`(:625), `CreateThread`(:637) 모두 반환값을 검사하지 않고, 저장소 전체에 `MessageBox`·로그·`GetLastError` 호출이 0건이다. 창 생성 실패조차 조용히 `return 0`(:581-583). **UI의 ON/OFF는 훅이 살아 있다는 증거가 아니다** — 실행 시 LoL이 포그라운드면 키보드 훅이 죽어 있어도 ON으로 표시된다(:640-645).

ini 경로도 같은 성향이다: `InitIniPath`가 실패하면 `g_iniPath`가 빈 문자열이 되고 `LoadSettings`/`SaveSettings`가 조용히 아무 일도 하지 않는다(autokey_e.cpp:247, :280). "설정이 저장되지 않는다"는 증상은 exe 옆에 `autokey_e.ini`가 생겼는지부터 확인할 것.

증상별 절단:
- F2/F3 토글이 전혀 안 먹힘 → 키보드 훅 실패 (F2/F3 처리는 훅 안에만 존재)
- F2 토글은 되는데 LoL 포커스 자동 전환만 안 됨 → WinEvent 훅 쪽(:625)
- 창은 ON인데 키 반복만 안 먹음 → 권한(UIPI) 문제 우선 확인 (아래)

원인을 좁히려면 해당 호출 지점에 임시로 반환값 검사 + `MessageBox`를 넣어 확인하고, 검증 후 반드시 제거할 것.

### 권한(UIPI) 제약
빌드 산출물에는 권한 상승 요청이 없다(`asInvoker`, uiAccess 없음 — "매니페스트는 저장소에 없다" 참조). 대상 창이 관리자 권한으로 실행 중이면 UIPI가 worker의 `keybd_event`(:971-972) 주입을 차단하므로 press2도 관리자 권한으로 띄워야 한다. 이때 **창은 ON인데 키 반복만 안 먹는** 증상이 나오는데, 권한 부족으로 `OpenProcess`(:1062)가 실패해도 창 제목 폴백(:1105-1106)이 감지를 성공시키기 때문이다. "입력이 안 먹는다" 신고는 코드를 파기 전에 양쪽 프로세스의 권한 수준부터 확인할 것.

---

## git 워크플로

### Windows `nul` 파일 문제

**발생 원인**: Bash 도구(Git Bash)에서 cmd 스타일 `>nul` / `2>nul` 리다이렉션을 쓰면 `/dev/null`이 아니라 현재 디렉터리에 진짜 `nul` 파일이 생긴다(실측: `where cmd >nul 2>&1` → 29바이트 `nul` 생성). `build.bat:10,19,44`와 `git-add-safe.bat:23`에 이 패턴이 들어 있으므로 **그 줄을 bash로 복사해 실행하지 말 것** — .bat 안에서 cmd가 해석할 때만 장치명이라 안전하다. bash에서는 `>/dev/null 2>&1`.

**실질 방어책은 `.gitignore:2-5`의 `nul` / `nul.*` / `NUL` / `NUL.*` 4줄이다.** 이 규칙이 살아 있는 한 stray `nul` 파일은 `git status` / `git add -A` / `git commit`을 깨뜨리지 않는다(실측: 규칙 없이 `git add -A` → `error: short read while indexing nul` … `fatal: adding files failed`, exit 128 / 규칙 있으면 exit 0 + 커밋 성공). **이 4줄을 지우지 말 것**이 핵심 지침이고, `git add` 실패를 nul 탓으로 단정하지 말 것.

정리 인프라 3종은 이 규칙이 없던 시절의 유산이며 현재는 발동할 일이 없다:
- `.git/hooks/pre-commit` — 활성 훅이지만 `.git/` 안이라 **커밋되지 않는다**(이 PC 로컬 전용, clone하면 없음). 정리는 저장소 밖 `C:\programming\2025\nul.bat`에 위임하며 실패해도 `exit 0`이라 커밋을 막지 않는다. 평상시엔 sh `[ -e "nul" ]`가 false라 즉시 no-op.
- `fix-nul.ps1` — 확장 경로(`\\?\`)로 삭제하는 PowerShell 스크립트. **자동 경로에서는 호출되지 않는다**(`git-add-safe.bat:10-15,31-35`는 `nul.bat`이 있으면 그쪽만 `call`하고 fix-nul.ps1은 else 분기, pre-commit은 참조조차 안 함). 사람이 직접 `powershell -ExecutionPolicy Bypass -File fix-nul.ps1`로 돌릴 때만 의미가 있고, `Get-Location` 기준 재귀라 영향이 저장소 안에 머물며 권한 상승·레지스트리 쓰기가 없다 — **수동 정리는 이쪽을 쓸 것**.
- `git-add-safe.bat` — **사용하지 말 것** (아래).

### `git-add-safe.bat`을 쓰지 말 것
1. `git-add-safe.bat:6`의 `if exist nul`은 **항상 참**이다 — cmd에서 `nul`은 모든 디렉터리에 존재하는 예약 장치명이라 실제 파일 유무와 무관하다(실측: 파일 부재 상태에서 TRUE, 대조군 `if exist zzz_nofile`은 FALSE). 매 실행마다 "NUL file detected!"를 출력하고 외부 `nul.bat`을 무조건 호출한다.
2. `nul.bat`은 관리자 권한 자가상승(`runas`)을 시도한다. 비관리자 셸에서는 매번 UAC 프롬프트가 떠 비대화형 흐름이 멈춘다.
3. 관리자 셸이면 `nul.bat` 마지막 줄이 `exit /B`가 아닌 bare `exit`라 **cmd 세션 전체가 종료**되어 정작 `git add -A`(:23,:39)에 도달하지 못한다. 종료 코드는 0이라 실패로 보이지 않는다 — 출력이 `NUL file detected!`에서 끊기고 `Cleanup completed.` / `Done!`이 안 나오면 add가 실행되지 않은 것이다.
4. 동작하더라도 `git add -A`라서 의도적으로 미추적 상태인 파일(`autokey_e_simple.cpp`, `autokey_e_complex.cpp.backup`, 루트 `claude-code-ps.bat`)을 전부 스테이징한다 — 셋 다 `.gitignore` 규칙이 없다.

**1차 정리 경로는 저장소 밖에 있다.** `C:\programming\2025\nul.bat`은 clone에 따라오지 않으며, 실행 시 press2를 크게 벗어난다: `cd /d "%~dp0"`로 작업 디렉터리를 `C:\programming\2025`로 바꾼 뒤 `for /d /r`로 **2025 트리 전체**를 재귀 스캔하고 `%TEMP%`/바탕화면/다운로드도 훑으며, `nul.*` 와일드카드에 자기 자신이 걸려 삭제될 수 있고, 삭제 실패 시 HKLM `PendingFileRenameOperations`에 재부팅 삭제를 예약한다.

**존재 확인 주의**: 이 파일을 PowerShell `Test-Path`로 확인하지 말 것 — 예약 장치명 파싱 때문에 실제로 있어도 False를 돌려주며 `\\?\`를 붙여도 마찬가지다. `Get-ChildItem -Force -Filter 'nul*'`, `[System.IO.File]::Exists()`, cmd `if exist`, Git Bash `ls`를 쓸 것.

### 빌드 산출물 커밋
`autokey_e.exe`와 `resource.res`는 **git에 추적되지만 이유가 서로 다르다**. `resource.res`는 `.gitignore:13`의 `*.res`에 매치되나 이미 인덱스에 있어 무시 규칙이 무력화된 상태다(`git check-ignore -v --no-index resource.res`로만 확인 가능 — 인덱스를 보는 기본 `git check-ignore`는 아무것도 보고하지 않는다). 반면 `autokey_e.exe`는 `.gitignore`에 `*.exe` 규칙 자체가 없다(:14는 `*.exe.manifest`). 즉 새로 생기는 임의의 `.exe`도 그대로 스테이징 대상이므로 실험 빌드 산출물을 남겨두지 말 것.

소스 수정 후에는 `build.bat`을 돌려 재빌드된 exe까지 커밋하는 것이 이 저장소의 관례다(`git log --oneline -- autokey_e.exe` — 소스 변경 커밋과 별개로 `build:` 타입 exe 커밋이 누적).

### `.claude/`도 추적 대상
`.claude/claude-code-cmd.bat`, `.claude/claude-code-ps.bat`, `.claude/settings.local.json`이 git에 추적된다(`.gitignore`에 `.claude/` 규칙 없음). 통상 로컬 전용인 `settings.local.json`까지 추적되므로 Claude Code 권한/설정을 바꾸면 그대로 작업 트리 변경으로 잡힌다. 소스 변경 커밋 시에는 `git add`에 파일을 명시할 것.

주의: `.claude/claude-code-ps.bat`과 루트 `claude-code-ps.bat`은 `::` 주석에 한글을 포함한 배치 파일이다(둘 다 `C:\Tools\cc.bat` 호출). cmd는 배치를 OEM 코드페이지로 파싱하므로 한글 배치는 토큰 깨짐 위험이 있다 — 이 파일들을 수정할 일이 생기면 ASCII로 바꿀 것.

---

## 미추적 참고 파일 (코드 복사 금지)

`autokey_e_simple.cpp`, `autokey_e_complex.cpp.backup`은 git에 없고 `build.bat`도 빌드하지 않는 과거 변종이다. **둘 다 worker 스레드가 없고 키 전송 자체를 `WM_TIMER`(50ms)로 한다** — 현행의 핵심 불변식을 따르지 않는다(현행의 `WM_TIMER`는 설정 저장 전용이며 전송과 무관하다).

- `autokey_e_simple.cpp` — LoL 감지·단일 인스턴스·모니터 배치가 모두 없는 최소 변종.
- `autokey_e_complex.cpp.backup` — LoL 감지는 현행과 동일하게 갖고 있고(psapi:5, `LOL_PROCESS_1/2`:39-41, `SetWinEventHook`:147-155, `IsLeagueOfLegendsProcess/Window`:415-463), 현행에 **없는** 기능이 하나 더 있다: 우선순위 키 인터럽트 — Q/W/R·1-7·Space·D/F/B/P/Tab 등이 눌린 동안 E 반복을 멈추고 떼면 재개(`PRIORITY_KEYS`:44-56, `priorityKeyCount`:322-346). 랜덤 간격 휴먼라이저(:29-33, :75-78)도 있지만 `GetRandomInterval()`은 호출부가 없는 **데드코드**이고 실제 간격은 고정 50ms(:248,341,362)다 — 지터는 2026-10-27에 현행 구조로 다시 구현됐으니("타이밍" 절) 이쪽을 참고할 이유가 없다. 단일 인스턴스·모니터 배치·`LLKHF_INJECTED`는 전부 없다.

**여기서 코드를 복사해 오면 이미 해결된 프리즈/렉이 그대로 재발한다.** 두 변종은 이 문서가 실패로 규정한 구조를 그대로 담고 있다:
- `LLKHF_INJECTED` 대신 `isSendingKey` 자체 플래그 (simple:41,231 / backup:72,306) — 타이밍 문제로 폐기된 방식
- hook 콜백 안에서 `SendEKey()`·`KillTimer`·`UpdateUI` 직접 호출 (simple:218-219,239,250,252 / backup:292-293,330,341,360-362) — 재진입 프리즈 경로
- `SendEKey()` 내부 동기 `Sleep()` (simple:277 / backup:388) — 2026-04-05 렉의 원인 구조

기능 아이디어만 참고하고 구현은 반드시 현행 구조(hook은 플래그 조작만 → worker가 전송, UI는 PostMessage)로 다시 쓸 것.

또한 두 변종도 현행과 같은 창 클래스명/타이틀 `P2`를 쓰지만(simple:53,68 / backup:97,112) 단일 인스턴스 종료 블록은 현행에만 있다. 변종을 띄워둔 채 `autokey_e.exe`를 실행하면 변종이 조용히 종료되고, 반대 순서로는 아무도 막지 않아 두 인스턴스가 각자의 훅으로 E를 이중 전송한다. 변종을 실험용으로 빌드한다면 클래스명·타이틀을 먼저 바꿀 것.

---

## 폐기된 접근 (재시도 금지)
- **키 전송에 `SendInput` 사용** — hook callback 컨텍스트에서 입력이 무시됨. `keybd_event`로 복원 (af1c4be, 2026-04-05). 현재는 worker에서 보내므로 전제가 다르지만 되돌리려면 장시간 홀드 실측 검증 후에만.
- **hook → `PostMessage`로 키 전송 위임** — E 입력 지연/누락 발생 (627579b, 2026-04-05). `PostMessage(WM_APP+1)`은 UI 갱신 요청 전용이며 키 전송 경로로 재사용하지 말 것.
- **hook callback 내 `keybd_event` 직접 호출** — hook chain 동기 재진입으로 5~7회 만에 시스템 전체 프리즈 (438cdf0, 2026-04-06). 회귀 검증은 반드시 E를 길게 눌러서 할 것.
- **`WM_TIMER` 기반 반복 전송** — worker 스레드 재설계 때 완전 폐기 (ee05c54, 2026-04-08). 키 전송을 타이머로 되돌리지 말 것. **예외**: 2026-10-27부터 `SetTimer`/`WM_TIMER`가 **설정 저장 디바운스 전용**으로 다시 존재한다(`SAVE_TIMER_ID`, autokey_e.cpp:70, :304, :746-754). 키 전송과 무관하며 핸들러는 `SaveSettings()`만 부른다 — 이 타이머에 전송 로직을 얹지 말 것.
- **`timeBeginPeriod(1)` + `-lwinmm`** — LoL 인게임 렉 (2668c37, 2026-04-08). 에러 로그 참조.
- **하이브리드 `Sleep` + `QueryPerformanceCounter` 스핀** — 정확도는 전 방식 중 1위(sd 0.014ms)지만 코어 1개를 **97~100% 태운다**(실측). 게임 대상 유틸에서 채택 금지 (2026-08-03 측정).
- **HR waitable timer의 periodic 모드** — 평균 주기는 더 정확하나 지터가 one-shot의 3배. one-shot 재arm 방식을 쓸 것 (2026-08-03 측정).
- **`-O` 수준 변경 / `-flto`** — `-O0`조차 `-O3`와 구분되지 않는다(155.1~155.8ms, 스프레드 0.6%). 핫 패스인 `LowLevelKeyboardProc`·`WorkerThreadProc`의 명령어 스트림이 `-O2`와 `-O3`에서 바이트 단위로 동일하다. 모든 실행 경로가 `Sleep` 또는 Win32 시스템 콜에 묶여 있어 컴파일러가 최적화할 계산이 없다 (2026-08-03 측정).
- **`MapVirtualKey` 스캔코드 캐싱** — 실측 219ns/호출로 반복 주기의 0.00014%. A/B 측정에서 부호가 뒤집혀 노이즈에 묻힌다. 성능 목적으로 하지 말 것 (2026-08-03 측정).
- **`keybd_event` x2 → `SendInput` 일괄 전송** — 주기 차이의 부호가 런마다 뒤집힘 = 차이 없음 (2026-08-03 측정).
- **injected 키 경로에서 `CallNextHookEx` 생략** — 수치상 이득은 가장 크지만 다른 앱의 훅 체인을 조용히 망가뜨린다.

### 제거된 선례 — Q/W/R 스킬 미발동
E 연타 중 스킬이 안 나가는 증상은 이 앱의 재발 가능한 핵심 리스크다. 27c4407(2025-09-20)에 우선순위 키 시스템(`PRIORITY_KEYS` + 카운터)이 구현된 적 있으나 350d0de(2025-10-25)의 대규모 단순화 리라이트에서 사라졌고, 커밋 메시지에 제거 사실이 없어 의도적 폐기인지 불명이다. 같은 증상이 보고되면 `git show 27c4407`로 선례를 먼저 볼 것 — 단 당시 구현은 hook 안에서 `SetTimer`를 조작하는 방식이라 현행 worker 구조에 그대로 이식할 수 없고, 인터럽트 조건은 플래그로만 옮기고 판정·전송은 worker에 남겨야 한다.

같은 리라이트에서 함께 사라진 랜덤 간격(`std::mt19937`)은 **2026-10-27에 다시 들어왔다** — 단 구현은 다르다(worker 로컬 xorshift32 + `IntervalToTicks`의 ±6ms 지터, "타이밍" 절). 당시 코드의 `GetRandomInterval()`은 호출부 없는 데드코드였으므로 그쪽을 참고하지 말 것.

---

## 에러 로그
<!-- 형식: | 날짜 | 에러 | 원인 | 해결책 | -->

### 2026-10-27 - 휠 노치마다 ini 전체 재작성 → 훅 서비스 스레드 블로킹
- **증상**: 속도 슬라이더 초기 구현에서 마우스 휠을 한 노치 돌릴 때마다 설정을 즉시 저장했다. 휠을 굴리는 동안 키보드 훅을 서비스하는 메인 스레드가 반복 블로킹된다(리뷰 단계에서 발견, 사용자 신고 전)
- **원인 [Certain]**: 저장이 `WritePrivateProfileStringW`를 **키마다 1회** 호출하는 구조였다 — `[Keys]` 7개 + `[Options]` 2개 = **노치당 파일 전체 재작성 9회**. 호출당 실측 13.7~18ms이므로 노치 하나가 100ms 이상을 먹는다. 휠은 빠르게 수십 노치가 들어오는 입력인데 그 비용이 전부 `WindowProc` → 즉 키보드 훅과 같은 스레드에 쌓인다("아키텍처" 절의 직렬 실행). `LowLevelHooksTimeout`(~300ms)을 넘기면 키 차단이 조용히 건너뛰어지거나 훅이 해제된다
- **해결 (근본)**: 두 겹으로 고쳤다. (1) 저장 자체를 `WritePrivateProfileSectionW` **섹션당 1회**로 바꿔 9회 쓰기를 2회로 줄였다(실측 18.2 → 3.5ms, `SaveSettings` autokey_e.cpp:278-299). (2) 휠 경로는 즉시 저장하지 않고 **500ms 디바운스 타이머**로 모아 쓴다(`MarkSettingsDirty` autokey_e.cpp:302-305, `WM_TIMER` 핸들러 :746-754). 드래그는 `WM_LBUTTONUP`에서 1회(:768), 셀 토글은 이산 이벤트라 즉시(:816), 종료 시 미저장분은 `WM_DESTROY`에서 flush(:838) — 어느 경로로도 값이 유실되지 않는다
- **교훈**: **메인 스레드에 동기 파일 I/O를 넣을 때는 1회 비용이 아니라 호출 빈도를 먼저 보라.** 18ms는 클릭 한 번이면 무해하고 휠 한 번이면 치명적이다. 이 저장소에서는 그 스레드가 키보드 훅도 서비스하므로 "느린 UI"가 아니라 "입력 소실"로 나타난다
- **부수 확인**: `WritePrivateProfileStringW`의 비용은 디스크가 아니라 호출마다 파일 전체를 다시 파싱·기록하는 구조에서 나온다. 키가 7개로 늘면 비용도 7배가 된다 — 키를 더 추가할 때 이 경로를 다시 볼 것

### 2026-08-03 - CLAUDE.md 문서-코드 불일치 전수조사
- **증상**: 문서의 여러 수치·줄 번호·권고가 실제 코드/환경과 어긋나 있었음 (7개 축 전수 감사, 43건 확정)
- **주요 오류 [Certain]**: (1) 반복 주기 "~16ms"가 실측 ~155ms로 약 10배 틀림, (2) F2/F3 `return 1` 줄 번호 302·307 → 실제 303·308, (3) `LLKHF_INJECTED` 제거 시 "무한 재귀"가 아니라 기능 무력화, (4) "4개 실행 컨텍스트"가 4개 스레드로 오독됨(실제 2개), (5) `git-add-safe.bat` 사용 권고가 실제로는 해로움, (6) `.gitignore`에 `*.exe` 규칙이 없어 exe 추적 이유 설명이 틀림, (7) 학습 사항 2026-04-05가 7분 만에 철회된 접근을 "해결책"으로 기록
- **해결**: 위 항목 전부 정정하고 실측 근거(objdump import 목록, ctypes Sleep 측정, git check-ignore --no-index, cmd 장치명 판정)를 문서에 명시. "폐기된 접근" / "알려진 함정과 진단" 섹션 신설
- **교훈**: 문서의 수치는 근거 명령과 함께 적어야 한다. "~16ms"처럼 출처 없는 값은 검증되지 않은 채 다음 세션에 그대로 인용된다

### 2026-04-08 - timeBeginPeriod(1) 적용 후 LoL 인게임 렉 발생
- **증상**: worker thread 구조는 유지한 채 `timeBeginPeriod(1)` + worker idle `Sleep(1)`로 속도 최적화했더니 LoL 인게임 프레임 렉 발생
- **원인 [High]**: `timeBeginPeriod(1)`가 시스템 타이머 해상도를 1ms로 상승시켜 스케줄러 틱 빈도 급증. 거기에 worker thread가 초당 ~1000회 wake up하면서 게임 렌더 스레드의 프레임 페이싱을 교란. Windows 10 2004+의 per-process 스코프로도 게임 타이밍 민감도를 완전히 격리하지 못함
- **해결**: `ee05c54` 시점의 `autokey_e.cpp` / `build.bat`으로 롤백(현재 소스는 ee05c54와 diff 0). worker thread 구조와 `E_KEY_INTERVAL = 10`은 유지하되 `timeBeginPeriod`는 사용하지 않음
- **대가 [정정 2026-08-03]**: 원문에 "실질 반복 간격 ~16ms"라고 적었으나 **틀렸다**. ~16ms는 `Sleep(1)` 1회 비용일 뿐이고 실제 E 반복 주기는 그 10배인 **~155ms(약 6.5회/초)**다. "조금 느려짐"이 아니라 게임 렉 제거와 연타 속도를 맞바꾼 트레이드오프다 (최적화 적용 시 실측 초당 64회 → 100회였음, 5792df6 커밋 본문)
- **분리 검증 안 됨 [정정 2026-08-03]**: 롤백(2668c37)은 4가지를 한 번에 되돌렸다 — `timeBeginPeriod`/`timeEndPeriod`, `g_eScanCode` scan code 캐싱, idle poll `Sleep(1)`→`Sleep(2)`, `build.bat`의 `-lwinmm`. 따라서 "렉 원인 = timeBeginPeriod"는 [High] 추정이며 scan code 캐싱 단독의 무해성은 확인된 바 없다. 재시도한다면 한 번에 하나씩만 적용하고 LoL 인게임에서 실측할 것. (idle `Sleep(1)`→`Sleep(2)`는 timeBeginPeriod 제거 후에는 실효 없는 no-op이었다 — 실측 동일)
- **교훈**: 게임처럼 스케줄링에 민감한 타겟에는 시스템 타이머 해상도 변경을 지양. 기본 Sleep 정밀도를 수용하는 것이 안전

## 패턴 기록
<!-- 유용한 패턴, 해결책 기록 -->

- Low-Level Keyboard Hook에서 자신이 보낸 키 차단 방지: `LLKHF_INJECTED` 플래그 체크 (isSendingKey 플래그는 SendInput과 타이밍 문제 발생). 단 이 체크는 모든 주입 입력을 무시하므로 자동 테스트도 불가능해진다
- 기존 인스턴스 종료: `FindWindowW` + `SendMessage(WM_CLOSE)` + 강제 종료 fallback (`SendMessage`에는 타임아웃이 없다는 한계 동반)
- Hook 콜백은 플래그 조작만 수행: 키 전송은 worker thread로 위임 (hook callback 안에서 keybd_event 호출 시 hook chain 재진입 → 시스템 프리즈)
- Windows `Sleep()` 기본 정밀도는 ~15.6ms이며, **연속 호출 시 매 호출이 꼬박 1 tick을 소모**한다(`Sleep(1)`과 `Sleep(2)`가 실측 동일). 게임 대상 유틸에서는 `timeBeginPeriod(1)` 사용 금지 (2026-04-08 에러 로그)
- **"N회 Sleep 후 1회 동작" 루프에서 N은 wake 빈도가 아니라 동작 빈도만 바꾼다.** wake 빈도는 `1 / tick`으로 N과 무관하게 고정. 스케줄러 부하를 걱정할 때 봐야 할 수는 N이 아니라 tick당 wake 수다
- 게임 대상에서 15.6ms보다 빠른 주기가 필요하면 `timeBeginPeriod`가 아니라 `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`(Win10 1803+, one-shot 재arm). 시스템 타이머 해상도를 올리지 않고, 같은 전송률 기준 wake도 더 적다
- 성능 벤치는 앱의 실행 구조(자체 훅 + 메시지 펌프 + 실주입)를 그대로 복제해야 한다. 카운터 스텁만으로 낸 수치는 재현되지 않을 수 있다
- 프로세스 이름 조회는 `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` + `QueryFullProcessImageNameW(PROCESS_NAME_NATIVE)`가 `PROCESS_QUERY_INFORMATION|PROCESS_VM_READ` + `GetModuleFileNameExW`보다 6배 빠르고 열 수 있는 프로세스도 더 많다. 반환 경로가 device 형식일 뿐 구분자는 동일해 basename 로직은 그대로 쓸 수 있다
- 핫 패스에서 `MapVirtualKey` 반복 호출은 마이크로 최적화 수준 — 구조적 문제 아니면 건드리지 않음 (scan code 캐싱은 2668c37에서 다른 변경과 묶여 롤백됨)
- **거친 격자 위에서 평균을 맞추려면 확률적 반올림(디더링)**: 대기 단위가 ~15.5ms tick인데 사용자가 1ms 단위로 값을 고르면 고정 반올림은 평균을 격자에 붙여버린다. `목표/tick`의 소수부를 확률로 삼아 위/아래 tick을 고르면 **평균이 설정값과 일치**한다(`IntervalToTicks`). 단 범위 끝에서 클램프가 한쪽만 걸리면 평균이 안쪽으로 밀리므로 끝단은 따로 실측할 것
- **고빈도 입력의 영속화는 디바운스**: 휠·드래그처럼 초당 수십 번 들어오는 입력에 동기 저장을 달면 안 된다. 이산 이벤트(클릭)는 즉시 쓰고, 연속 입력은 타이머로 모아 쓰고, 드래그 종료/창 파괴에서 flush하는 세 경로를 함께 둬야 값이 유실되지 않는다 (2026-10-27 에러 로그)
- **그리기와 히트테스트는 좌표 함수 하나를 공유해야 한다**(`GridCellRect` / `SliderRect`). 각자 좌표를 계산하면 레이아웃 상수를 바꿀 때 한쪽만 따라가 "보이는 곳과 눌리는 곳이 다른" 버그가 난다
- **`FindWindow` 성공은 메시지 루프 준비 완료가 아니다**: 창 핸들은 `CreateWindowExW` 직후부터 유효하지만 메시지 루프는 더 뒤에 시작하므로, 찾은 직후 보낸 `PostMessage`는 유실될 수 있다. 외부 검증 스크립트는 창을 찾은 뒤 1초쯤 settle을 둘 것
- cmd에서 `nul`은 항상 존재하는 예약 장치명이라 `if exist nul`은 무조건 참. 파일 존재 확인에 쓰지 말 것
- `call`된 배치 파일의 bare `exit`는 호출자로 돌아가지 않고 cmd 세션 전체를 종료시킨다. 반드시 `exit /B`를 쓸 것

## 학습 사항
<!-- 세션별 학습 내용 -->

### 2026-04-05
- E키 부하/렉 원인: hook 콜백에서 `Sleep(1)` + `keybd_event` 동기 호출이 메시지 큐 축적 유발
- 같은 날 밤 시도 → 철회 순서: SendInput 전환 + 키 전송의 PostMessage 위임(583d48f) → PostMessage 위임은 E 입력 지연/누락으로 철회(627579b) → SendInput은 hook 콜백 안에서 입력이 무시되어 keybd_event로 복원(af1c4be). **이 날 살아남은 결론은 두 가지뿐**: down/up 사이 Sleep 제거, `isSendingKey` 자체 플래그 대신 `LLKHF_INJECTED` 사용
- 따라서 SendInput 재도입과 PostMessage를 통한 키 전송 위임은 둘 다 이미 실패한 경로다 ("폐기된 접근" 참조)

### 2026-04-08
- 최종 안정 구조: hook callback은 `isEPressed` 플래그만 조작, worker thread가 플래그 폴링 + `keybd_event` 전송. Hook 재진입과 키 전송이 완전 분리되어 장시간 반복에도 프리즈 없음
- 속도 최적화 시도 후 롤백: `timeBeginPeriod(1)` 조합이 LoL 인게임 렉 유발 → `ee05c54` 시점으로 롤백
- 교훈: "조금만 더 빠르게"라는 최적화는 게임 타이밍에 영향을 주지 않는 범위에서만 유효

### 2026-08-03 (Q/W/E/R 다중 키 확장)
- 단일 `isEPressed` bool → `KeyEntry g_keys[]` 배열로 전환. hook·worker·그리기가 전부 배열을 순회하므로 키를 늘릴 때 손댈 곳은 배열과 `KEY_COUNT`뿐이다
- 확장하면서 stuck key 비대칭을 `weBlockedDown`으로 함께 해소했다. 키가 1개에서 4개로 늘면 같은 함정도 4배가 되므로, 구조를 바꾸는 김에 고치는 것이 자연스러웠다
- 동시 홀드 정책은 **분할이 아니라 병렬**로 정했다. Q와 E를 같이 누르면 각각 32회/초씩 나간다(합쳐서 32회/초로 나누지 않음). 게임 입력 파이프라인 부하는 선택한 키 수에 비례한다
- R 켤 때 확인 대화상자를 넣었다가 같은 날 사용자 요청으로 제거했다. 경고는 강조색 표시로만 남는다
- 레이아웃은 창 크기를 하드코딩하지 않고 콘텐츠에서 역산하도록 바꿨다(위 "레이아웃" 절). 처음엔 `WINDOW_WIDTH/HEIGHT`를 직접 넘겨서 클라이언트 84x96에 콘텐츠가 79x55만 차지해 우측 14px·하단 24px가 비었다 — 프레임 두께를 눈대중으로 빼는 방식의 전형적 실패다
- 클라이언트를 콘텐츠에 맞춘 뒤에도 창의 3분의 1이 제목표시줄이었다. 이런 소형 HUD에서는 **콘텐츠보다 크롬이 더 큰 낭비**이며, `WS_POPUP`으로 바꾸고 이동·종료를 직접 구현하는 편이 실익이 크다. 최종 90x125 → 78x52 (높이 40% 감소)
- 검증 방법: `PostMessage(WM_LBUTTONDOWN)`으로 셀 좌표를 찍어 토글·ini 저장·재시작 유지를, `GetClientRect`로 실제 클라이언트 크기를 자동 검증했다. 반대로 **키 처리 경로 자체는 자동 검증 불가**(`LLKHF_INJECTED` 때문에 합성 입력이 훅을 통과)

### 2026-08-03 (성능 최적화)
- **wake 불변식 발견이 이 프로젝트 성능 논의의 핵심**: `E_KEY_INTERVAL`은 worker의 wake 빈도를 바꾸지 않는다(전 구간 ~64.7회/초 고정). 이걸 몰랐기 때문에 2026-04-08에 "더 빠르게"를 `timeBeginPeriod`로 풀려다 렉이 났다. 상수만 낮췄으면 스케줄러 부하 증가 없이 같은 목적을 달성할 수 있었다
- 채택: `E_KEY_INTERVAL` 10→2(6.47→32.4회/초), `WinEventProc` 프로세스 조회 경량화(6.6→1.05us), `UpdateUI` 상태 가드(126us→0.02us), `-Wunused-parameter` 경고 제거, exe strip 복구
- 보류: worker idle 폴링의 이벤트화(idle wake 64→0, 첫 E 지연 8.06→0.045ms). 이득은 실측으로 확인됐으나 worker 구조 변경이라 이 저장소의 프리즈 이력을 감안해 미적용. 적용 시 종료 경로 `SetEvent`가 **필수 동반**(누락하면 종료 시 1초간 전역 키 입력 정지)
- `timeBeginPeriod` 없이 15.6ms 장벽을 깨는 수단이 존재한다: `CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`. 2026-04-08에 렉을 낸 최적화가 달성한 속도가 초당 100회였는데, 이 타이머의 10ms 설정이 90.6회/초로 사실상 같은 지점을 글로벌 타이머를 건드리지 않고 낸다
- 벤치마크 방법론 교훈: 카운터 스텁 하니스와 **실주입 하니스(자체 WH_KEYBOARD_LL + 메시지 펌프 + 실제 keybd_event)** 는 결과가 다를 수 있다. 반증 에이전트 하나가 실주입 조건에서 N=1이 25회/초로 떨어진다고 보고했으나 충실 하니스에서 재현되지 않았다(64.3~64.4회/초, 2회). 성능 주장은 앱 구조를 그대로 복제한 하니스로 재현할 것
- 속도를 올릴 때의 진짜 리스크는 렉이 아니라 **Q/W/R 스킬 미발동**이다("제거된 선례" 참조). N을 더 낮추기 전에 인게임에서 이것부터 확인할 것

### 2026-08-03 (문서 감사)
- 전수 감사로 확인: 감사 시점 소스는 `ee05c54`와 완전히 동일(`git diff ee05c54 HEAD -- autokey_e.cpp build.bat` 결과 없음). 롤백이 온전했음
- 이 저장소의 실제 위험은 코드보다 **문서와 주변 스크립트**에 있었다 — 소스 주석(`E_KEY_INTERVAL`의 "milliseconds", idle Sleep의 "near-instant", 정리 순서 주석)이 세 군데 모두 부정확했고, `git-add-safe.bat`은 권고할수록 해로운 도구였다
- Windows 타이머 해상도는 Win10 2004+/11에서 프로세스 단위다. 다른 앱이 시스템 해상도를 1ms로 올려도 `timeBeginPeriod`를 호출하지 않은 프로세스의 `Sleep(1)`은 여전히 ~15.6ms (실측 검증)

### 2026-10-27 (1/2/3 키 · 속도 슬라이더 · 지터 · 연동 모드)
- **Sleep 루프를 그대로 유지한 것이 이번 설계의 핵심 선택이다.** 슬라이더로 31~200ms를 받으면 HR waitable timer가 더 자연스러워 보이지만, 그 구조는 인게임 검증이 안 된 미지의 경로이고 이 저장소는 타이밍 변경으로 렉을 낸 이력이 있다("폐기된 접근"). 그래서 대기 방식은 손대지 않고 **ms → tick 환산만 새로 넣었다** — wake 빈도(~64.7회/초)와 2 tick 하한이 그대로 유지되므로 2026-04-08 렉 기전과 인과 경로가 겹치지 않는다
- 거친 tick 격자에 ms 슬라이더를 얹는 문제는 **지터 + 디더링**으로 풀었다. 지터(±6ms)는 사람 같은 흔들림이 목적이고, 디더링은 그 흔들림을 tick 격자에 확률적으로 투영해 **평균 주기를 설정값에 맞추는** 장치다. 둘을 한 순수 함수(`IntervalToTicks`)에 모아 전역 없이 단위 테스트가 되게 했다 — 10만회 분포로 끝단 편향(31→32.5, 200→198.5)까지 수치로 확인했다
- **슬라이더를 owner-drawn으로 만든 이유는 미학이 아니라 치수다.** 72px 트랙에 SCROLLBAR 컨트롤을 넣으면 화살표 버튼이 트랙을 거의 다 먹는다. 직접 그리면 트랙 전체가 값 공간이 되고, 썸 대신 초록 채움의 끝을 지표로 쓰면 중앙의 숫자와 겹치지도 않는다. 창 폭을 늘리지 않고 기능을 하나 더 넣는 방법이었다
- **연동 모드는 hook을 전혀 건드리지 않고 worker 쪽에서만 끝냈다.** hook에 "다른 키도 눌린 것으로 간주" 같은 조건을 넣는 순간 단일 writer 불변식과 keyup 대칭(`weBlockedDown`)이 둘 다 흔들린다. worker가 `AnyKeyActive()` 한 번 보고 선택 키 전부를 쏘는 쪽은 **기존 불변식을 하나도 건드리지 않는다**
- 키를 4개에서 7개로 늘릴 때 창 폭은 그대로 뒀다 — 2행 그리드로 바꾸고 마지막 슬롯을 ★에 줬다. 폭이 열 수에서만 나오도록 상수를 정리해 둔 덕에 가능했고, `static_assert` 두 줄로 "마지막 슬롯 = ★"과 가드 비트 폭을 컴파일 타임에 묶었다
- **미검증 [Hypothesis]**: 실제 키 전송 출력, ★ 연동 모드가 게임에서 내는 결과, 인게임 성능 영향, 물리 마우스 휠 조작. `LLKHF_INJECTED` 때문에 합성 입력으로는 훅 경로를 밟을 수 없어 전부 수동 검증 대상이다. 자동 검증은 UI 경로(PostMessage 62항목)와 `IntervalToTicks` 분포(10만회)에 한정된다
- 검증 함정 하나를 새로 밟았다: `FindWindow`가 성공해도 메시지 루프가 아직 안 돌고 있으면 `PostMessage`가 유실된다. 1초 settle을 넣기 전까지 "토글이 안 먹는다"로 오진했다
