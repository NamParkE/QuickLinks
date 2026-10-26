# QuickLinks — 작업 안내

자주 쓰는 폴더·파일·웹 주소 바로가기 위젯. C++ Win32 + Direct2D/DirectWrite, MSVC, 외부 라이브러리 없음, 한국어 UI. 사용법은 [README.md](README.md), 설계와 모든 결정(시안 선택 이유 포함)은 [docs/설계.md](docs/설계.md).

- 저장소: https://github.com/NamParkE/QuickLinks (비공개, `main`). 릴리즈: v0.1.0 (2026-10-26)
- 로컬: `C:\Users\Administrator\WorkApps\QuickLinks`
- 커밋 메시지는 한국어. 커밋·푸시·릴리즈는 사용자가 요청할 때만.
- 형제 프로젝트: `..\ScanReceiver`(같은 C++ Win32 방식, 코드 스타일 참고), `..\ScreenShot`(C# WPF)

## 사용자 선호 (지킬 것)

- 답변·진행 안내는 **한국어**로. 디자인은 **시안을 여러 개 그려 보여 주고 고르게** 함(지금까지 레이아웃 4안 → B, 스타일 5안 → A, 세부 UI 9가지 → 추천안).
- 사용자가 같은 PC를 쓰는 중. **마우스·키보드 자동화(SendInput 등) 금지.** 확인은 아래 테스트 도구로, 직접 눌러 보는 확인은 사용자에게 부탁.
- 화면 캡처는 **위젯 창 영역만**. 위젯이 숨었거나 가려진 채 찍으면 사용자 화면이 찍힘(실제로 한 번 있었음) → `qltool capture`가 이제 거부함. 테스트 캡처는 확인 후 지울 것.
- 테스트는 사용자 목록(`%APPDATA%\QuickLinks`)을 건드리지 않게 **항상 `--config`로 따로** 실행.

## 파일 구조

| 경로 | 내용 |
| --- | --- |
| `links.h/.cpp` | 모델: 그룹·항목, 입력 판별(`ParseInput`), 화면 줄 목록(`Rows`), ini 읽기/쓰기. GUI 없음 |
| `shell.h/.cpp` | 열기(백그라운드 스레드), 클립보드·드롭 해석, 아이콘·존재 검사 작업 스레드(`IconWorker`) |
| `main.cpp` | 위젯 창 전부: 그리기(D2D), 입력, 메뉴, 드래그·드롭, 알림 막대, 트레이 |
| `app.rc`, `QuickLinks.manifest` | comctl32 v6, PerMonitorV2, asInvoker |
| `tests/links_test.cpp`, `run_tests.bat` | 모델 단위 테스트 (92개) |
| `tests/run_ui.ps1`, `sample.ini`, `empty.ini` | 테스트 인스턴스 실행 → 단계 실행 → 위젯 캡처 |
| `tools/qltool.cpp`, `build_qltool.bat` | 테스트 인스턴스 조작: capture / cmd / key / type / quit (메시지 PostMessage만, 실제 입력 아님). `run_ui.ps1`이 빌드하고 지움 |
| `spike/mica_spike.cpp` | 미카 검증용 (결과는 설계 2.5절). 다시 쓸 일 없으면 지워도 됨 |

## 빌드·테스트

```bat
build.bat                      :: build\QuickLinks.exe 하나뿐. 실행 중이라 잠겨 있으면 [LOCKED], 종료 코드 2
tests\run_tests.bat            :: 92 passed, 0 failed 여야 함. 끝나면 build\tests를 지움
```

```powershell
& tests\run_ui.ps1 -Out $env:TEMP\claude\ql_ui -Theme light -Steps 'capture:base','key:40','capture:sel','cmd:111','capture:add'
```

- `pwsh -File tests\run_ui.ps1`로 부르면 이 환경에서 아무 출력 없이 끝났음 → `&`로 실행.
- 테스트 인스턴스(`--config`)는 창 제목이 `QuickLinks (test)`, 단일 인스턴스 검사 안 함, **창을 띄울 때 포커스를 가져가지 않음**(한 번 사용자가 치던 글자가 테스트 창에 들어간 적 있음). `--theme light|dark`로 테마 고정.
- `cmd:<id>`의 번호는 `main.cpp` 위 enum (111 추가 줄 열기/닫기, 113 보이기/숨기기, 114 항상 위, 118 실행 취소, 119 다시 실행, 120 모두 선택, 121/122/123 테마 시스템/라이트/다크, 124 투명도 슬라이더 열기(`capslider:<이름>`으로 슬라이더 창 캡처, `key:`는 열린 슬라이더로 감), 125~128 시계 끄기/큰 숫자/한 줄/카드, 129 24시간제, 130 초 표시, 131/132 시계 위치 위/아래, 1000+n 선택 항목을 n번 그룹으로).
- 테마 우선순위: `--theme`(테스트) > ini `Theme` > Windows 앱 모드(`UseDark()`). `--theme` 없이 띄우면 `sample.ini`에 Theme가 없으므로 시스템(이 PC는 다크). `cmd:112`(붙여넣기)는 사용자의 실제 클립보드를 읽으므로 쓰지 말 것.
- **반투명 상태는 캡처하지 말 것**: 위젯 뒤의 사용자 화면이 섞여 찍힘. 투명도는 `GetLayeredWindowAttributes`로 알파 값을 읽어 확인(20% → 204, 50% → 127).
- `key:`로 보낸 키는 실제 Ctrl 상태가 없으므로 Ctrl 조합은 `cmd:`로 시험.
- 사용자 위젯이 기본 위치(주 모니터 오른쪽 위)에 떠 있으므로 `sample.ini`는 X=1300에 띄움(겹치면 캡처가 거부됨).
- **빌드 파일 규칙 (사용자 요청)**: 실행 파일은 `build\QuickLinks.exe` **하나만** 관리(다른 폴더에 따로 빌드하지 않음). 실행 중이거나 다른 이유로 덮어쓸 수 없으면(`[LOCKED]`) **사용자에게 먼저 물어보고** 처리. 허락을 받으면: 사용자 창(`QuickLinksWidget`/`QuickLinks`)에 `WM_APP+5`(종료)를 PostMessage → 빌드 → 다시 실행. 목록은 바뀔 때마다 저장되므로 남음.
- **테스트용 빌드는 확인 후 삭제** (사용자 요청): `run_tests.bat`은 `build\tests`를, `run_ui.ps1`은 `qltool.exe`(없으면 스스로 빌드)를 끝날 때 지움. `spike\build_spike.bat`으로 만든 `mica_spike.*`도 확인 후 지울 것. `build\`에는 `QuickLinks.exe`와 `obj\`만 남아야 함.
- `key:27`(Esc)은 목록에 포커스가 있으면 위젯을 숨김 → 그 뒤 캡처는 거부됨.
- **`key:13`(Enter)은 선택한 항목을 실제로 엶** — `sample.ini`의 실제 경로(`C:\Windows\win.ini` 등)를 고른 채 누르면 사용자 화면에 메모장·탐색기가 뜸(한 번 있었음). Enter 시험은 "없는 폴더 (테스트)"(Down 5번째 줄)에만.
- `-Win10Fonts`: Windows 11 글꼴이 없는 것처럼 Segoe UI / Segoe MDL2 Assets로 그림(`--win10-fonts`). Windows 10 화면 확인용.

### 릴리즈

1. `QuickLinks.manifest`의 `version`과 zip 이름의 버전을 올림
2. `build.bat` → `dist\QuickLinks-<버전>-win-x64.zip`에 `build\QuickLinks.exe`와 `README.md`를 넣음 (`dist/`는 git 무시)
3. `gh release create v<버전> dist\QuickLinks-<버전>-win-x64.zip --title "QuickLinks <버전>" --notes-file <노트>`

## 구조 메모

- **글꼴**: `PickFonts()`가 시작할 때 있는 글꼴을 고름. Windows 11 = Segoe UI Variable Text/Display + Segoe Fluent Icons, 없으면(Windows 10) Segoe UI + Segoe MDL2 Assets. 아이콘 코드 포인트는 두 글꼴이 같음. 한글은 어느 쪽이든 맑은 고딕으로 자동 대체.
- **그리기**: `ID2D1DCRenderTarget`을 `BeginPaint` DC에 묶고 `SetDpi(창 DPI)` → 좌표는 전부 DIP. 레이아웃 상수는 `main.cpp` 위쪽(설계 2.5절 값). 아이콘 글리프는 `Segoe Fluent Icons`, ClearType 색 번짐 때문에 글리프만 그레이스케일.
- **입력칸 3개**(검색, 추가 줄, 이름 바꾸기)는 표준 EDIT + `SetWindowSubclass`. 배경은 `WM_CTLCOLOREDIT`, 상자·밑줄은 부모가 그림.
- **아이콘**: `IconWorker`가 먼저 확장자 기반 일반 아이콘(디스크 접근 없음)을 보내고, 이어서 실제 아이콘(`IShellItemImageFactory`)과 존재 여부를 보냄. 로컬 경로 먼저, UNC 나중. 결과는 `WM_APP_ICON`으로 → WIC → D2D 비트맵 캐시(`g_icons`, 대상 문자열이 키).
- **열기**는 `shell::OpenAsync`(별도 스레드)라서 꺼진 NAS를 눌러도 위젯이 멈추지 않음. 실패하면 `WM_APP_OPENED`로 "찾을 수 없음" 막대.
- **드래그**: 목록 안 끌기는 마우스 캡처로 직접, 탐색기·브라우저에서 놓기는 OLE `IDropTarget`. 둘 다 `ComputeDrop`(놓을 그룹·위치·삽입선) 공유. 검색 중에는 목록 안 끌기 꺼짐.
- **창**: `WS_POPUP | WS_THICKFRAME` + `WM_NCCALCSIZE` 0 → 클라이언트가 창 전체, DWM이 둥근 모서리·테두리·그림자. 머리글 빈 곳은 `HTCAPTION`(이동), 가장자리 6 DIP는 크기 조절.
- **선택**: `g_sel`은 포커스 줄(바꿀 때는 꼭 `SetSel()` — 다중 선택을 지움), `g_picked`는 Ctrl/Shift 다중 선택(대상 문자열 집합, 비어 있으면 `g_sel` 하나). 동작은 `SelectedLinks()`(화면에 보이는 것만, 화면 순서)를 씀. 여러 항목 이동은 `Store::MoveMany`.
- **시계**: `Relayout()`이 디자인·위치에 따라 `L.clock` 영역을 잡고 검색칸·목록을 밀어냄(`L.fieldTop`, `listTop/listBottom`). `PaintClock()`/`PaintAnalog()`, 타이머는 `ScheduleClock()`(다음 초/분 경계에 맞춤), 다시 그릴 때는 `InvalidateClock()`으로 시계 부분만.
- **저장**: 바뀔 때마다 ini 전체를 임시 파일에 쓰고 `MoveFileEx`로 교체(UTF-16 LE BOM).

## 함정 (실제로 겪은 것)

- **끌기 중 `ReleaseCapture()`를 먼저 부르면 `WM_CAPTURECHANGED`가 끌기를 취소**해 버림 → 목록 안 끌기가 한 번도 적용되지 않았던 버그(2026-10-26 수정). `OnLButtonUp`은 `FinishDrag()`(상태를 비운 뒤 캡처 해제)를 먼저.
- 투명도 슬라이더는 별도 팝업 창(`QuickLinksSlider`)이고, 그릴 때 `g_rt`/`g_brush`를 잠시 슬라이더용으로 바꿔 끼워 같은 그리기 함수를 씀.

- `small`은 `rpcndr.h` 매크로 → 변수 이름으로 쓰지 말 것(ScanReceiver에서도 겪음).
- 미카(확장 프레임) 위에서는 GDI로 그린 EDIT 글자가 투명해짐 → 미카를 쓰지 않는 이유(설계 2.5절).
- 이 PC는 투명 효과 꺼짐 + 앱 다크 모드. 라이트 화면 확인은 `--theme light`.
- PowerShell에서 `Remove-Item`을 변수 경로와 같이 쓰면 이 환경의 보호 규칙에 막힐 때가 있음 → 따로 실행.

## 현재 상태와 남은 일 (2026-10-26)

- **Windows 10**: 글꼴 대체까지 완료(`-Win10Fonts`로 캡처 확인). 실제 Windows 10에서는 미확인 — 특히 `WS_THICKFRAME` + `WM_NCCALCSIZE` 0 방식에서 창 위에 얇은 흰 줄이 생기는지, 최소 1607 이상. 둥근 모서리는 Windows 10에서 없음(정상).

- 1단계 완료 + 일부 2·3단계(검색, 항목 끌기, 키보드, 아이콘·존재 검사, 다크 모드) + 사용자 요청으로 `Ctrl+Z`/`Ctrl+Y` 실행 취소·다시 실행(스냅숏 스택 `g_undoStack`/`g_redoStack`, 바꾸기 직전에 `Checkpoint()`; 새 편집 기능을 넣으면 꼭 호출). 사용자가 `build\QuickLinks.exe`를 실제로 쓰는 중. 자동 캡처로 확인한 화면: 목록·선택·추가 줄·이름 바꾸기·제거 알림·찾을 수 없음 막대·검색·빈 목록(라이트/다크).
- **사용자 확인 필요(자동 확인 불가)**: 마우스 호버 표시와 툴팁, 끌어서 순서·그룹 바꾸기, 탐색기·Chrome/Edge에서 끌어다 놓기(브라우저가 제목을 주는지), 우클릭 메뉴(다크 모드), 트레이 아이콘·메뉴, 창 이동·크기 조절, 다른 DPI 모니터(세로 모니터)로 옮기기.
- 남은 2단계: 그룹 끌어서 정렬, 전역 단축키(`Ctrl+Alt+Space`), 시작 시 실행, 설정 창.
- 3단계: 앱 아이콘 시안(지금은 실행 중에 그린 임시 아이콘, exe 파일 아이콘 없음), 메뉴 아이콘, 투명도 UI, 내보내기/가져오기, 미카 재시도, 배포 zip.
- GitHub 비공개 저장소 생성, v0.1.0 릴리즈 완료(2026-10-26).
