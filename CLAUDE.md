# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 필수 규칙
1. **에러 발생 시** 이 파일의 "에러 로그" 섹션에 기록 (원인, 해결책, 날짜)
2. **새로운 패턴 발견 시** "패턴 기록" 섹션에 문서화
3. **세션 종료 전** "학습 사항" 섹션에 정리

---

## 프로젝트 개요
- **이름**: Press2 (AutoKey E) — 창 클래스명/타이틀은 모두 `P2`
- **목적**: League of Legends E키 자동 반복 입력 유틸리티
- **언어**: C++17 / Win32 API, 로직 전체가 단일 소스 파일 (`autokey_e.cpp`)
- **출력**: `autokey_e.exe` (git에 추적됨 — "빌드 산출물 커밋" 참조)

### 동작
- E키를 누르고 있는 동안 worker 스레드가 E를 반복 전송 (원본 E는 hook에서 차단)
- F2: 활성화 / F3: 비활성화 (두 키 모두 시스템으로 전달되지 않고 소비됨)
- LoL 포그라운드 진입 시 자동 ON, 이탈 시 자동 OFF
- 항상 위(topmost) 90x90 소형 창, 가장 왼쪽 모니터 좌상단, 작업표시줄 미표시

---

## 빌드

```batch
build.bat
```

`build.bat`의 3단계 파이프라인:
1. `windres resource.rc -O coff -o resource.res` — 아이콘/버전정보 리소스 컴파일
2. `g++ -Wall -Wextra -O3 -mwindows -static autokey_e.cpp resource.res -o autokey_e.exe -lcomctl32 -luser32 -lpsapi -std=c++17`
3. `strip autokey_e.exe` (strip 미존재 시 경고만 내고 계속)

주의: `build.bat`은 실패 시 `pause`로 입력을 대기하므로 비대화형 실행에서 멈출 수 있다. 컴파일 오류만 빠르게 확인하려면 2단계 g++ 명령을 직접 실행할 것.

**winmm은 링크하지 않는다** ("타이밍 제약" 참조).

### 아이콘 재생성
`icon.ico` / `icon.png`는 `create_icon.py`(Pillow)가 생성한다:
```
py create_icon.py
build.bat
```
`resource.rc:1`의 `1 ICON "icon.ico"`가 리소스 ID 1로 등록하고 `autokey_e.cpp:120`의 `LoadIcon(hInstance, MAKEINTRESOURCE(1))`이 이를 참조한다 — ID 변경 시 양쪽 모두 수정 필요.

### 테스트
자동화된 테스트 없음. 검증은 실행 후 수동 확인: F2/F3 토글, LoL 포커스 시 ON/OFF 자동 전환, E 홀드 시 반복 입력, 장시간 홀드 시 프리즈 없음.

---

## 아키텍처 — 핵심 불변식

로직 전체가 `autokey_e.cpp` 한 파일이지만, **4개의 실행 컨텍스트가 엄격히 분리**되어 있다. 이 분리가 이 프로젝트의 유일하고 핵심적인 설계이며, 경계를 넘으면 시스템 입력 파이프라인이 프리즈한다.

1. **Hook 콜백** `LowLevelKeyboardProc` (autokey_e.cpp:285) — 전역 플래그(`isEnabled`, `isEPressed`) 조작만 수행. 키 전송(`keybd_event`/`SendInput`) 금지, UI 호출 금지. UI 갱신이 필요하면 `PostMessage(hMainWnd, WM_APP+1, 0, 0)`로 비동기 위임.
2. **Worker 스레드** `WorkerThreadProc` (autokey_e.cpp:350) — 플래그를 폴링해 `SendEKey()` 호출. **유일한 키 전송 지점**.
3. **WinEvent 콜백** `WinEventProc` (autokey_e.cpp:419) — 포그라운드 창 변경 감지 후 자동 ON/OFF. 여기서도 플래그 조작 + PostMessage만.
4. **메시지 루프 / `WindowProc`** (autokey_e.cpp:224) — UI 갱신과 GDI 리소스(브러시/폰트) 소유·해제 담당.

