# 이 저장소에서 일할 때

WISH60 HE 홀이펙트 키보드 펌웨어. HPM5361(RISC-V, 400MHz) 위에서 돌고, 보드의 IAP
부트로더는 그대로 두고 `0x80020000` 부터가 앱 자리다.

## 먼저 읽을 것

**[firmware/wish-he/docs/README.md](firmware/wish-he/docs/README.md) 의 "지금 차례"** —
이어서 할 일의 **유일한 목록**이다. 대화 맥락은 압축되면 사라지므로 여기가 인계 지점이다.
무언가를 끝내면 거기서 지우고, 알아낸 것은 거기에 남긴다.

그 아래 18편이 "왜 그렇게 됐는지" 를 담고 있다. 코드를 고치기 전에 관련 편을 본다.

## 고쳤으면 무엇을 확인하나

**[firmware/wish-he/docs/checklist.md](firmware/wish-he/docs/checklist.md)** 에 목록이
있다. 아래 세 절은 그중 가장 자주 빠뜨리는 것을 풀어 쓴 것이다.

## 짐작하지 말고 잰다

이 프로젝트에서 가장 값진 습관이다. **"굳는다", "값이 이상하다" 같은 증상은 원인을
알려주지 않는다.** 웹 도구로만 보면 펌웨어가 잘못한 것인지 앱이 잘못한 것인지 못 가린다.

```sh
python3 tools/dev.py cli "keys time"      # 스캔 단계별 시간
python3 tools/dev.py cli "keys noise 60000"
python3 tools/dev.py stat                 # 진단 카운터
python3 tools/dev.py burst                # 왕복 시험 — 응답 소실·어긋남
```

짐작으로 고치다 여러 번 빗나갔고, 직접 재서 한 번에 갈린 적이 많다. **가설을 세웠으면
고치기 전에 재서 확인한다.** 아니라고 나오면 그 사실도 문서에 남긴다 — 안 되는 길을
적어 두는 것이 되는 길만큼 값지다.

## 기능을 더하거나 고쳤으면 성능도 잰다

**굽기 전에 한 번, 굽고 나서 한 번.** 예외 없다.

```sh
python3 tools/dev.py cli "qmk reset"    # 통계는 누적이다 — 재기 전에 0 으로
python3 tools/dev.py cli "qmk info"     # keyboard_task / rgb_task 의 avg·max·초과
python3 tools/dev.py cli "keys time"    # 스캔 단계별 시간
```

스캔이 25us 대에서 도는 실시간 루프다. 기능 하나가 조용히 태스크 시간을 늘려도 웹
화면에서는 안 보이고, 한참 뒤 `scan over` 카운터로만 드러난다 — 그때는 무엇이 늘렸는지
가릴 수가 없다. 전후 값을 나란히 적어 둔다.

## 그리고 동작도 잰다

```sh
python3 tools/he_test.py        # 단계별 시험 — 표로 나온다
```

**성능 비교는 "느려졌나" 만 본다. 오입력과 키씹힘은 느려지지 않아도 난다.** 실제로
기준값 드리프트가 얕은 설정에서 키를 저절로 떼던 버그가 태스크 시간에는 한 글자도
안 나타났다 ([17편](firmware/wish-he/docs/17-verification.md)).

`keys inject` 가 ADC 값을 갈아 끼워 **실제 키를 안 누르고** 시험한다. 무언가를 고쳤으면
재현 절차를 시험에 **더해 둔다** — 손으로 확인하고 잊으면 반년 뒤 조용히 되살아난다.

★ 아직 안 고친 것은 `xfail` 로 둔다. **늘 빨간 검사는 아무도 안 본다.** 알려진 문제는
  "알려진 대로 재현됨" 이 통과이고, 재현이 안 되면 그때 알려 준다.

★ 시험이 장치 상태를 바꾸므로 **반드시 되돌린다.** 순서도 있다 — 설정을 먼저 되돌리고
  주입을 나중에 끈다. 반대로 하면 그 사이 리포트가 살아나 스턱 키가 호스트로 샌다.

## 손대면 안 되는 것

