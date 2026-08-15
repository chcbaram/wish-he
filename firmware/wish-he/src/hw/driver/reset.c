/*
 * reset.c
 *
 * 리셋 원인 조회와 부트로더(IAP) 진입.
 *
 * 이 보드는 상용 IAP 부트로더 위에 얹혀 있고, IAP 는 부팅할 때마다
 * 플래시 BOOT_FLAG_ADDR 워드를 본다 — 0xFFFFFFFF 가 아니면 앱으로 가지 않고
 * USB 업데이트 모드로 남는다. 그래서 "부트로더로 재부팅" 은 그 워드를 쓰고
 * 소프트 리셋하는 것이다. (docs/03-reset-boot.md)
 *
 * 다른 프로젝트들(m483-fw, baram-qmk-8k)은 RAM/RTC 백업 레지스터에 플래그를 두지만,
 * 여기서는 IAP 가 플래시만 확인하므로 선택의 여지가 없다. 자체 부트로더를 쓰게 되면
 * PMIC 도메인 GPR(HPM_PGPR0, 0xF4110000)로 옮기는 편이 낫다 — 플래시 마모가 없다.
 */
#include "reset.h"


#ifdef _USE_HW_RESET
#include "cli.h"
#include "log.h"

#include "hpm_romapi.h"
#include "hpm_ppor_drv.h"
#include "hpm_interrupt.h"
#include "hpm_l1c_drv.h"
#include "hpm5361_it.h"


#if CLI_USE(HW_RESET)
static void cliReset(cli_args_t *args);
#endif


/* IAP 가 확인하는 플래그. 주소는 플래시 오프셋(실행 주소 0x8001D000). */
#define BOOT_FLAG_ADDR        0x0001D000UL
#define BOOT_FLAG_XIP_ADDR    (0x80000000UL + BOOT_FLAG_ADDR)

/* IAP 는 != 0xFFFFFFFF 만 본다. 상용 펌웨어와 같은 값을 쓴다. */
#define BOOT_REQUEST_MAGIC    0x0000FFFFUL

/* ppor_sw_reset() 카운터. 24MHz 기준이며 상용 펌웨어도 10 을 쓴다. */
#define RESET_SW_COUNTER      10

/*
 * XPI NOR 설정 옵션 — board.c 의 nor_cfg_option 과 반드시 같아야 한다.
 * header 하위 4비트가 뒤따르는 옵션 워드 개수다. 2 로 주지 않으면 option1
 * (핀그룹)이 무시돼 엉뚱한 플래시를 잡는다.
 */
#define NOR_CFG_HEADER        0xFCF90002U
#define NOR_CFG_OPTION0       0x00000006U   /* 120MHz */
#define NOR_CFG_OPTION1       0x00001000U   /* 2번 핀그룹 (PX00~PX07) */


/*
 * 부팅 안전망 시험 플래그. .noinit 이라 리셋해도 남는다.
 * 안전망이 부트로더로 떨어뜨릴 때 같이 지워야 무한 반복이 안 된다.
 */
static __attribute__((section(".noinit"))) struct
{
  uint32_t magic;
  uint32_t on;
} hang_test;

#define HANG_TEST_MAGIC   0x48414E47UL   /* 'HANG' */

bool resetGetHangTest(void)
{
  return (hang_test.magic == HANG_TEST_MAGIC) && (hang_test.on != 0);
}

void resetSetHangTest(bool on)
{
  hang_test.magic = HANG_TEST_MAGIC;
  hang_test.on    = on ? 1 : 0;
}

/*
 * 부팅 안전망.
 *
 * 부팅 도중 멈추는 펌웨어를 구우면 USB 가 안 올라와 복구가 막힌다. 시도 횟수를
 * .noinit 에 세고, 한계를 넘으면 더 진행하지 않고 부트로더로 떨어진다.
 *
 * 여기서 멈추는 게 요점이다 — 아래쪽 초기화(스캔·USB·플래시)에서 죽고 있으므로
 * 같은 길을 또 가면 또 죽는다. 부트로더로 가면 USB 로 되구울 수 있다.
 *
 * 하드폴트도 같은 그물에 걸린다. exception_handler 가 리셋하므로 반복되면 카운트가
 * 쌓인다. 일회성이라면 메인 루프가 살아남아 카운트가 지워진다.
 */
