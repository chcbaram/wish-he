#include "cli_mgr.h"


#ifdef _USE_HW_CLI

void cliThread(void const *arg);


static uint8_t  cli_ch    = HW_UART_CH_CLI;
static uint32_t cli_baud  = 115200;
static bool     is_enable = true;



bool cliMgrInit(void)
{
  cliOpen(cli_ch, cli_baud);
  cliBegin();
  return true;
}

void cliMgrEnable(bool enable)
{
  is_enable = enable;
}

void cliMgrThread(void const *arg)
{
  /*
   * is_enable 이 false 인 동안은 채널 전환까지 통째로 멈춘다.
   *
   * RTOS 가 없어서 CLI 명령 실행 중에도 cliKeepLoop() -> cliLoopIdle() -> moduleUpdate()
   * 경로로 여기까지 되돌아온다. 전환 로직을 가드 밖에 두면 'usb tx' 같은 처리량 시험 도중
   * CLI 채널이 USB 로 넘어가 시험 데이터와 CLI 출력이 같은 파이프에서 섞인다.
   * cliLoopIdle() 이 이미 cliMgrEnable(false) 를 하므로 여기서 함께 막으면 된다.
   */
  if (is_enable == false) return;

  cliMain();

#if HW_USE_CDC == 1
  /* 우선순위 UART > USB. 아래쪽 대입이 이긴다.
     보레이트 115200 은 "터미널이지 데이터 파이프가 아니다"를 구분하는 신호다. */
  if (cdcIsConnect() && cdcGetBaud() == 115200)
  {
    cli_ch = HW_UART_CH_USB;
  }
  else if (cli_ch == HW_UART_CH_USB)
  {
    cli_ch = HW_UART_CH_CLI;
  }
#endif

  if (uartAvailable(HW_UART_CH_CLI) > 0)
  {
    cli_ch = HW_UART_CH_CLI;
  }

  if (cliGetPort() != cli_ch)
  {
    cliOpen(cli_ch, cli_baud);
    logOpen(cli_ch, cli_baud);    /* 로그도 CLI 를 따라간다 */
  }
}

MODULE_DEF(cli){
  .name     = "cli",
  .priority = MODULE_PRI_LOW,
  .init     = cliMgrInit,
  .update   = cliMgrThread,
};

#endif