- **플래시 `0x080000 ~ 0x0C0000`** — 기존 데이터 영역이다. 우리 몫은 `0x0C0000` 위다
  (`HW_FLASH_USER_BEGIN`). `flashWrite()`/`flashErase()` 에는 주소 검사가 없으니 규율로 지킨다
- **부트로더 영역** — 바꿀 수 없다. 앱이 스스로를 검사하는 구조로 대신한다

## 깃

- **커밋은 지시할 때만 한다.** 작업이 끝났다고 자동으로 커밋하지 않고, "커밋할까요?" 로
  묻지도 않는다. 미커밋 변경이 쌓였으면 그 사실만 한 줄로 알린다
- 커밋 메시지에 Claude 서명(`Co-Authored-By` 등)을 넣지 않는다
- **어셈코드와 바이너리는 올리지 않는다.** `build/` 는 `.gitignore` 대상이다.
  배포용 이미지는 예외지만 그것도 이 저장소가 아니라 웹앱 쪽으로 나간다

## 문서와 주석

- **구현 자체를 설명한다.** 다른 제품·보드를 견주어 쓰지 않고, 제품·회사 이름도 쓰지
  않는다. 다른 펌웨어의 디스어셈을 인용하지 않는다
- 오류 표기는 `[E_]` 로 한다 (`[NG]` 아님)
- 주석은 **왜** 를 적는다. 무엇을 하는지는 코드가 말한다. 특히 **안 되는 길과 그 이유**를
  남긴다 — 다음 사람이 같은 데서 헤매지 않는다

## 코드 배치

초기화 함수(`hwInit()`, `xxxInit()`)를 **파일 맨 앞**에 둔다. 헬퍼는 위에 선언만 두고
정의는 뒤에 놓는다. 파일을 열면 그 모듈이 무엇을 켜는지가 먼저 보여야 한다.

## 굽기

```sh
cmake -S . -B build && cmake --build build -j8    # firmware/wish-he 에서
python3 tools/iap_update.py build/wish-he.bin      # USB 로. JTAG 불필요
```

빌드가 `wish-he-tag.bin` 을 하나 더 만든다 — 태그가 박혀 부팅 때 CRC 검사를 받는다.
원본 `.bin` 은 태그가 0 이라 검사를 건너뛰므로 개발 중에는 그쪽을 쓴다.

★ **링커 스크립트만 고치면 재링크가 안 걸린다.** `.ld` 를 만졌으면 `build/*.elf` 를
지우고 다시 빌드한다.

## 배포

`tools/make_release.py` 가 웹앱(`via-he`) 안에 **바로 쓴다.** 사본을 여기 두지 않는다 —
두 곳에 두면 반드시 갈라진다 (실제로 R38 / R1 로 갈라진 적이 있다).

버전은 `src/hw/hw_def.h` 의 `_DEF_FIRMWATRE_VERSION` 하나가 출처다. 장치가 보고하는
값과 배포 목록의 값이 같아야 한다.

## 설정 도구

웹앱은 별도 저장소다 — [chcbaram/via-he](https://github.com/chcbaram/via-he),
https://chcbaram.github.io/via-he/ 에서 돈다. 장치 쪽 프로토콜을 바꾸면 그쪽도 같이
고쳐야 한다 (`0xC0`~ 대역이 우리 확장, `0xFF60` 이 VIA 채널).

**메뉴(`keyboards/<보드>/menus.json`)를 고쳤으면** 정의를 다시 만들어 웹앱에 넘긴다.

```sh
python3 tools/gen_keymap.py --board <보드>            # layout-via.json 을 새로 만든다
cp keyboards/<보드>/layout-via.json  ../../../via-he/local-kbs/<보드>.json
cd ../../../via-he && bun scripts/add-local-kbs.ts    # 변환 + 등록 + 캐시 해시 갱신
```

★ **`public/definitions/v3/` 에 직접 복사하면 안 된다.** 편집용 원본과 앱이 읽는 형식이
  다르다(`keymap` vs `keys`+`optionKeys`). 그냥 넣으면 키보드가 붙는 순간 화면이 죽는다.
  자세한 것은 via-he 의 `CLAUDE.md` 에 있다.
