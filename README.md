# 부엉이 파일 지킴이 (C_Inotify)

리눅스 **inotify**로 폴더 안의 파일 · 디렉터리 변화를 실시간으로 잡고,
기준 시점 이후 **무엇이 바뀌었는지**(생김 · 고쳐짐 · 사라짐, 바뀐 줄)를 보여 주는 C++ GUI 프로그램입니다.
이벤트가 생기면 담당 캐릭터가 통통 튀며 말풍선으로 알려 줘요.

| 캐릭터 | 맡은 일 |
|---|---|
| 부엉이 (가운데) | 감시 담당. 감시 중이면 눈 뜨고 두리번, 멈추면 쿨쿨 |
| 새싹이 (초록) | 생성 `IN_CREATE` |
| 뚝딱이 (주황) | 수정 `IN_CLOSE_WRITE` |
| 사르르 (빨강) | 삭제 `IN_DELETE` |
| 이사짱 (파랑) | 이동 · 이름 바꾸기 `IN_MOVED_FROM/TO` |

캐릭터를 누르면 실시간 목록이 그 종류만 보이게 걸러져요 (부엉이 = 전체).

## 빌드 · 실행 (Ubuntu / WSL)

```bash
make                    # 없는 패키지 설치 → 빌드 → 실행 (앱에서 "폴더 열기")
make DIR=~/내폴더       # 그 폴더를 바로 열어서 실행
make test               # 자가 검사 → "all passed"
```

설치되는 것: `g++` `cmake` `qt6-base-dev`(없으면 `qtbase5-dev`) `fonts-noto-cjk` — 이미 있으면 건너뜀.

## 기능

| 화면 | 하는 일 |
|---|---|
| 실시간 탭 | inotify 이벤트 타임라인. 하위 폴더 자동 감시, 새 폴더 · 옮긴 폴더도 따라감. 종류별 거르기 |
| 변경사항 탭 | 폴더를 연 순간(또는 "지금 상태를 기준으로" 누른 순간) 대비 생김(A) · 고쳐짐(M) · 사라짐(D) 목록, 파일을 고르면 바뀐 줄(+초록 / −빨강) |

- 빈 폴더도 추적합니다.
- 감시하는 폴더에는 아무 파일도 만들지 않아요. 기준 상태는 앱이 켜져 있는 동안 메모리에만 있어요 (1MB 넘는 파일은 줄 비교 생략).
- `.trackignore`에 `*.o`, `build` 같은 패턴을 한 줄에 하나씩 적으면 감시에서 빠집니다.

## 구조

```
src/Watcher.*         inotify 래퍼 (재귀 감시, 이동 짝 맞추기, 폴더 이동 시 watch 경로 갱신) — Qt 없음
src/Tracker.*         기준 상태 기억 · 바뀐 파일 목록 · 줄 비교(LCS) — Qt 없음
src/CharacterStage.*  캐릭터 무대 (QPainter로 직접 그림, 그림 파일 없음)
src/MainWindow.*      화면. inotify fd를 QSocketNotifier로 Qt 이벤트 루프에 연결
tests/core_test.cpp   Tracker + Watcher 자가 검사
```

## 발표 시연 순서 (예시)

1. `mkdir ~/demo && make DIR=~/demo` → 부엉이가 깨어남
2. 다른 터미널에서 `cd ~/demo` 후 `echo hi > a.txt` → 새싹이, `echo more >> a.txt` → 뚝딱이
3. `mkdir -p src/lib && touch src/lib/x.c` → 하위 폴더까지 자동 감시
4. `mv src code`(이사짱), `rm a.txt`(사르르)
5. 변경사항 탭에서 바뀐 파일 · 바뀐 줄 확인 → "지금 상태를 기준으로"로 초기화
