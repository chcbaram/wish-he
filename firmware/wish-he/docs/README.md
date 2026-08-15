# WISH60-HE 펌웨어

HPM5361 홀이펙트 키보드 펌웨어. **상용 보드의 IAP 부트로더 위에 얹는 앱**이라
벤더 부트로더를 보존한 채 `0x80020000` 부터만 쓴다.

```
ROM → 부트 헤더 → IAP(0x80003000) → 매직 확인 → 본 펌웨어(0x80020004)
```

- 클럭 400MHz — 외부 전원 보드라 **DCDC 전압을 설정하면 안 된다**
- 이 보드에는 UART 헤더도 디버그 LED 도 없다 → **USB CDC** · **JTAG RAM 링버퍼** · **네오픽셀**

---

## 문서 지도

| 찾는 것 | 문서 |
|---|---|
| **어디까지 왔나 / 다음은 뭔가** | [steps.md](steps.md) |
| 각 단계에서 한 일과 함정 | [steps/](steps/) |
| IAP 인터페이스 명세 (부팅 판정 · USB 프로토콜) | `../../hpm5361-fw/docs/board-iap.md` |
| 프로브 · OpenOCD · 벤더 펌웨어 복원 | `../../hpm5361-fw/docs/debug-recovery.md` |
| 상용 보드 핀맵 · 플래시 레이아웃 | `../../hpm5361-fw/docs/flash_dump.md` |

아래는 **실무 절차만** 적는다.

---

## 1. 빌드

```sh
export HPM_RISCV_TOOLCHAIN_DIR="$HOME/hdd/tools/xpack-riscv-none-elf-gcc-13.4.0-1"
cmake -B build -DCMAKE_BUILD_TYPE=Release .
cmake --build build -j8
```

산출물 `build/wish60-he.bin` 은 `0x80020000` 부터의 이미지다. 선두 4바이트가
`48 50 4D 0A`(`"HPM\n"`) 여야 IAP 가 인식한다.

```sh
xxd -l 8 build/wish60-he.bin
# 00000000: 4850 4d0a 9711 0680    HPM.....
```

## 2. 기록

### 2.1 USB — 평소에는 이걸 쓴다

```sh
# CLI 에서 먼저:  reset boot     → IAP 업데이트 모드로 재부팅
python3 tools/iap_update.py --vid 0x534b build/wish60-he.bin
```

84KB 에 약 1.2초. 배선이 필요 없다. → [steps/09-iap-updater.md](steps/09-iap-updater.md)

### 2.2 JTAG — 앱이 죽었을 때

**FT2232 + HPMicro OpenOCD 조합이어야 한다.** J-Link 은 읽기·디버깅 전용이다.

```sh
~/hdd/tools/hpmicro/openocd/bin/openocd -s tools/openocd -f hpm5361-fw.cfg \
  -c "adapter speed 4000" -c "init" -c "reset halt" -c "flash probe 0" \
  -c "flash write_image erase build/wish60-he.bin 0x80020000 bin" \
  -c "verify_image build/wish60-he.bin 0x80020000 bin" \
  -c "reset run" -c "exit"
```

`0x80020000` 부터만 쓰므로 IAP 와 EEPROM 은 건드리지 않는다.

> `init` 다음에 `reset halt` 를 넣는다. 앱이 돌던 상태에서 바로 `halt` 하면
> 플래시 로더 실행이 실패하는 경우가 있다.

## 3. 디버그

### 3.1 CLI (USB CDC)

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

### 3.2 RAM 링버퍼 (JTAG)

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

## 4. USB 구성

| 항목 | 값 |
|---|---|
| VID / PID | `0x0483` / `0x5304` |
| 제품 문자열 | `WISH60-HE` |
| 속도 | High Speed (480 Mb/s) |
| 클래스 | `0xEF / 0x02 / 0x01` (Misc / Common / **IAD**) |
| 인터페이스 | CDC ACM (`0x81` IN / `0x01` OUT / `0x83` INT) |

PID 는 자체 키보드 저장소들과 같은 체계다. 이미 쓰는 값 — `0x5201`~`0x5207`,
`0x5210`, `0x5230`, `0x5301`~`0x5303`.

HID 를 붙일 때는 **부트 키보드를 IF0 에 둔다** — 일부 BIOS 가 첫 인터페이스만 본다
(상용 펌웨어도 그렇다). 디바이스 클래스가 이미 IAD 조합이고 USB0 엔드포인트가
16개라 CDC 와 공존에 여유가 있다.

## 5. 복구

자체 앱이 매직을 갖고 있으면 IAP 는 계속 그리로 점프한다. 앱이 초기화 중 멈추면
자동 복구가 안 걸리므로 아래 중 하나를 쓴다.

| 경로 | 방법 |
|---|---|
| **JTAG** | `0x20000` 섹터를 소거 → IAP 가 업데이트 모드로 떨어진다 |
| **PA09** | LOW 로 잡고 전원 인가 → 강제 업데이트 모드 (보드상 위치 미확인) |
| **벤더 복원** | `../../hpm5361-fw/docs/flash_dump.bin` 을 `0x80000000` 에 통째로 기록 |

IAP 의 업데이트 명령은 기록 주소를 `0x80020000` 으로 하드코딩하므로
**USB 로는 IAP 자신을 덮어쓸 수 없다.**
