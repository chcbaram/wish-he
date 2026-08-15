/*
 * flash.c  —  내장 XPI NOR 접근 (ROM API)
 *
 * reset.c 가 부트 플래그 하나를 쓰려고 갖고 있던 ROM API 절차를 일반화한 것이다.
 * 캘리브레이션 저장(8편)과 VIA EEPROM(11편)이 같이 쓴다.
 *
 * ★ 이 파일이 다루는 세 가지 함정은 전부 3편에서 실제로 겪은 것들이다.
 *
 *   ① nor_cfg_option 의 header 하위 4비트가 "뒤따르는 옵션 워드 개수" 다.
 *      0xFCF90001 로 주면 option1(핀그룹)이 무시돼 엉뚱한 플래시를 잡는다.
 *
 *   ② XIP 로 실행 중이라 소거·기록 동안 인터럽트를 막아야 한다. ISR 본체가 그
 *      플래시에 있어서 인출하는 순간 죽는다. ROM API 자체는 ROM/RAM 에서 돈다.
 *
 *   ③ 기록 직후 D-cache 가 옛 값을 들고 있다. 무효화하지 않으면 방금 쓴 값을
 *      못 읽고 "실패" 로 오판한다.
 */

#include "flash.h"


#ifdef _USE_HW_FLASH

#include "cli.h"
#include "log.h"

#include "hpm_romapi.h"
#include "hpm_interrupt.h"
#include "hpm_l1c_drv.h"


/* XIP 창의 시작 주소. 호출자는 플래시 오프셋만 다루므로 여기 안에서만 쓴다. */
#define FLASH_XIP_BASE        0x80000000UL

/* board.c 의 nor_cfg_option 과 반드시 같아야 한다 (함정 ①) */
#define NOR_CFG_HEADER        0xFCF90002U
#define NOR_CFG_OPTION0       0x00000006U   /* 120MHz */
#define NOR_CFG_OPTION1       0x00001000U   /* 2번 핀그룹 (PX00~PX07) */


static bool              is_init  = false;
static volatile uint32_t err_step = 0;   /* 1=get_config 2=erase 3=program */
static volatile uint32_t err_stat = 0;
static volatile bool     is_busy  = false;   /* 소거·기록 진행 중 */

#if CLI_USE(HW_FLASH)
static void cliFlash(cli_args_t *args);
#endif


/*
 * ★ 캐시 무효화는 주소와 크기가 **둘 다** 캐시라인 배수여야 한다.
 *
 *   SDK 의 l1c_dc_invalidate() 는 ASSERT_ADDR_SIZE 로 두 조건을 모두 본다.
 *   주소만 내림하고 크기는 `length + 32` 로 넘기면, length 가 32 의 배수가 아닐 때
 *   크기 쪽에서 걸린다 (예: 설정 레코드 536B -> 568, 568 % 32 = 24).
 *
 *   요청 구간을 덮는 캐시라인 경계까지 앞뒤로 늘려서 넘긴다.
 */
static void flashCacheInval(uint32_t xip_addr, uint32_t length)
{
  uint32_t start = HPM_L1C_CACHELINE_ALIGN_DOWN(xip_addr);
  uint32_t end   = HPM_L1C_CACHELINE_ALIGN_UP(xip_addr + length);

  l1c_dc_invalidate(start, end - start);
}

static void flashCfgOption(xpi_nor_config_option_t *p_opt)
{
  p_opt->header.U  = NOR_CFG_HEADER;
  p_opt->option0.U = NOR_CFG_OPTION0;
  p_opt->option1.U = NOR_CFG_OPTION1;
}

bool flashInit(void)
{
  xpi_nor_config_t        nor_cfg;
  xpi_nor_config_option_t cfg_option;
  hpm_stat_t              status;
  uint32_t                mask;

  flashCfgOption(&cfg_option);

  mask   = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  status = rom_xpi_nor_get_config(HPM_XPI0, &nor_cfg, &cfg_option);
  restore_global_irq(mask);

  is_init = (status == status_success);

#if CLI_USE(HW_FLASH)
  cliAdd("flash", cliFlash);
#endif

  logPrintf("[%s] flashInit()\n", is_init ? "OK" : "E_");

  return is_init;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (p_data == NULL || length == 0) return false;

  /* 함정 ③ — 요청 구간을 덮는 캐시라인까지 무효화한다 */
  flashCacheInval(FLASH_XIP_BASE + addr, length);

  memcpy(p_data, (const void *)(FLASH_XIP_BASE + addr), length);

  return true;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  xpi_nor_config_t        nor_cfg;
  xpi_nor_config_option_t cfg_option;
  hpm_stat_t              status;
  uint32_t                mask;

  if (is_init == false) return false;

  flashCfgOption(&cfg_option);

  /* 함정 ② — 소거가 끝날 때까지 인터럽트를 막는다 */
  is_busy = true;
  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);

  err_step = 1;
  status = rom_xpi_nor_get_config(HPM_XPI0, &nor_cfg, &cfg_option);
  if (status == status_success)
  {
    err_step = 2;
    status = rom_xpi_nor_erase(HPM_XPI0, xpi_xfer_channel_auto,
                               &nor_cfg, addr, length);
  }

  restore_global_irq(mask);
  is_busy = false;

  err_stat = status;
  if (status != status_success) return false;

  err_step = 0;
  return true;
}

