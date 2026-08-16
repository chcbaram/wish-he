# 남의 부트로더 위에 내 키보드 펌웨어 얹기

기성 홀이펙트 키보드 보드를 열어, 벤더 부트로더는 그대로 둔 채 그 위에서 도는
**내 펌웨어**를 만든다. 최종 목표는 8kHz 스캔과 래피드 트리거지만, 먼저
**완전히 동작하는 평범한 키보드**를 만든다.

- 보드: HPM5361 (RISC-V, 400MHz), 아날로그 MUX + ADC 로 64키, WS2812 83개
- 배선: **없다.** UART 헤더도 디버그 LED 도 없는 보드라 USB 하나로 굽고 디버깅한다
- 자리: 플래시 `0x80020000` 부터 384KB. 부트로더와 EEPROM 은 건드리지 않는다

```
ROM → 부트 헤더 → 부트로더(0x80003000) → 매직 확인 → 본 펌웨어(0x80020004)
```

---

## 목차

| 편 | 내용 | 상태 |
|---|---|---|
| [0. 하드웨어 사양](00-hardware.md) | 핀맵, 메모리 배치, 부트로더 인터페이스, 전류 예산 | 참고 |
| [1. 남의 부트로더 위에서 깨어나기](01-boot-on-iap.md) | 링커 재배치, 매직 워드, DCDC 무한 대기, **인터럽트를 여는 순간 죽는 버그** | 완료 |
| [2. 배선 없이 콘솔 만들기](02-console.md) | USB CDC + CLI, 죽은 뒤에도 읽는 `.noinit` RAM 링버퍼 | 완료 |
| [3. 스스로 부트로더로 돌아가기](03-reset-boot.md) | ROM API 플래시 쓰기, 소프트 리셋, **D-캐시가 거짓말한 사건** | 완료 |
| [4. LED 83개를 CPU 없이 흘리기](04-ws2812.md) | SPI 1바이트 = 1비트, HDMA 논블로킹, 채널 배분과 우선순위 | 완료 |
| [5. 센서가 내는 값 들여다보기](05-adc-scan.md) | ADC 시퀀스 + MUX, **스캔 한 바퀴 38us**, `SEQ_INT_EN`/`CONT_EN` 함정 | 완료 |
| [6. 눌렸다 말았다를 정하기](06-key-decision.md) | 러닝 극값 기준값, 12비트 전환, **드리프트가 안 돌던 세 가지 이유** | 진행 |
| [7. 드디어 키보드](07-keyboard.md) | HID 부트 키보드(IF0), 키맵, **바뀔 때만 리포트** | 완료 |
| [8. 전원을 꺼도 남기기](08-storage.md) | 플래시 저장 계층, 설정·보정 핑퐁, `keys cal` | 완료 |
| [9. JTAG 없이 굽기](09-iap-updater.md) | 부트로더 USB 프로토콜, 파이썬 업데이터, **HID 로 자동 진입** | 완료 |
| [10. QMK 를 matrix 아래에 얹기](10-qmk.md) | `quantum` 이식, `keysGetRow()` 를 그대로 받기, **디바운스를 안 쓴다**, EEPROM 지연 플러시, **정렬 assert 로 죽은 이야기** | 완료 |
| [11. VIA 붙이기](11-via.md) | `quantum/via.c` + `dynamic_keymap`, raw HID `0xFF60`. **라이브 트래킹은 장치가 민다**, 레이아웃도 장치가 준다 (JSON 없이) | 진행 |
| [12. 스캔을 62us 에서 33us 로](12-scan-speed.md) | 구조가 아니라 `-O0` 이었다. ILM 배치, **-O2 가 드러낸 잠복 버그 둘**, 우리 소스만 `-Werror` | 완료 |
| [13. 래피드 트리거](13-rapid-trigger.md) | 처리를 변환 대기에 숨기기, **3개 이동합(실측 1.31배)**, 방향 반전 판정, 바닥 보호, 설정을 전부 키별로 | 완료 |
| [14. LED 전류 리미터와 rgb_matrix](14-led-limiter.md) | 프레임 합산 제한, **전역 배율의 고유한 결함**, 밝기 상한은 리미터의 일이 아니다. **전류계로 재니 짐작한 셋이 전부 틀렸다**. rgb_matrix 얹기와 **HE 라서 만들 수 있는 효과 셋** | 완료 |

## 다음에 할 것

