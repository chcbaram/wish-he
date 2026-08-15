/*
 * flash.c  —  내장 XPI NOR 접근 (ROM API)
 *
 * reset.c 가 부트 플래그 하나를 쓰려고 갖고 있던 ROM API 절차를 일반화한 것이다.
 * 캘리브레이션 저장(8편)과 VIA EEPROM(10편)이 같이 쓴다.
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


/* board.c 의 nor_cfg_option 과 반드시 같아야 한다 (함정 ①) */
#define NOR_CFG_HEADER        0xFCF90002U
#define NOR_CFG_OPTION0       0x00000006U   /* 120MHz */
#define NOR_CFG_OPTION1       0x00001000U   /* 2번 핀그룹 (PX00~PX07) */


static bool              is_init  = false;
static volatile uint32_t err_step = 0;   /* 1=get_config 2=erase 3=program */
static volatile uint32_t err_stat = 0;

#if CLI_USE(HW_FLASH)
static void cliFlash(cli_args_t *args);
#endif


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

  /* 함정 ③ — 캐시 라인 경계까지 넉넉히 무효화한다 */
  l1c_dc_invalidate((FLASH_XIP_BASE + addr) & ~(HPM_L1C_CACHELINE_SIZE - 1),
                    length + HPM_L1C_CACHELINE_SIZE);

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

  err_stat = status;
  if (status != status_success) return false;

  /* 함정 ③ — 방금 쓴 자리를 바로 읽을 수 있게 */
  l1c_dc_invalidate((FLASH_XIP_BASE + addr) & ~(HPM_L1C_CACHELINE_SIZE - 1),
                    length + HPM_L1C_CACHELINE_SIZE);

  err_step = 0;
  return true;
}

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
    cliPrintf("sector size : %d\n", FLASH_SECTOR_SIZE);
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
      bool     ok = flashErase(addr, FLASH_SECTOR_SIZE);

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