static void resetBootGuard(void)
{
  itBootMarkStart();

  if (itBootGetFailCount() > HW_BOOT_FAIL_LIMIT)
  {
    logPrintf("[E_] 부팅 %d회 연속 실패 — 부트로더로 간다\n", (int)itBootGetFailCount());
    resetSetHangTest(false);        /* 시험 중이었다면 여기서 푼다 */
    resetToBoot();                  /* 돌아오지 않는다 */
  }

  /* 안전망 자체를 시험하려고 일부러 멈추는 자리 */
  if (resetGetHangTest())
  {
    logPrintf("[  ] hang test — 여기서 멈춘다 (시도 %d)\n", (int)itBootGetFailCount());
    while (1)
    {
    }
  }
}

/* 메인 루프가 일정 시간 살아남으면 이번 부팅을 성공으로 본다. */
void resetBootAlive(void)
{
  static bool marked = false;

  if (marked)                   return;
  if (millis() < HW_BOOT_OK_MS) return;

  itBootMarkOk();
  marked = true;
}

static bool     is_init    = false;
static uint32_t reset_bits = 0;
static uint32_t boot_mode  = 0;

/* 진단: 어느 ROM API 에서 실패했는지 */
static volatile uint32_t nor_step = 0;   /* 1=get_config 2=erase 3=program */
static volatile hpm_stat_t nor_stat = 0;


static const char *mode_bit_str[] =
  {
    "MODE_BIT_BOOT",
    "MODE_BIT_UPDATE",
  };


static uint32_t resetGetBootFlag(void)
{
  /*
   * ROM API 로 플래시를 고친 직후에는 D-cache 가 옛 값을 들고 있다.
   * 무효화하지 않으면 방금 쓴 값을 못 읽는다 — 실제로 기록은 됐는데
   * "실패" 로 오판했다. 주소는 4KB 정렬이라 캐시 라인 경계에 걸리지 않는다.
   */
  l1c_dc_invalidate(BOOT_FLAG_XIP_ADDR, 64);

  return *(volatile uint32_t *)BOOT_FLAG_XIP_ADDR;
}

/*
 * 플래그가 있는 4KB 섹터를 지운다.
 *
 * XIP 로 실행 중에 같은 플래시를 건드리므로 인터럽트를 반드시 막아야 한다 —
 * ISR 본체가 XIP 영역에 있어서 소거 중에 인출하면 죽는다.
 * ROM API 자체는 RAM/ROM 에서 도므로 안전하다.
 */
static bool resetEraseBootFlag(void)
{
  xpi_nor_config_t        nor_cfg;
  xpi_nor_config_option_t cfg_option;
  hpm_stat_t              status;
  uint32_t                mask;

  cfg_option.header.U  = NOR_CFG_HEADER;
  cfg_option.option0.U = NOR_CFG_OPTION0;
  cfg_option.option1.U = NOR_CFG_OPTION1;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);

  status = rom_xpi_nor_get_config(HPM_XPI0, &nor_cfg, &cfg_option);
  if (status == status_success)
  {
    status = rom_xpi_nor_erase_sector(HPM_XPI0, xpi_xfer_channel_auto,
                                      &nor_cfg, BOOT_FLAG_ADDR);
  }

  restore_global_irq(mask);

  return (status == status_success);
}

static bool resetWriteBootFlag(uint32_t data)
{
  xpi_nor_config_t        nor_cfg;
  xpi_nor_config_option_t cfg_option;
  hpm_stat_t              status;
  uint32_t                mask;
  uint32_t                value = data;

  cfg_option.header.U  = NOR_CFG_HEADER;
  cfg_option.option0.U = NOR_CFG_OPTION0;
  cfg_option.option1.U = NOR_CFG_OPTION1;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);

  nor_step = 1;
  status = rom_xpi_nor_get_config(HPM_XPI0, &nor_cfg, &cfg_option);
  if (status == status_success)
  {
    /*
     * NOR 는 1->0 만 가능하다. 소거 상태(0xFFFFFFFF)면 그대로 쓰고,
     * 아니면 섹터를 지운 뒤 쓴다.
     */
    if (*(volatile uint32_t *)BOOT_FLAG_XIP_ADDR != 0xFFFFFFFFUL)
    {
      nor_step = 2;
      status = rom_xpi_nor_erase_sector(HPM_XPI0, xpi_xfer_channel_auto,
                                        &nor_cfg, BOOT_FLAG_ADDR);
    }
  }
  if (status == status_success)
  {
    nor_step = 3;
    status = rom_xpi_nor_program(HPM_XPI0, xpi_xfer_channel_auto,
                                 &nor_cfg, &value, BOOT_FLAG_ADDR, sizeof(value));
  }
  nor_stat = status;

  restore_global_irq(mask);

  return (status == status_success);
}