> 대화 맥락은 압축되면 사라진다. **여기가 이어서 할 일의 유일한 목록**이다.
> 새로 시작할 때 이 절을 먼저 읽고, 무언가를 끝내면 여기서 지운다.

### 지금 차례

- [ ] **RT 실사용 튜닝** — 재입력·입력 해제 거리의 경계 찾기.
      `keys learn` 이 눌림 에지마다 한 줄을 찍으므로 한 키를 한 번 눌러 몇 줄이
      나오는지로 잰다. 감으로 정하지 말 것

### 그다음

- [ ] **보정을 VIA HE 에서** — HE 탭의 `CALIBRATION` 섹션이 자리만 잡혀 있다
      (`todo: porting the CLI keys cal flow`).

      **보정이 두 가지라는 것부터 화면에 드러나야 한다.**

      | | 무압 기준값 | 바닥값 |
      |---|---|---|
      | 어떻게 아나 | 가만히 두면 저절로 | **끝까지 눌러야만** |
      | 우리 | 러닝 최대값이 늘 추적 — 자동 | `keys cal` |
      | 참고 보드 | 연결 시 자동, 수동 재보정 토글 | **안 한다.** 공칭 행정을 쓴다 |

      즉 우리 `keys cal` 은 참고 보드의 "재보정"과 다른 물건이다. 그쪽이 안 하는
      것을 하고, 그래서 mm 가 정확하다 — 이 보드 키별 스트로크 편차가 13% 라
      공칭값을 쓰면 그만큼 어긋난다. 대신 63키를 한 번씩 눌러야 한다.

      화면에 이렇게 적는다 — "보정 안 해도 동작한다 / 하면 mm 가 정확해진다 /
      63키를 한 번씩 끝까지 눌러야 한다".

      진행 순서:
      1. 펌웨어에서 수집 로직을 CLI 루프 밖으로 뺀다 (`keysCalStart/Update/Save/Cancel`).
         지금은 `cal` 명령 안에 갇혀 있어 HID 로 못 부른다. CLI 도 같은 핵을 쓴다
      2. HID 명령을 뚫는다 — 시작·저장·취소와 **키별 완료 비트맵** 읽기
      3. HE 탭에서 키보드 그림에 진행 상황을 칠한다. CLI 의 격자 뷰보다 낫고
         어느 키가 남았는지 한눈에 보인다
- [ ] **선택 버튼 자리** (보류 중) — 하위 메뉴 열 아래 / 내용 영역 오른쪽 여백 중
      택일. 키보드 위는 캔버스가 경로마다 미끄러져서 권하지 않는다
- [ ] **QMK 설정도 프로파일을 따르게** — 지금 프로파일을 따르는 것은 HE 설정·키맵·
      조명이고, `hold_okp`·기본 레이어·디버그는 공유다. 매크로와 NKRO 는 **일부러**
      공유로 둔다 (매크로는 글자 대신 치는 것, NKRO 는 장치의 성질).

      ★ 항목마다 옮길 것이 아니다. RAM 에 캐시되는 값은 `EECONFIG_BASE_SIZE` 37바이트
      한 덩어리에 다 모여 있다. 드라이버에서 그 구간의 주소만 프로파일 칸으로 접고,
      전환할 때 네 줄(`eeconfig_read_debug/keymap/default_layer`,
      `rgb_matrix_reload_from_eeprom`)을 다시 부르면 끝이다. 148바이트면 된다.

      ★ 위험한 자리다. 그 덩어리 맨 앞이 "이 EEPROM 이 쓸 만한가" 매직이라, 잘못
      건드리면 부팅 때 설정이 통째로 날아간다. 판정 로직을 먼저 읽고 손댈 것

- [ ] **프로파일에 이름·초기화** — 지금은 번호뿐이다. 백업 파일에도 프로파일을 담을
      자리 (지금은 HE 설정만 담고 키맵·조명은 안 담는다)
- [ ] **디버그 탭** — 지금 CLI 로만 보는 것들. 통로는 대부분 이미 열려 있다
      (스캔 주기·초과 횟수·`keyboard_task`·잡음 p-p·키별 스트로크·스냅샷/초)
- [ ] **11편 문서 갱신** — 6인터페이스 기술자, VIA 커스텀 메뉴, 포크 내용이 빠져 있다

### 규칙 (지키기)

- **키보드 그림에 무엇을 칠할지는 그 화면이 정한다** — `heSlice.overlayKeys`.
  화면에 들어갈 때 자기 것을 넣지 않으면 앞 화면의 표시가 그대로 남는다.
  `null` = "보여줄 것 없으니 선택을 보여라", `[]` = "칠할 것이 없다"
