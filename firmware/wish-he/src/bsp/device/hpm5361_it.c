/*
 * hpm5361_it.c
 *
 * 중앙 인터럽트/예외 처리 파일. STM32 의 stm32h5xx_it.c 에 해당한다.
 *
 * RISC-V (HPM5361) 의 인터럽트 라우팅은 두 갈래다.
 *
 *   mtvec = __vector_table                      (벡터드 모드 = 기본, start.S)
 *    ├─ [0] irq_handler_trap  (SDK trap.c)  ─┬─ exception_handler()  ← ARM HardFault 대응
 *    │                                       ├─ mchtmr_isr()         ← ARM SysTick  대응
 *    │                                       ├─ swi_isr()
 *    │                                       └─ syscall_handler()    (ecall)
 *    └─ [N] SDK_DECLARE_EXT_ISR_M 로 만든 주변장치 ISR
 *           — PLIC 벡터드라 trap 핸들러를 거치지 않고 곧바로 진입한다
 *
 * 위 네 개 훅은 SDK trap.c 에 weak 로 비어 있고, 이 파일에서 재정의한다.
 */

#include "hpm5361_it.h"
#include "bsp.h"

#include "hpm_mchtmr_drv.h"
#include "hpm_ppor_drv.h"
#include "hpm_interrupt.h"
#include "hpm_csr_drv.h"

#ifdef _USE_HW_SWTIMER
#include "swtimer.h"
#endif


/* 리셋 후에도 보존되는 영역. 링커 스크립트의 .noinit 은 .bss 바깥에 있어
   start.S 의 c_startup() 이 0 으로 밀지 않는다. */
static __attribute__((section(".noinit"))) fault_log_t fault_log;

static volatile uint32_t tick_ms   = 0;
static uint32_t          tick_hz   = 0;   /* mchtmr 클럭 (Hz)  */
static uint32_t          tick_us   = 0;   /* 1us 당 카운트     */
static uint32_t          tick_load = 0;   /* 1ms 당 카운트     */




bool itInit(void)
{
  tick_hz   = clock_get_frequency(clock_mchtmr0);
  tick_us   = tick_hz / 1000000;
  tick_load = tick_hz / 1000;

  if (tick_us == 0)
  {
    tick_us = 1;
  }

  /* wfi 상태에서도 mchtmr 인터럽트가 살아있도록 CPU 클럭을 유지한다 */
  board_ungate_mchtmr_at_lp_mode();

  tick_ms = 0;
  mchtmr_set_compare_value(HPM_MCHTMR, mchtmr_get_count(HPM_MCHTMR) + tick_load);

  /* 전역 인터럽트(MSTATUS.MIE)는 system_init() 이 이미 켜 두었다 */
  enable_mchtmr_irq();

  return true;
}

uint32_t itGetTickMs(void)
{
  return tick_ms;
}

uint32_t itGetTickUs(void)
{
  return (uint32_t)(mchtmr_get_count(HPM_MCHTMR) / tick_us);
}

bool itFaultIsValid(void)
{
  return (fault_log.magic == FAULT_LOG_MAGIC);
}

const fault_log_t *itFaultGet(void)
{
  return &fault_log;
}

void itFaultClear(void)
{
  fault_log.magic = 0;
  fault_log.count = 0;
}




/*---------------------------------------------------------------------------
 *  1ms 틱  —  ARM SysTick_Handler 대응
 *---------------------------------------------------------------------------*/
SDK_DECLARE_MCHTMR_ISR(isr_mchtmr)
void isr_mchtmr(void)
{
  mchtmr_set_compare_value(HPM_MCHTMR, mchtmr_get_count(HPM_MCHTMR) + tick_load);

  tick_ms++;

#ifdef _USE_HW_SWTIMER
  swtimerISR();
#endif
}


/*---------------------------------------------------------------------------
 *  예외 처리  —  ARM HardFault_Handler 대응
 *
 *  ARM 은 예외 스택 프레임(R0..PSR)을 스냅샷했지만 RISC-V 에는 그런 구조가 없다.
 *  대신 mcause / mepc / mtval CSR 세 개가 그 역할을 한다.
 *---------------------------------------------------------------------------*/
long exception_handler(long cause, long epc)
{
  if (fault_log.magic != FAULT_LOG_MAGIC)
  {
    fault_log.count = 0;
  }

  fault_log.magic  = FAULT_LOG_MAGIC;
  fault_log.mcause = (uint32_t)cause;
  fault_log.mepc   = (uint32_t)epc;
  fault_log.mtval  = (uint32_t)read_csr(CSR_MTVAL);
  fault_log.count++;

  /* 디버거가 붙어 있으면 여기서 멈춘다 */
  __asm volatile("ebreak");

  ppor_sw_reset(HPM_PPOR, 10);

  while (1)
  {
  }

  return epc;
}
