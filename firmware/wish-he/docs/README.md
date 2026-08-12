# WISH60-HE 펌웨어

HPM5361 홀이펙트 키보드 펌웨어. **상용 보드의 IAP 부트로더 위에 얹는 앱**으로 만들어져
있어서, 벤더 부트로더를 보존한 채 `0x80020000` 부터만 사용한다.

- 부팅: ROM → 부트 헤더 → IAP(`0x80003000`) → 매직 확인 → **본 펌웨어(`0x80020004`)**
- 콘솔: 이 보드에는 UART 헤더가 없다 → **USB CDC** + **JTAG 로 읽는 RAM 링버퍼**
- 클럭: 400MHz (외부 전원 보드, DCDC 설정 금지 — 6.2 절)

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

---

## 4. WS2812 (NeoPixel) — 이 보드의 유일한 시각 표시

외부 디버깅용 LED 도 UART 헤더도 없으므로 네오픽셀이 상태 표시 수단이다.

| 항목 | 값 | 출처 |
|---|---|---|
| 데이터 핀 | **PA29 = SPI1.MOSI (ALT5)** | 덤프 분석 확정 |
| SCLK | **8 MHz** (SPI 1바이트 = 1.0us = WS2812 1비트) | 실측 `TIMING=0x3b04`, `SCLK_DIV=4` |
| LED 개수 | 83 | 덤프 분석 |
| 프레임 | 83 × 24 + latch 60 = **2052 B** | |
| 비트 인코딩 | `0` → `0xE0`(0.375us high), `1` → `0xFC`(0.75us high) | WS2812B T0H 0.4 / T1H 0.8us 규격 내 |

> PA29 먹싱은 **IAP 부트로더가 이미 해준다**(`0x8000EDD2`). 그래도 자립을 위해
> `ws2812Init()` 에서 다시 설정한다.

### CLI

```
ws2812 info
ws2812 all <r> <g> <b>
ws2812 set <ch> <r> <g> <b>
ws2812 off
```

### ★ 전류 예산

WS2812B 는 채널당 풀스케일 약 20mA, LED 1개 흰색이면 약 60mA다. **83개를 전부
흰색 255 로 켜면 약 5A** 로 불가능하다.

```
83개 흰색 v  →  v × 19.5 mA
83개 단색 v  →  v × 6.5  mA
```

USB 선언은 상용 보드와 같은 **500mA**(`USBD_MAX_POWER 500`)로 잡았다. 여기서
MCU·USB·홀센서(64개면 150~200mA)를 빼면 LED 몫은 대략 200~300mA 이므로:

| 표현 | 안전 상한 |
|---|---|
| 전체 흰색 | **v ≈ 10~15** |
| 전체 단색 | **v ≈ 30~45** |
| 소수 LED (디버그 표시) | 여유 있음 |

> 나중에 **프레임 전류 합산 리미터**(총합이 예산을 넘으면 전체 비례 축소)를 넣는 것이
> 정석이다. QMK 의 `RGB_MATRIX_MAXIMUM_BRIGHTNESS` 와 같은 역할.

![WS2812 데이터 경로](images/ws2812-dma.svg)

### 전송 — SPI TX + HDMA (논블로킹)

`ws2812Refresh()` 는 DMA 를 걸고 바로 반환한다. CPU 는 프레임버퍼만 채우므로 키 스캔
루프를 막지 않는다 — 상용 펌웨어가 40kHz 스캔과 LED 를 공존시키는 방식과 같다.
진행 여부는 `ws2812IsBusy()` 로 본다.

> `m483-fw` 의 ws2812.c 는 한 걸음 더 나가 **PDMA 순환 scatter-gather** 로 무정지
> 전송을 한다(재시작 글리치 제거). 필요해지면 참고할 만하다.

### ★ DMA 를 쓰며 겪은 것 두 가지

**① HDMA 채널은 전역 자원이다.** 처음에 WS2812 를 채널 0 에 잡았는데 `uart.c` 가
UART0 RX 로 이미 쓰고 있어서 서로 덮어썼다. 레지스터를 읽으니 `SRCADDR` 이 UART0
데이터 레지스터를 가리키고 있어 바로 드러났다. 이후 `hw_def.h` 한 곳에서 관리한다.

```c
#define HW_DMA_CH_UART0_RX     0
#define HW_DMA_CH_WS2812       2
/*      HW_DMA_CH_ADC          3~  (예정) */
```

**우선순위는 채널 번호와 무관하다.** 채널마다 `CHCTRL[n].CTRL` bit29 로 따로 주며
2단계(LOW/HIGH)뿐이고 기본값은 LOW 다. ADC 는 HIGH 로 잡을 것 — 스캔 타이밍이 밀리면
키 입력이 튀지만 LED 는 2ms 늦어도 보이지 않는다.

**② `dma_check_transfer_status()` 는 유휴 판정에 쓰면 안 된다.** 완료 플래그가 하나도
없으면 `ONGOING` 을 돌려주므로 **한 번도 안 쓴 채널이 "진행 중"으로 보인다.**
"시작 후 완료 확인용" 함수다. 시작 시점을 자체 `is_busy` 플래그로 기억해야 한다.
또한 W1C 라 읽는 순간 플래그가 지워진다.

