#include "bsp.h"
#include "hw_def.h"
#include "cli.h"
#include "hpm5361_it.h"

#include "hpm_interrupt.h"




bool bspInit(void)
{
  /*
   * board_init() 이 아래를 모두 처리한다.
   *   init_py_pins_as_pgpio() / board_init_usb_dp_dm_pins()
   *   board_init_clock()    : PLL0 960MHz, CPU0 480MHz, AXI/AHB 160MHz, mchtmr 24MHz
   *   board_init_console()  : UART0(PA00/PA01) 115200 + printf retarget
   *   board_init_pmp()      : hpm5300evk 에서는 빈 함수
   *
   * L1 캐시(I$ 16KB / D$ 16KB)는 start.S 가 c_startup() 이전에 이미 켜 두었다.
   */
  board_init();

  /* 1ms 틱 개시 (mchtmr) */
  itInit();

  return true;
}

void delay(uint32_t ms)
{
  uint32_t pre_time = millis();

  while ((millis() - pre_time) < ms)
  {
    cliLoopIdle();
  }
}

void delayUs(uint32_t delay_us)
{
  uint32_t pre_time = micros();

  while (micros() - pre_time <= delay_us)
  {
    //
  }
}

uint32_t millis(void)
{
  return itGetTickMs();
}

uint32_t micros(void)
{
  return itGetTickUs();
}

void Error_Handler(void)
{
  __asm volatile("ebreak");

  disable_global_irq(CSR_MSTATUS_MIE_MASK);
  while (1)
  {
  }
}