- **i18n 키는 세 벌(en·ko·ja)을 함께 채운다** — 이 앱은 영어 문장을 그대로 키로
  쓰는데, `en.json` 에 없으면 영어에서 키 문자열이 그대로 나온다. 실제로 툴팁
  4개가 그렇게 나가고 있었다. 낱말이 이미 다른 뜻으로 쓰이면(`Flash` → "플래시")
  문장을 늘리지 말고 `he.fw.flash` 처럼 이름 공간을 준다
- **키캡 각인 스타일은 화면이 정한다** — 키 하나하나의 데이터가 정하면 같은 화면
  안에서 들쭉날쭉해진다. HE 탭은 값이 하나도 없어도 `keyLabels` 를 늘 넘겨
  `compact` 를 켠다. 값은 그 자리에 들어왔다 나갔다 할 뿐이다
- **HID 명령을 더하면 옛 펌웨어를 생각한다** — 모르는 하위 명령이 오면 옛 펌웨어는
  엉뚱한 응답을 주고, VIA 는 에코가 어긋난 것을 오류로 잡는다. 폴링 안에서 쌓이면
  **진짜 오류를 못 본다.** 도구는 한 번 실패하면 그만 물어야 한다
- **HID 왕복은 한 줄로 세운다** — 요청 하나에 응답 하나다. 라이브 트래킹·보정
  폴링·전 키 읽기·슬라이더 쓰기가 동시에 나가면 응답이 엇갈린다. 부르는 쪽마다
  조심하게 하면 반드시 하나를 놓치므로 `send` 자체를 직렬화한다 (HE 탭은 그렇게
  되어 있다). 폴링 한 회차가 여러 왕복이면 `setInterval` 도 쓰지 않는다 — 주기
  안에 못 끝내면 회차가 겹친다. 끝난 뒤에 다시 건다
- **버튼 폭은 고정 숫자로 맞추지 않는다** — 번역마다 길이가 달라 반드시 어긋난다.
  `min-width` 로 바닥을 주되 가장 긴 번역이 넘지 않는 낱말을 고른다

### 확인만 하면 되는 것

- [ ] `keyboard_task` 최대 302us 가 나는 순간 특정 (153만 번 중 3회, 부팅·CLI 조작 때)
- [ ] 8kHz 리포트 실측 — 폴링 주기와 스캔 주기가 실제로 분리됐는지
- [ ] **`bInterval = 1` 을 유지할지** — 125us 폴링은 링크에 여유를 안 준다. USB
      전류계를 끼웠더니 키 입력이 간헐로 빠졌고 빼니 정상이었다
      ([14편](14-led-limiter.md)). 보통 키보드(1ms)라면 견뎠을 조건이다

---

번호는 순서가 아니라 편 번호다. 9편을 5편보다 먼저 했다 — **JTAG 배선 없이 굽게 되면서**
이후 시행착오 비용이 크게 줄었고, 스캔처럼 반복이 많은 작업 전에 해둘 값어치가 있었다.

VIA 는 키맵을 장치에 저장해야 하므로 **8편(EEPROM)이 먼저**다. 앱 뒤쪽
`0x80080000` 부터가 그 자리다 ([00-hardware.md](00-hardware.md) 2절).

**VIA 프로토콜을 직접 구현하지 않는다.** QMK 의 `quantum/via.c` 와
`dynamic_keymap.c` 가 이미 그 일을 한다. 우리가 만들 것은 `port/` 쪽 —
`matrix_scan()` 이 `keysGetRow()` 를 읽게 하고, EEPROM 백엔드를 8편의 플래시 계층에
연결하고, HID 리포트 송신을 우리 CherryUSB 로 돌리는 어댑터다. 그래서 **10편(QMK)이
11편(VIA)보다 먼저** 온다.

**GUI 는 따로 만들지 않는다.** 11편에서 VIA 웹앱을 포크해 HE 기능을 얹는 것이 목표이고,
래피드 트리거 튜닝 화면도 거기에 붙인다. 그전까지 필요한 관측은 CLI 로 충분하다
(`keys bar` / `keys watch`).

레이아웃은 이미 펌웨어 안에 바이너리로 들어 있다 — `keys_geo[]` 가 키마다
`{x, y, w, h, row, col}` 6바이트(1/4 키유닛)다. raw HID 로 이걸 내보내면
**웹 도구가 JSON 파일 없이 장치만 보고** 배치를 그릴 수 있다.