bool flashWrite(uint32_t addr, const uint8_t *p_data, uint32_t length)
{
  xpi_nor_config_t        nor_cfg;
  xpi_nor_config_option_t cfg_option;
  hpm_stat_t              status;
  uint32_t                mask;

  if (is_init == false)                return false;
  if (p_data == NULL || length == 0)   return false;

  flashCfgOption(&cfg_option);

  is_busy = true;
  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);

  err_step = 1;
  status = rom_xpi_nor_get_config(HPM_XPI0, &nor_cfg, &cfg_option);
  if (status == status_success)
  {
    err_step = 3;
    status = rom_xpi_nor_program(HPM_XPI0, xpi_xfer_channel_auto, &nor_cfg,
                                 (const uint32_t *)p_data, addr, length);
  }

  restore_global_irq(mask);
  is_busy = false;

  err_stat = status;
  if (status != status_success) return false;

  /* 함정 ③ — 방금 쓴 자리를 바로 읽을 수 있게 */
  flashCacheInval(FLASH_XIP_BASE + addr, length);

  err_step = 0;
  return true;
}

/*
 * 지금 플래시를 굽고 있는가.
 *
 * 예외·assert 핸들러가 "부트로더로 넘어가도 되는지" 판단하는 데 쓴다. 소거·기록
 * 도중에 죽었다면 그 자리에서 또 플래시를 건드리면 안 된다.
 */
bool     flashIsReady(void)     { return is_init && !is_busy; }
uint32_t flashGetErrStep(void)   { return err_step; }
uint32_t flashGetErrStatus(void) { return err_stat; }




/*---------------------------------------------------------------------------
 *  CLI
 *---------------------------------------------------------------------------*/
#if CLI_USE(HW_FLASH)

void cliFlash(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("init        : %d\n", is_init);
    cliPrintf("sector size : %d\n", HW_FLASH_SECTOR_SIZE);
    cliPrintf("err step    : %d  (1=cfg 2=erase 3=program)\n", (int)err_step);
    cliPrintf("err status  : %d\n", (int)err_stat);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint32_t addr = args->getData(1);
    uint32_t len  = args->getData(2);
    uint8_t  buf[16];

    for (uint32_t i = 0; i < len; i += 16)
    {
      uint32_t n = ((len - i) > 16) ? 16 : (len - i);

      flashRead(addr + i, buf, n);
      cliPrintf("0x%06X  ", (unsigned)(addr + i));
      for (uint32_t k = 0; k < n; k++) cliPrintf("%02X ", buf[k]);
      cliPrintf("\n");
    }
    ret = true;
  }

  /*
   * 시험용 소거·기록. 주소를 손으로 넣는 만큼 위험하므로 우리 영역 밖은 막는다.
   * 부트로더(0x00000~0x20000)·앱(~0x80000)·상용 EEPROM(~0xC0000) 은 건드리면 안 된다.
   */
  if (args->argc == 2 && args->isStr(0, "erase"))
  {
    uint32_t addr = args->getData(1);

    if (addr < HW_FLASH_USER_BEGIN)
    {
      cliPrintf("[E_] 0x%06X 미만은 쓸 수 없다 (부트로더·앱·상용 EEPROM)\n",
                (unsigned)HW_FLASH_USER_BEGIN);
    }
    else
    {
      uint32_t t = millis();
      bool     ok = flashErase(addr, HW_FLASH_SECTOR_SIZE);

      cliPrintf("erase 0x%06X : %s  (%d ms)\n", (unsigned)addr,
                ok ? "OK" : "E_", (int)(millis() - t));
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("flash info\n");
    cliPrintf("flash read  [addr] [len]\n");
    cliPrintf("flash erase [addr]\n");
  }
}
#endif

#endif
