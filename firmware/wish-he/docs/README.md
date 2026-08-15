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
| 5. 센서가 내는 값 들여다보기 | ADC 8채널 × MUX 8스텝, 원시값 덤프 | 예정 |
| 6. 눌렸다 말았다를 정하기 | 기준값 캘리브레이션, 임계값 + 히스테리시스 | 예정 |
| 7. 드디어 키보드 | HID 부트 키보드, 키맵 | 예정 |
| 8. VIA 붙이기 | raw HID `0xFF60`, 키맵 JSON | 예정 |
| [9. JTAG 없이 굽기](09-iap-updater.md) | 부트로더 USB 프로토콜, 파이썬 업데이터, 페이지 경계 함정 | 완료 |
| 10. 8kHz 로 올리기 | 스캔 예산, `bInterval=1`, 타이머 트리거 스캔 | 예정 |
| 11. 래피드 트리거 | ADC 값 → 거리, 액추에이션 조정 | 예정 |
| 12. LED 효과와 전류 리미터 | 프레임 전류 합산 제한 | 예정 |

9편을 5편보다 먼저 했다. **JTAG 배선 없이 굽게 되면서** 이후 시행착오 비용이 크게
줄었다 — 스캔처럼 반복이 많은 작업에 들어가기 전에 해둘 값어치가 있었다.

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

$ python3 tools/iap_update.py build/wish60-he.bin
84,840 B  ->  1.2 s
```

- 벤더 부트로더를 보존한 채 **콜드 부팅으로 내 펌웨어가 400MHz 로 뜬다**
- 배선 없는 콘솔 — USB CDC + CLI, 그리고 죽은 뒤에도 읽히는 RAM 링버퍼
- 펌웨어에서 **스스로 부트로더로 돌아간다** (`reset boot`)
- LED 83개를 **CPU 개입 없이** — SPI + DMA 로 2112바이트 한 방
- **JTAG 없이 USB 로만 굽는다** — 84KB 1.2초
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

산출물 `build/wish60-he.bin` 은 `0x80020000` 부터의 이미지다. 선두 4바이트가
`48 50 4D 0A`(`"HPM\n"`) 여야 부트로더가 인식한다.

```sh
xxd -l 8 build/wish60-he.bin
# 00000000: 4850 4d0a 9711 0680    HPM.....
```

## 3. 기록

### 3.1 USB — 평소에는 이걸 쓴다

```sh
# CLI 에서 먼저:  reset boot     → 업데이트 모드로 재부팅
python3 tools/iap_update.py --vid 0x534b build/wish60-he.bin
```

84KB 에 약 1.2초. 배선이 필요 없다. → [09-iap-updater.md](09-iap-updater.md)

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

명령: `help` · `reset info|boot|reset` · `ws2812 info|all|set|off|test|rainbow` · `log` · `usb`

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
| 인터페이스 | CDC ACM (`0x81` IN / `0x01` OUT / `0x83` INT) |

PID 는 자체 키보드 저장소들과 같은 체계다. 이미 쓰는 값 — `0x5201`~`0x5207`,
`0x5210`, `0x5230`, `0x5301`~`0x5303`.

HID 를 붙일 때는 **부트 키보드를 IF0 에 둔다** — 일부 BIOS 가 첫 인터페이스만 본다.
디바이스 클래스가 이미 IAD 조합이고 USB0 엔드포인트가 16개라 CDC 와 공존에 여유가 있다.

## 6. 복구

앱이 매직을 갖고 있으면 부트로더는 계속 그리로 점프한다. 앱이 초기화 중 멈추면
자동 복구가 안 걸리므로 아래 중 하나를 쓴다.

| 경로 | 방법 |
|---|---|
| **JTAG** | `0x20000` 섹터를 소거 → 부트로더가 업데이트 모드로 떨어진다 |
| **강제 스트랩** | PA09 를 LOW 로 잡고 전원 인가 (보드상 위치 미확인) |
| **전체 복원** | 보드 원본 플래시 이미지(1MB)를 `0x80000000` 에 통째로 기록 |

부트로더의 업데이트 명령은 기록 주소를 `0x80020000` 으로 하드코딩하므로
**USB 로는 부트로더 자신을 덮어쓸 수 없다.**

---

## 남은 확인거리

- [ ] PA09 가 보드상 어느 패드인지 실측
- [ ] `USBD_MAX_POWER` 가 디스크립터에 mA 그대로 들어가는지 (규격은 2mA 단위)
- [ ] `HPM5361_FLASH_XIP.ld` — 부트로더판을 만들며 남은 원본. 독립 부팅 빌드가
      필요 없으면 정리
