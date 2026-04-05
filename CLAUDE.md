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
- Hook 콜백에서 절대 Sleep/무거운 작업 금지: PostMessage로 비동기 처리
- keybd_event 대신 SendInput 사용: down+up을 하나의 호출로 원자적 전송
- WM_TIMER 핸들러에서 SetTimer 재호출 불필요: 같은 ID의 타이머는 자동 반복

## 학습 사항
<!-- 세션별 학습 내용 -->

### 2026-04-05
- E키 부하/렉 원인: Hook 콜백에서 Sleep(1) + keybd_event 동기 호출이 메시지 큐 축적 유발
- 해결: Sleep 제거, keybd_event->SendInput, Hook에서 PostMessage 비동기 처리로 전환
- SendInput + isSendingKey 플래그 타이밍 문제: SendInput은 비동기적으로 hook 트리거하므로 플래그가 false가 된 후 hook이 호출됨. LLKHF_INJECTED 플래그로 해결
