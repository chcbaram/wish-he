# WISH60-HE 펌웨어

HPM5361 홀이펙트 키보드 펌웨어. **상용 보드의 IAP 부트로더 위에 얹는 앱**으로 만들어져
있어서, 벤더 부트로더를 보존한 채 `0x80020000` 부터만 사용한다.

- 부팅: ROM → 부트 헤더 → IAP(`0x80003000`) → 매직 확인 → **본 펌웨어(`0x80020004`)**
- 콘솔: 이 보드에는 UART 헤더가 없다 → **USB CDC** + **JTAG 로 읽는 RAM 링버퍼**
- 클럭: 400MHz (외부 전원 보드 — DCDC 설정 금지, [steps/01](steps/01-boot-on-iap.md))

구현 단계와 진행 상황은 [steps.md](steps.md) 를 본다.

IAP 부트로더의 리버스 엔지니어링 결과(부팅 판정 · USB 업데이트 프로토콜)는
`../hpm5361-fw/docs/board-iap.md` 에 있다. 프로브·복구 절차는 같은 곳의
`debug-recovery.md` 를 본다.

---

## 1. 빌드

```sh
export HPM_RISCV_TOOLCHAIN_DIR="$HOME/hdd/tools/xpack-riscv-none-elf-gcc-13.4.0-1"
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j8
```

산출물 `build/wish60-he.bin` 은 **`0x80020000` 부터의 이미지**다. 선두 4 바이트가
`48 50 4D 0A`(`"HPM\n"`) 인지 확인하면 IAP 가 인식할 준비가 된 것이다.

```sh
xxd -l 16 build/wish60-he.bin
# 00000000: 4850 4d0a 9711 0680 ...   "HPM....."
```

## 2. 기록 (JTAG)

**FT2232 + HPMicro OpenOCD 조합이어야 한다.** J-Link 은 읽기·디버깅 전용이다
(자세한 이유는 `debug-recovery.md` 1 절).

```sh
~/hdd/tools/hpmicro/openocd/bin/openocd -s tools/openocd -f hpm5361-fw.cfg \
  -c "adapter speed 4000" -c "init" -c "halt" -c "flash probe 0" \
  -c "flash write_image erase build/wish60-he.bin 0x80020000 bin" \
  -c "verify_image build/wish60-he.bin 0x80020000 bin" \
  -c "reset run" -c "exit"
```

`0x80020000` 부터만 쓰므로 **IAP 와 EEPROM 영역은 건드리지 않는다.**

## 3. 디버그

### 3.1 USB CDC 콘솔 (CLI)

**macOS 에서는 반드시 `/dev/cu.*` 를 쓴다.** `/dev/tty.*` 는 캐리어를 기다려 블록된다.

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

### 3.2 RAM 링버퍼 콘솔 (JTAG)

USB 가 올라오기 전이나 죽은 뒤에도 로그를 읽을 수 있다. `log.c` 가 모든 `logPrintf()`
출력을 `.noinit` 링버퍼에도 흘린다. `.noinit` 이라 **리셋해도 내용이 남는다.**

주소는 빌드마다 바뀌므로 ELF 에서 뽑는다.

```sh
A=$(riscv-none-elf-objdump -t build/wish60-he.elf | grep -w log_ram | awk '{print "0x"$1}')
openocd ... -c "dump_image log.bin $A 0x810" -c "exit"

python3 -c "
import struct; d=open('log.bin','rb').read()
m,i,w,s=struct.unpack('<IIII',d[:16])
print((d[16+i:16+s]+d[16:16+i]).decode('utf-8','replace') if w else d[16:16+i].decode('utf-8','replace'))"
```

헤더는 `magic('RLOG') / index / wrapped / size` 4 워드다.

### 3.3 JTAG 없이 굽기

```sh
# CLI 에서  reset boot   -> IAP 모드로 재부팅
python3 tools/iap_update.py --vid 0x534b build/wish60-he.bin
```

84KB 에 약 1.2초. 상세는 **[steps/09-iap-updater.md](steps/09-iap-updater.md)**

---

## 4. WS2812 (NeoPixel)

외부 LED 가 없으므로 네오픽셀이 유일한 시각 표시다.
PA29 = SPI1.MOSI, 8MHz, 83개, SPI + HDMA 논블로킹.

CLI: `ws2812 info / all / set / off / test / rainbow`

> 상세·전류 예산·겪은 함정은 **[steps/04-ws2812.md](steps/04-ws2812.md)**

---

## 5. USB 구성

| 항목 | 값 |
|---|---|
| VID / PID | `0x0483` / `0x5304` |
| 제품 문자열 | `WISH60-HE` |
| 속도 | High Speed (480 Mb/s) |
| 클래스 | `0xEF / 0x02 / 0x01` (Miscellaneous / Common / **IAD**) |
| 현재 인터페이스 | CDC ACM (`0x81` IN / `0x01` OUT / `0x83` INT) |

PID 는 자체 키보드 저장소들과 같은 체계다. 이미 쓰는 값: `0x5201`~`0x5207`, `0x5210`,
`0x5230`, `0x5301`~`0x5303`.

### 향후 — HID 추가 (복합 장치)

디바이스 클래스가 이미 IAD 조합이고 USB0 엔드포인트가 16 개라 여유가 충분하다.
**부트 키보드 인터페이스는 IF0 에 둔다** — 일부 BIOS 가 첫 인터페이스만 보기 때문이다
(상용 펌웨어도 그렇게 돼 있다).

| IF | 용도 | EP |
|---|---|---|
| 0 | HID 부트 키보드 | `0x81` IN 8B, bInterval 1 (HS 125us = 8kHz) |
| 1·2 | CDC (IAD 묶음) | INT / IN / OUT |
| 3+ | HID NKRO · 설정 채널 | |

---

## 6. IAP 위에 얹기 위한 요건

ROM 이 직접 앱을 띄우는 EVK 와 다른 점들이다. 링커 설정, 외부 전원에서의 DCDC 함정,
**IAP 인계 상태 정리**(안 하면 부팅 직후 죽는다) 세 가지가 핵심이다.

> 상세는 **[steps/01-boot-on-iap.md](steps/01-boot-on-iap.md)**
> 부트로더 진입은 **[steps/03-reset-boot.md](steps/03-reset-boot.md)**

---

## 7. 복구

자체 앱이 매직을 갖고 있으면 IAP 는 계속 그리로 점프한다. 앱이 초기화 중 멈추면
자동 복구가 안 걸리므로 아래 중 하나를 쓴다.

| 경로 | 방법 |
|---|---|
| **JTAG** | `0x20000` 섹터를 소거하면 IAP 가 업데이트 모드로 떨어진다 |
| **PA09** | LOW 로 잡고 전원 인가 → 강제 업데이트 모드 (보드상 위치 미확인) |
| **벤더 복원** | `../hpm5361-fw/docs/flash_dump.bin` 을 `0x80000000` 에 통째로 기록 |

---

## 8. 남은 작업

전체 로드맵은 [steps.md](steps.md) 를 본다. 가까운 것만 적으면:

- [ ] **ADC(PB00~PB07) + MUX(PY00~PY02) 스캔** — 다음 단계. 1단계는 임계값 on/off
- [ ] HID 인터페이스 추가 (5 절) — IF0 부트키보드 / IF1 VIA(`0xFF60`)
- [ ] PA09 가 보드상 어느 패드인지 실측
- [ ] 프레임 전류 합산 리미터
- [ ] `USBD_MAX_POWER` 가 디스크립터에 mA 그대로 들어가는지 확인 (규격은 2mA 단위)