**재진입 차단**: worker 스레드가 보낸 E는 hook 최상단(autokey_e.cpp:293)에서 `LLKHF_INJECTED` 플래그로 즉시 통과시킨다. 이 체크가 없으면 무한 재귀. (과거 `isSendingKey` 자체 플래그 방식은 `SendInput` 비동기 타이밍 때문에 실패 — 패턴 기록 참조.)

**원본 키 차단**: hook이 `return 1`을 반환하면 원본 키가 시스템으로 전달되지 않는다 (F2/F3: autokey_e.cpp:302,307 / E down·up: autokey_e.cpp:316,323).

### 타이밍 제약 (건드리지 말 것)
- **`timeBeginPeriod` 사용 금지** — LoL 인게임 프레임 렉 유발 (에러 로그 2026-04-08). winmm 링크도 하지 않는다.
- `E_KEY_INTERVAL = 10` (autokey_e.cpp:23)은 "10ms"가 아니라 **`Sleep(1)` 반복 횟수**다 (autokey_e.cpp:355-358). `timeBeginPeriod` 미적용 상태의 `Sleep(1)`은 실측 ~1-15.6ms이므로 실제 반복 주기는 상수값을 ms로 읽은 값보다 크다. 속도 조절은 이 상수만 조정하고, 시스템 타이머 해상도는 절대 변경하지 말 것.
- idle 폴링은 `Sleep(2)` (autokey_e.cpp:361). 반복 대기 루프 내부에서도 매 tick마다 `isEPressed`/`isEnabled`를 재확인해 키를 뗀 즉시 멈춘다.

### 단일 인스턴스
`FindWindowW(L"P2", L"P2")` → `WM_CLOSE` → 최대 2초 대기 → `TerminateProcess` 폴백 (autokey_e.cpp:87-111). **창 클래스명과 타이틀이 둘 다 `"P2"`**여야 동작하므로, 어느 한쪽만 바꾸면 기존 인스턴스를 찾지 못해 중복 실행된다.

### LoL 감지
프로세스명 우선 (PSAPI `GetModuleFileNameExW` → `League of Legends.exe` / `LeagueClient.exe`), 실패 시 창 제목 부분일치(`League of Legends (TM) Client`)로 폴백 (autokey_e.cpp:368-416). 프로세스 핸들 열기가 권한 문제로 실패하는 경우를 제목 폴백이 커버한다.

### 창 배치
`EnumDisplayMonitors`로 가장 왼쪽 모니터의 좌상단 좌표를 찾아 배치하고, 열거 실패 시 (0,0) 폴백 (autokey_e.cpp:124-140). `WS_EX_TOPMOST | WS_EX_TOOLWINDOW`.

---

## git 워크플로