---

## 지금까지 확인된 것

```
cli# reset info
boot flag  : 0xFFFFFFFF   (앱 모드)
reset      : POWER

cli# ws2812 rainbow
83 LED, SPI1 8MHz + HDMA ch2, 2112 B / frame, 논블로킹

cli# reset boot
부트로더 진입 → USB 재열거

cli# keys time
scan   : 25796 회 / 초        주기 38 us        timeout 0
```

```
$ python3 tools/iap_update.py build/wish60-he.bin
앱이 실행 중이다 (0483:5304 WISH60-HE) — HID 로 부트로더 진입을 요청한다
부트로더 진입 확인
   86,888 / 86888 B  (100%)
완료 — 0.9초
```

- 벤더 부트로더를 보존한 채 **콜드 부팅으로 내 펌웨어가 400MHz 로 뜬다**
- 배선 없는 콘솔 — USB CDC + CLI, 그리고 죽은 뒤에도 읽히는 RAM 링버퍼
- 펌웨어에서 **스스로 부트로더로 돌아간다** — CLI `reset boot`, 또는 HID 명령
- LED 83개를 **CPU 개입 없이** — SPI + DMA 로 2112바이트 한 방
- 64키 전체를 **38us 에 훑는다** — 8kHz(125us) 예산의 30%
- 설정·보정이 전원을 꺼도 남는다 — 저장 **2~3ms**, 키별 스트로크 편차 16% 실측
- 기준값이 스스로 따라간다 — 누른 채 부팅해도, 온도가 변해도 (전 셀 잔차 ±1)
- **키보드로 동작한다** — 63키 매핑, HID 부트 키보드, 8kHz 폴링(125us) 확인
- **JTAG 없이 USB 로만, 사람 손 없이 굽는다** — HID 명령으로 부트로더에 넣고 87KB 0.9초
- 복구 경로가 셋 (JTAG · 업데이트 모드 · 강제 스트랩), 그리고
  **USB 로는 부트로더 자신을 덮어쓸 수 없다** — 벽돌이 되지 않는다

---

## 1. 개발 환경

### 1.1 툴체인 — xPack RISC-V GCC

이 프로젝트는 **hpm_sdk 에 의존하지 않는다.** 필요한 SoC 헤더·드라이버·링커
스크립트는 `src/bsp/` 아래로 들여왔다. 그래서 준비할 것은 컴파일러뿐이다.

```sh
# https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases
tar xf xpack-riscv-none-elf-gcc-13.4.0-1-darwin-arm64.tar -C ~/hdd/tools/
export HPM_RISCV_TOOLCHAIN_DIR="$HOME/hdd/tools/xpack-riscv-none-elf-gcc-13.4.0-1"
```

`HPM_RISCV_TOOLCHAIN_DIR` 은 **디렉토리든 `gcc` 실행 파일 경로든** 받는다. 안 잡히면
`PATH` 에서 찾고, 그래도 없으면 CMake 가 `RISCV Toolchain not found` 로 멈춘다.
HPMicro 공식 툴체인(`riscv32-unknown-elf-`)도 그대로 쓸 수 있다 —
`tools/hpmicro-riscv-gcc.cmake` 가 두 접두어를 모두 안다.

### 1.2 OpenOCD — HPMicro 빌드여야 한다

플래시 기록에는 **`hpm_xpi` 드라이버가 들어간 HPMicro 배포판**이 필요하다.
homebrew 의 OpenOCD 에는 그 드라이버가 없어 읽기·디버깅까지만 된다.

```sh
tar xf openocd-macos-arm64.tar -C ~/hdd/tools/hpmicro/
~/hdd/tools/hpmicro/openocd/bin/openocd --version   # 0.12.0+dev
```

cfg 는 `HPM_SDK_BASE` 를 요구하지 않도록 `tools/openocd/` 아래로 복사해두었다.
`-s tools/openocd` 로 검색 경로만 넣어주면 `[find ...]` 가 풀린다.

### 1.3 파이썬

pip 패키지는 쓰지 않는다. 업데이터는 **libhidapi 를 ctypes 로 직접** 부른다.

```sh
brew install hidapi          # tools/iap_update.py — 펌웨어 굽기
pip3 install pyserial        # CLI 접속 (선택)
```

### 1.4 프로브

| 용도 | 프로브 |
|---|---|
| **플래시 기록 · 디버깅** | FT2232 계열 (HPM5300EVK 온보드 디버거 등) |
| 메모리 읽기만 | J-Link — 단, 정품이어야 한다 |

