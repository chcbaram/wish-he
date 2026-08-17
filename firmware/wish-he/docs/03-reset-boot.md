# 3. 리셋 / 부트로더 진입

> 전체 목차는 [README.md](README.md) 를 본다.

`m483-fw` · `baram-qmk-8k` 의 `reset` 모듈과 같은 인터페이스로 맞췄다.

```
reset info    리셋 플래그 · 부트 플래그
reset boot    IAP 업데이트 모드로 재부팅
reset reset   소프트 리셋
```

---

## 소프트 리셋

```c
ppor_sw_reset(HPM_PPOR, 10);      /* 24MHz 기준 카운터 */
```

## IAP 진입 — 플래시 플래그

IAP 는 부팅할 때마다 **플래시 `0x8001D000`** 워드를 본다. `0xFFFFFFFF` 가 아니면
앱으로 가지 않고 USB 업데이트 모드로 남는다. 그래서 "부트로더로 재부팅" 은
그 워드를 쓰고 소프트 리셋하는 것이다.

```c
resetToBoot()
  -> resetWriteBootFlag(0x0000FFFF)    /* ROM API xpi_nor */
  -> 되읽어 검증
  -> ppor_sw_reset()
```

다른 프로젝트들은 RAM/RTC 백업 레지스터를 쓰지만 **여기서는 IAP 가 플래시만
확인하므로 선택의 여지가 없다.** 자체 부트로더로 가면 PMIC 도메인 GPR
(`HPM_PGPR0`, `0xF4110000`)로 옮기는 편이 낫다 — 플래시 마모가 없다.

부팅 시 `resetInit()` 이 플래그가 남아 있으면 섹터를 지운다. 안 지우면 다음 부팅에도
IAP 가 앱으로 넘어오지 않는다.

---

## ★ 겪은 함정 두 가지

### ① `nor_cfg_option` 헤더의 하위 4비트 = 옵션 워드 개수

```c
#define NOR_CFG_HEADER   0xFCF90002U   /* board.c 의 nor_cfg_option 과 같아야 한다 */
#define NOR_CFG_OPTION0  0x00000006U   /* 120MHz */
#define NOR_CFG_OPTION1  0x00001000U   /* 2번 핀그룹 (PX00~PX07) */
```

처음에 `0xFCF90001` 로 줬다. 그러면 옵션 워드가 1개라는 뜻이라 **`option1`(핀그룹)이
무시되고** 기록이 실패한다.

### ② ROM API 로 플래시를 고친 뒤에는 D-cache 를 무효화해야 한다

```c
l1c_dc_invalidate(BOOT_FLAG_XIP_ADDR, 64);
return *(volatile uint32_t *)BOOT_FLAG_XIP_ADDR;
```

이걸 안 해서 **실제로는 기록이 됐는데 되읽기가 옛 값을 봐서 "실패" 로 오판**했다.
그 상태로 리셋을 건너뛰었다가, 나중에 다른 이유로 리셋됐을 때 IAP 가 플래그를
발견해 업데이트 모드로 들어가는 바람에 한참을 헤맸다.

> ROM API 는 `fencei()` 로 I-cache 만 정리한다. D-cache 는 호출자 몫이다.

---

## 주의 — XIP 중 자기 플래시 쓰기

인터럽트를 반드시 막고 해야 한다. ISR 본체가 XIP 영역에 있어서 소거 중에 인출하면
죽는다. ROM API 자체는 RAM/ROM 에서 도므로 안전하다.

```c
mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
... rom_xpi_nor_* ...
restore_global_irq(mask);
```