> DMA 는 D-cache 를 보지 않는다. 전송 전에 `l1c_dc_writeback()` 으로 프레임버퍼를
> 메모리까지 밀어내야 한다.

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

ROM 이 직접 앱을 띄우는 EVK 와 다른 점들이다. **전부 실측으로 확인했다.**

### 6.1 링커 / 빌드

```ld
FLASH (rx) : ORIGIN = 0x80020000, LENGTH = 0x60000   /* 384KB */

/DISCARD/ : { *(.nor_cfg_option) *(.boot_header) *(.fw_info_table) *(.dc_info) }

.iap_magic ORIGIN(FLASH) : { LONG(0x0A4D5048) }      /* "HPM\n" */
__app_load_addr__ = ORIGIN(FLASH) + 4;
```

- **`LENGTH` 를 384KB 로 묶는다** — `0x80080000` 부터의 EEPROM(캘리브레이션·시리얼)을
  침범하지 않아 벤더 펌웨어를 언제든 완전 복원할 수 있다.
- **`.start` 선두의 `. = ALIGN(8);` 을 반드시 뺀다.** 그대로 두면 진입점이 `0x80020008`
  로 밀려 IAP 의 점프가 빗나간다.
- **`hpm_bootheader.c` 를 빌드에서 제외한다.** `__app_offset__` 를 참조하는데 링커에서
  그 심볼을 없앴기 때문이다.

### 6.2 외부 전원 — DCDC 전압을 설정하면 안 된다

`pcfg_dcdc_set_voltage()` 를 호출하면 `VOLT` 에는 값이 들어가고 `MODE` 도 `001`(basic)
인데 **`DCDC_MODE.READY`(bit28) 가 영영 서지 않아** `pcfg_dcdc_is_stable()` 에서
무한 대기한다 (PC 가 `pcfg_dcdc_is_stable` 안에 고정된다).

SDK 원본 주석이 경고하던 그대로다 — *"When using an external DCDC, don't set the
internal DCDC voltage."* 상용 펌웨어도 같은 값(1175mV)을 쓰지만 구버전 SDK 라
`READY` 를 기다리지 않아 그냥 넘어간다.

> **자체 설계(외부 LDO)에도 그대로 적용되는 함정이다.**

### 6.3 ★ IAP 인계 상태 정리 — 안 하면 부팅 직후 죽는다

IAP 는 **주변장치를 켜둔 채** 점프한다. `mstatus.MIE` 를 여는 순간 등록되지 않은
벡터로 점프해 `default_irq_handler` 무한루프(`0x19C`)에 빠진다.

`system_init()` 이 weak 이므로 `board.c` 에서 덮어쓰고 **세 가지를 모두** 한다:

```c
write_csr(CSR_MIE, 0);                     /* ① start.S 는 mstatus 만 지운다 */

for (irq = 1; irq < 128; irq++)            /* ② IAP 가 켜둔 PLIC enable */
    intc_m_disable_irq(irq);

volatile uint32_t *claim =                 /* ③ ★ 남은 pending 배수 */
    (volatile uint32_t *)(HPM_PLIC_BASE + HPM_PLIC_CLAIM_OFFSET);
for (i = 0; i < 128; i++) {
    uint32_t id = *claim;
    if (id == 0) break;
    *claim = id;                           /* complete */
}
```

**③ 이 핵심이다.** enable 을 전부 0 으로 만든 뒤에도 claim 레지스터가 계속 `6`(GPTMR1)
을 내줬다 — IAP 가 리포트 주기를 GPTMR1 로 돌리다가 끄지 않고 넘기기 때문이다.
마스킹만으로는 부족하고 pending 자체를 비워야 한다.

셋을 모두 하면 SDK 원본과 같은 위치(`system_init()` 끝)에서 전역 인터럽트를 켤 수 있고,
`hw.c` 등 애플리케이션 코드는 EVK 원본과 동일하게 유지된다.

> 진단 과정 메모 — 싱글스텝으로는 재현되지 않는다(디버거가 스텝 중 인터럽트를 마스킹).
> 브레이크포인트에서 claim 레지스터(`HPM_PLIC_BASE + 0x200004`)를 직접 읽는 것이
> 결정타였다.

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

- [x] `reset` 모듈 — `reset info` / `reset boot`(IAP 진입) / `reset reset`  ※ `boot` 는 미실증
- [ ] 파이썬 업데이터 — IAP HID 프로토콜(`0x81` 시작 → `0x80`/`0x82` → `0x83` 종료)
- [ ] HID 인터페이스 추가 (5 절) — IF0 부트키보드 / IF1 VIA(0xFF60)
- [ ] PA09 가 보드상 어느 패드인지 실측
- [x] WS2812 드라이버 (GRB 순서·발광 확인 완료)
- [ ] ADC(PB00~PB07) + MUX(PY00~PY02) 스캔 — 1단계는 임계값 on/off
- [ ] 프레임 전류 합산 리미터