> 클론 J-Link 은 SEGGER DLL 이 연결 자체를 거부한다(`0xFFFFFEFA`). 그리고
> **클론에 SEGGER 펌웨어 업데이트를 수락하면 안 된다** — 벽돌이 된다.
> OpenOCD 는 그런 검사가 없어 클론에서도 읽기는 된다.

## 2. 빌드

```sh
export HPM_RISCV_TOOLCHAIN_DIR="$HOME/hdd/tools/xpack-riscv-none-elf-gcc-13.4.0-1"
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j8
```

키보드 모델은 `keyboards/` 아래에 있고 `-DHW_KEYBOARD=<모델>` 로 고른다 (기본
`wish60-he-7u`). 레이아웃을 고쳤으면 먼저 생성물을 다시 만든다.

```sh
python3 tools/gen_keymap.py            # layout-kle.json -> layout-via.json + layout.h
```

산출물 `build/wish60-he.bin` 은 `0x80020000` 부터의 이미지다. 선두 4바이트가
`48 50 4D 0A`(`"HPM\n"`) 여야 부트로더가 인식한다.

```sh
xxd -l 8 build/wish60-he.bin
# 00000000: 4850 4d0a 9711 0680    HPM.....
```

## 3. 기록

### 3.1 USB — 평소에는 이걸 쓴다

```sh
python3 tools/iap_update.py build/wish60-he.bin
```

이 한 줄이 전부다. 앱이 돌고 있으면 **HID 로 부트로더 진입을 요청**하고, 열거를
기다린 뒤 기록한다. 87KB 에 약 0.9초. VSCode 에서는 `flash-usb` 태스크.

→ [09-iap-updater.md](09-iap-updater.md)

### 3.2 JTAG — 앱이 죽었을 때

```sh
openocd -s tools/openocd -f hpm5361-fw.cfg \
  -c "adapter speed 4000" -c "init" -c "reset halt" -c "flash probe 0" \
  -c "flash write_image erase build/wish60-he.bin 0x80020000 bin" \
  -c "verify_image build/wish60-he.bin 0x80020000 bin" \
  -c "reset run" -c "exit"
```

`0x80020000` 부터만 쓰므로 부트로더와 EEPROM 은 건드리지 않는다.

> `init` 다음에 `reset halt` 를 넣는다. 앱이 돌던 상태에서 바로 `halt` 하면
> 플래시 로더 실행이 실패하는 경우가 있다.

## 4. 디버그

### 4.1 CLI (USB CDC)

**macOS 에서는 반드시 `/dev/cu.*`** — `/dev/tty.*` 는 캐리어를 기다려 블록된다.

```sh
python3 - <<'PY'
import serial, time
p = serial.Serial("/dev/cu.usbmodem00011", 115200, timeout=0.3)
p.dtr = True; p.rts = True
time.sleep(0.3); p.reset_input_buffer()
p.write(b"help\r\n"); time.sleep(1)
print(p.read(8192).decode())
PY
```

명령: `help` · `reset info|boot|reset` ·
`ws2812 info|all|set|off|limit|prio|group|test|rainbow` ·
`keys layout|show|learn|key|base|map|watch|noise|dump|time|info` · `log` · `usb`

### 4.2 RAM 링버퍼 (JTAG)

USB 가 올라오기 전이나 죽은 뒤에도 로그를 읽는다. `.noinit` 이라 리셋해도 남는다.
주소는 빌드마다 바뀌므로 ELF 에서 뽑는다.

```sh
A=$(riscv-none-elf-objdump -t build/wish60-he.elf | grep -w log_ram | awk '{print "0x"$1}')
openocd ... -c "dump_image log.bin $A 0x810" -c "exit"

python3 -c "
import struct; d=open('log.bin','rb').read()
m,i,w,s=struct.unpack('<IIII',d[:16])
print((d[16+i:16+s]+d[16:16+i]).decode('utf-8','replace') if w
      else d[16:16+i].decode('utf-8','replace'))"
```

헤더는 `magic('RLOG') / index / wrapped / size` 4 워드다.

## 5. USB 구성

| 항목 | 값 |
|---|---|
| VID / PID | `0x0483` / `0x5304` |
| 제품 문자열 | `WISH60-HE` |
| 속도 | High Speed (480 Mb/s) |
| 클래스 | `0xEF / 0x02 / 0x01` (Misc / Common / **IAD**) |

