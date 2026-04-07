# Press2 (AutoKey E) - 작업 규칙

## 필수 규칙
1. **에러 발생 시** 이 파일의 "에러 로그" 섹션에 기록 (원인, 해결책, 날짜)
2. **새로운 패턴 발견 시** "패턴 기록" 섹션에 문서화
3. **세션 종료 전** "학습 사항" 섹션에 정리

---

## 프로젝트 개요
- **이름**: Press2 (AutoKey E)
- **목적**: League of Legends E키 자동 반복 입력 유틸리티
- **언어**: C++ (Win32 API)
- **빌드**: MinGW g++ (`build.bat`)
- **출력**: `autokey_e.exe`

## 주요 기능
- E키 누르고 있으면 10ms 간격으로 자동 반복 입력
- F2: 활성화 / F3: 비활성화
- LoL 프로세스 자동 감지 (포커스 시 자동 ON/OFF)
- 항상 위(topmost) 소형 창 (90x90), 왼쪽 모니터 상단 배치

## 기술 스택
- Win32 API (WinMain, WindowProc, Low-Level Keyboard Hook)
- WinEventHook (포그라운드 창 변경 감지)
- PSAPI (프로세스 이름 확인)
- MinGW g++ -O3 -mwindows -static

## 빌드 방법
```batch
build.bat
```

## 파일 구조
- `autokey_e.cpp` - 메인 소스 (단일 파일)
- `resource.rc` - 리소스 정의 (아이콘, 버전 정보)
- `icon.ico` / `icon.png` - 앱 아이콘
- `build.bat` - 빌드 스크립트
- `autokey_e.exe` - 빌드 산출물

---

## 에러 로그
<!-- 형식: | 날짜 | 에러 | 원인 | 해결책 | -->

(없음)

## 패턴 기록
<!-- 유용한 패턴, 해결책 기록 -->

- Low-Level Keyboard Hook에서 자신이 보낸 키 차단 방지: `LLKHF_INJECTED` 플래그 체크 (isSendingKey 플래그는 SendInput과 타이밍 문제 발생)
- 기존 인스턴스 종료: `FindWindowW` + `SendMessage(WM_CLOSE)` + 강제 종료 fallback
- Hook 콜백은 플래그 조작만 수행: 키 전송은 worker thread로 위임 (hook callback 안에서 keybd_event 호출 시 hook chain 재진입 → 시스템 프리즈)
- WM_TIMER 핸들러에서 SetTimer 재호출 불필요: 같은 ID의 타이머는 자동 반복
- Windows `Sleep()` 기본 정밀도는 ~15.6ms. `timeBeginPeriod(1)` 호출 시 1ms 정밀도 확보 (짝 맞춰 `timeEndPeriod(1)` 필수, `-lwinmm` 링크)
- 핫 패스에서 `MapVirtualKey` 반복 호출 피하기: 초기화 시 scan code 캐싱

## 학습 사항
<!-- 세션별 학습 내용 -->

### 2026-04-05
- E키 부하/렉 원인: Hook 콜백에서 Sleep(1) + keybd_event 동기 호출이 메시지 큐 축적 유발
- 해결: Sleep 제거, keybd_event->SendInput, Hook에서 PostMessage 비동기 처리로 전환
- SendInput + isSendingKey 플래그 타이밍 문제: SendInput은 비동기적으로 hook 트리거하므로 플래그가 false가 된 후 hook이 호출됨. LLKHF_INJECTED 플래그로 해결

### 2026-04-08
- 최종 안정 구조: hook callback은 `isEPressed` 플래그만 조작, worker thread가 플래그 폴링 + `keybd_event` 전송. Hook 재진입과 키 전송이 완전 분리되어 장시간 반복에도 프리즈 없음
- 속도 향상: `timeBeginPeriod(1)` 적용으로 `Sleep(1)` 실제 정밀도 ~15.6ms → ~1ms. 10ms 반복 간격이 실측 ~16ms에서 ~10ms로 개선 (초당 64회 → 100회, +56%)
- 첫 E 응답 지연: idle poll `Sleep(2)` → `Sleep(1)`로 단축, 실제 ~15ms → ~1ms
- 마이크로 최적화: scan code 전역 캐싱으로 SendEKey 호출 시 MapVirtualKey 제거
