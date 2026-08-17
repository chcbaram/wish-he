# 1. IAP 위에서 부팅

> 전체 목차는 [README.md](README.md) 를 본다.

보드의 IAP 부트로더를 보존한 채 `0x80020000` 부터만 쓴다. 그래야 복구망이 세 겹으로 남는다.

```
ROM -> 부트 헤더(0x1000) -> IAP(0x80003000) -> 매직 확인 -> 본 펌웨어(0x80020004)
```

IAP 가 요구하는 조건과 인계 상태는 [00-hardware.md](00-hardware.md) 에 정리돼 있다.

---

## 링커 / 빌드

```ld
FLASH (rx) : ORIGIN = 0x80020000, LENGTH = 0x60000   /* 384KB */

/DISCARD/ : { *(.nor_cfg_option) *(.boot_header) *(.fw_info_table) *(.dc_info) }

.iap_magic ORIGIN(FLASH) : { LONG(0x0A4D5048) }      /* "HPM\n" */
__app_load_addr__ = ORIGIN(FLASH) + 4;
```

- **`LENGTH` 를 384KB 로 묶는다** — `0x80080000` 부터의 EEPROM(캘리브레이션·시리얼)을
  침범하지 않아 원래 상태로 언제든 완전 복원할 수 있다.
- **`.start` 선두의 `. = ALIGN(8);` 을 반드시 뺀다.** 그대로 두면 진입점이 `0x80020008`
  로 밀려 IAP 의 점프가 빗나간다.
- **`hpm_bootheader.c` 를 빌드에서 제외한다.** `__app_offset__` 를 참조하는데 링커에서
  그 심볼을 없앴기 때문이다.

빌드 후 선두 4바이트가 `48 50 4D 0A` 인지 보면 된다.

```sh
xxd -l 8 build/wish60-he.bin
# 00000000: 4850 4d0a 9711 0680    HPM.....
```

---

## ★ 외부 전원 — DCDC 전압을 설정하면 안 된다

`pcfg_dcdc_set_voltage()` 를 호출하면 `VOLT` 에는 값이 들어가고 `MODE` 도 `001`(basic)
인데 **`DCDC_MODE.READY`(bit28) 가 영영 서지 않아** `pcfg_dcdc_is_stable()` 에서
무한 대기한다.

SDK 원본 주석이 경고하던 그대로다 — *"When using an external DCDC, don't set the
internal DCDC voltage."* 이 보드는 1175mV 를 쓰는데, 구버전 SDK 라
`READY` 를 기다리지 않아 그냥 넘어간다.

> **자체 설계(외부 LDO)에도 그대로 적용되는 함정이다.**

CPU 는 보드가 주는 전압 그대로 400MHz 로 돈다(PLL0CLK2 직결).

---

## ★ IAP 인계 정리 — 안 하면 부팅 직후 죽는다

IAP 는 **주변장치를 켜둔 채** 점프한다. `mstatus.MIE` 를 여는 순간 등록되지 않은
벡터로 점프해 `default_irq_handler` 무한루프(`0x19C`)에 빠진다.

`system_init()` 이 weak 이므로 `board.c` 에서 덮어쓰고 **세 가지를 모두** 한다.

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

셋을 모두 하면 SDK 원본과 같은 위치에서 전역 인터럽트를 켤 수 있고, `hw.c` 등
애플리케이션 코드는 EVK 원본과 동일하게 유지된다.

> 진단 메모 — 싱글스텝으로는 재현되지 않는다(디버거가 스텝 중 인터럽트를 마스킹).
> 브레이크포인트에서 claim 레지스터를 직접 읽는 것이 결정타였다.

---

## 검증

- 콜드 부팅(전원 재인가)으로 ROM → IAP → 앱 경로 확인
- `Booting..Clock : 400 Mhz`, `Booting..Addr : 0x80020000`