| IF | 클래스 | 엔드포인트 | 용도 |
|---|---|---|---|
| 0 | HID (`0xFF60`) | `0x84` IN / `0x04` OUT, 32 B | 설정 · **부트로더 점프** |
| 1 | CDC 제어 | `0x83` INT | ┐ IAD 로 묶인다 |
| 2 | CDC 데이터 | `0x81` IN / `0x01` OUT | ┘ |

7편에서 부트 키보드가 들어오면 그것이 IF0 을 가져가고 나머지가 한 칸씩 밀린다 —
일부 BIOS 가 첫 인터페이스만 보기 때문이다. 호스트 도구는 인터페이스 번호가 아니라
**usage page 로 찾으므로** 밀려도 고칠 것이 없다.

PID 는 자체 키보드 저장소들과 같은 체계다. 이미 쓰는 값 — `0x5201`~`0x5207`,
`0x5210`, `0x5230`, `0x5301`~`0x5303`.

**윈도우 드라이버는 필요 없다.** Win10/11 은 IAD 복합 장치를 `usbccgp` 로 잡고 CDC
기능에 `usbser.sys` 를 자동 바인딩한다. HID 는 애초에 클래스 드라이버가 처리한다.
(Win7/8.1 은 CDC 자동 바인딩이 없어 INF 가 필요하다.)

## 6. 복구

앱이 매직을 갖고 있으면 부트로더는 계속 그리로 점프한다. 앱이 초기화 중 멈추면
자동 복구가 안 걸리므로 아래 중 하나를 쓴다.

| 경로 | 방법 | 조건 |
|---|---|---|
| **부트로더 핀** | 핀을 잡고 전원 인가 → 업데이트 모드 | **언제나** |
| **HID 점프 / `reset boot`** | `iap_update.py` 가 알아서 한다 | 앱이 살아 있을 때 |
| **JTAG** | `0x20000` 섹터를 소거 | 프로브를 붙일 수 있을 때 |
| **전체 복원** | 보드 원본 플래시 이미지(1MB)를 `0x80000000` 에 | |

> **부트로더 핀은 실전에서 검증됐다.** 부팅 초반에 리셋 루프로 빠지는 펌웨어를 구워
> USB 열거가 아예 안 되는 상태가 됐을 때, JTAG 없이 이 핀만으로 되굽었다.

### 부팅 경로에 플래시 작업을 넣을 때

한 번 여기서 브릭을 만들었다. "부팅이 N회 연속 실패하면 스스로 부트로더로 간다"는
안전망을 넣었는데, 그 판단이 **초기화 아주 이른 시점에서 플래시 소거·기록**을 한다는
게 문제였다. CLI 의 `reset boot` 는 완전히 초기화된 뒤라 검증됐지만 그 이른 경로는
아니었고, 기록이 실패하자 조용히 리턴해 **그대로 리셋 루프에 남았다.**

교훈은 둘이다.

- 부팅 경로의 플래시 작업은 **실패했을 때 어디로 가는지까지** 설계해야 한다
- 안전망이 덮는 범위를 정직하게 따져야 한다. 그 카운터는 `.noinit`(RAM)에 있어
  전원을 빼면 사라지므로 **따뜻한 리셋 루프만** 잡았다. 정작 위험한 "부팅 중 멈춤"은
  못 잡으면서 위험만 추가한 셈이다. 그 경우는 부트로더 핀이 이미 덮고 있었다

부트로더의 업데이트 명령은 기록 주소를 `0x80020000` 으로 하드코딩하므로
**USB 로는 부트로더 자신을 덮어쓸 수 없다.**

---

## 남은 확인거리

- [ ] **연속 전송 상한 5900/s** — 매 폴링마다 억지로 보내면 8000 을 못 채운다.
      HPM 컨트롤러는 dTD 를 하나만 걸 수 있어 매 마이크로프레임마다
      `완료 -> 인터럽트 -> 프라임` 왕복이 필요하다. 상용 펌웨어도 전송 경로가
      SDK 와 동일해 같은 상한이다. 실사용 지연에는 영향이 없다(리포트는 바뀔 때만
      나가고 즉시 프라임된다). 11편에서 dTD 체이닝으로 다룰지 판단
- [ ] PA09 가 보드상 어느 패드인지 실측
- [ ] `HPM5361_FLASH_XIP.ld` — 부트로더판을 만들며 남은 원본. 독립 부팅 빌드가
      필요 없으면 정리