### Windows `nul` 파일 문제
이 저장소에는 Windows 예약 장치명 `nul` 파일이 생성되어 git을 깨뜨리는 문제 전용 인프라가 있다:
- `.git/hooks/pre-commit` — 커밋 전 `nul` 존재 확인 후 제거 시도 (샘플이 아닌 **활성 훅**)
- `git-add-safe.bat` — 정리 후 `git add -A`, 실패 시 재정리·재시도하는 래퍼
- `fix-nul.ps1` — 확장 경로(`\\?\`)로 삭제하는 PowerShell 폴백
- `.gitignore`의 `nul` / `NUL` 항목

`git add`가 원인 불명으로 실패하면 `git-add-safe.bat`을 쓰거나 `fix-nul.ps1`을 먼저 실행할 것. 일반 `del nul`은 파일이 아닌 장치명으로 해석되어 실패한다.

### 빌드 산출물 커밋
`autokey_e.exe`와 `resource.res`는 **git에 추적된다** (`.gitignore`에 `*.res`가 있어도 이미 추적 중인 파일에는 무시 규칙이 적용되지 않음). 소스 수정 후에는 `build.bat`을 돌려 재빌드된 exe까지 커밋하는 것이 이 저장소의 관례다 (`git log --oneline -- autokey_e.exe` — 소스 변경 커밋과 별개로 `build:` 타입 exe 커밋이 누적되어 있다).

---

## 미추적 참고 파일
`autokey_e_simple.cpp`, `autokey_e_complex.cpp.backup`은 git에 없는 과거 변종이며 `build.bat`이 빌드하지 않는다. simple 쪽은 worker 스레드 대신 `WM_TIMER`(50ms) 기반이고 PSAPI/LoL 감지가 없어 현재 설계와 다르므로, 참고 시 현행 코드와 혼동하지 말 것.

---

## 에러 로그
<!-- 형식: | 날짜 | 에러 | 원인 | 해결책 | -->

### 2026-04-08 - timeBeginPeriod(1) 적용 후 LoL 인게임 렉 발생
- **증상**: worker thread 구조는 유지한 채 `timeBeginPeriod(1)` + worker idle `Sleep(1)`로 속도 최적화했더니 LoL 인게임 프레임 렉 발생
- **원인 [High]**: `timeBeginPeriod(1)`가 시스템 타이머 해상도를 1ms로 상승시켜 스케줄러 틱 빈도 급증. 거기에 worker thread가 초당 ~1000회 wake up하면서 게임 렌더 스레드의 프레임 페이싱을 교란. Windows 10 2004+의 per-process 스코프로도 게임 타이밍 민감도를 완전히 격리하지 못함
- **해결**: `ee05c54` 시점의 `autokey_e.cpp` / `build.bat`으로 롤백. worker thread 구조와 10ms 반복 간격은 유지하되 `timeBeginPeriod`는 사용하지 않음 (실질 반복 간격 ~16ms로 조금 느려지지만 게임 렉 없음)
- **교훈**: 게임과 같은 프로세스 스케줄링에 민감한 타겟에 대해서는 시스템 타이머 해상도 변경을 지양. 기본 Sleep 정밀도를 수용하는 것이 안전

## 패턴 기록
<!-- 유용한 패턴, 해결책 기록 -->

- Low-Level Keyboard Hook에서 자신이 보낸 키 차단 방지: `LLKHF_INJECTED` 플래그 체크 (isSendingKey 플래그는 SendInput과 타이밍 문제 발생)
- 기존 인스턴스 종료: `FindWindowW` + `SendMessage(WM_CLOSE)` + 강제 종료 fallback
- Hook 콜백은 플래그 조작만 수행: 키 전송은 worker thread로 위임 (hook callback 안에서 keybd_event 호출 시 hook chain 재진입 → 시스템 프리즈)
- WM_TIMER 핸들러에서 SetTimer 재호출 불필요: 같은 ID의 타이머는 자동 반복
- Windows `Sleep()` 기본 정밀도는 ~15.6ms. 게임 대상 유틸에서는 `timeBeginPeriod(1)` 사용 금지 (LoL 인게임 프레임 스케줄링 교란 확인됨, 2026-04-08 에러 로그 참조). 기본 정밀도 수용
- 핫 패스에서 `MapVirtualKey` 반복 호출은 마이크로 최적화 수준 — 구조적 문제 아니면 건드리지 않음

## 학습 사항
<!-- 세션별 학습 내용 -->

### 2026-04-05
- E키 부하/렉 원인: Hook 콜백에서 Sleep(1) + keybd_event 동기 호출이 메시지 큐 축적 유발
- 해결: Sleep 제거, keybd_event->SendInput, Hook에서 PostMessage 비동기 처리로 전환
- SendInput + isSendingKey 플래그 타이밍 문제: SendInput은 비동기적으로 hook 트리거하므로 플래그가 false가 된 후 hook이 호출됨. LLKHF_INJECTED 플래그로 해결

### 2026-04-08
- 최종 안정 구조: hook callback은 `isEPressed` 플래그만 조작, worker thread가 플래그 폴링 + `keybd_event` 전송. Hook 재진입과 키 전송이 완전 분리되어 장시간 반복에도 프리즈 없음
- 속도 최적화 시도 후 롤백: `timeBeginPeriod(1)` + idle `Sleep(1)` 조합이 LoL 인게임 렉 유발 → `ee05c54` 시점으로 롤백. Sleep 실측 정밀도 ~16ms 수용이 게임 호환성 면에서 최적
- 교훈: "조금만 더 빠르게"라는 최적화는 게임 타이밍에 영향을 주지 않는 범위에서만 유효. 시스템 타이머 해상도 변경은 게임 대상 유틸에서 피해야 함