bool resetInit(void)
{
  resetBootGuard();     /* ★ 맨 앞. 연속 실패가 쌓였으면 여기서 부트로더로 빠진다 */

  reset_bits = ppor_reset_get_flags(HPM_PPOR);
  ppor_reset_clear_flags(HPM_PPOR, reset_bits);

  /*
   * 플래그가 남아 있으면 업데이트 모드를 거쳐 돌아온 것이다.
   * 지우지 않으면 다음 부팅에도 IAP 가 앱으로 안 넘어온다.
   */
  if (resetGetBootFlag() != 0xFFFFFFFFUL)
  {
    boot_mode = (1 << MODE_BIT_BOOT);
    resetEraseBootFlag();
  }

  is_init = true;

#if CLI_USE(HW_RESET)
  cliAdd("reset", cliReset);
#endif

  return is_init;
}

void resetLog(void)
{
  logPrintf("Reset Flag \t\t: 0x%08X\r\n", (unsigned int)reset_bits);

  for (int i = 0; i < MODE_BIT_MAX; i++)
  {
    if (boot_mode & (1 << i))
    {
      logPrintf("     %s\r\n", mode_bit_str[i]);
    }
  }
}

void resetToBoot(void)
{
  if (resetSetBootMode(1 << MODE_BIT_BOOT) == false)
  {
    return;                 /* 플래그를 못 썼으면 리셋하지 않는다 */
  }
  resetToReset();
}

void resetToReset(void)
{
  ppor_sw_reset(HPM_PPOR, RESET_SW_COUNTER);

  while (1)
  {
  }
}

uint32_t resetGetBits(void)
{
  return reset_bits;
}

void resetSetBits(uint32_t data)
{
  reset_bits = data;
}

bool resetSetBootMode(uint32_t data)
{
  boot_mode = data;

  if (resetWriteBootFlag(BOOT_REQUEST_MAGIC) == false)
  {
    return false;
  }

  /* 되읽어 확인한다 — 기록이 실패한 채로 리셋하면 그냥 앱으로 되돌아온다. */
  return (resetGetBootFlag() != 0xFFFFFFFFUL);
}

uint32_t resetGetBootMode(void)
{
  return boot_mode;
}


#if CLI_USE(HW_RESET)
void cliReset(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("reset flag  : 0x%08X\n", (unsigned int)reset_bits);
    cliPrintf("boot flag   : 0x%08X (0xFFFFFFFF = 앱 부팅)\n",
              (unsigned int)resetGetBootFlag());
    cliPrintf("nor step    : %d (1=get_config 2=erase 3=program)\n", (int)nor_step);
    cliPrintf("nor stat    : 0x%X\n", (unsigned int)nor_stat);
    for (int i = 0; i < MODE_BIT_MAX; i++)
    {
      if (boot_mode & (1 << i))
      {
        cliPrintf("      %s\n", mode_bit_str[i]);
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    if (resetSetBootMode(1 << MODE_BIT_BOOT) == false)
    {
      cliPrintf("[E_] boot flag 기록 실패 (0x%08X)\n",
                (unsigned int)resetGetBootFlag());
    }
    else
    {
      cliPrintf("boot flag = 0x%08X, jumping to iap...\n",
                (unsigned int)resetGetBootFlag());
      delay(100);               /* CDC 로 문구가 나갈 시간 */
      resetToReset();
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    cliPrintf("reset...\n");
    delay(100);
    resetToReset();
    ret = true;
  }

  /*
   * 부팅 안전망 시험 — 다음 부팅부터 hwInit() 초반에서 일부러 멈춘다.
   *
   * .noinit 플래그라 리셋해도 남는다. HW_BOOT_FAIL_LIMIT 만큼 연속으로 멈추면
   * 안전망이 부트로더로 떨어뜨려야 한다. 그러면 USB 로 되구울 수 있다.
   */
  if (args->argc == 1 && args->isStr(0, "hang"))
  {
    resetSetHangTest(true);
    cliPrintf("다음 부팅부터 초기화 중 멈춘다.\n");
    cliPrintf("%d회 연속 실패하면 부트로더로 떨어져야 한다.\n", HW_BOOT_FAIL_LIMIT);
    delay(300);
    resetToReset();
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("boot fail   : %d\n", (int)itBootGetFailCount());
    cliPrintf("hang test   : %d\n", resetGetHangTest());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("reset info\n");
    cliPrintf("reset hang     부팅 안전망 시험 (일부러 멈춘다)\n");
    cliPrintf("reset boot\n");
    cliPrintf("reset reset\n");
  }
}
#endif

#endif /* _USE_HW_RESET */
