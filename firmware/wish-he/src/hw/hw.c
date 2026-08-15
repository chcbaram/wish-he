#include "hw.h"



extern uint32_t _fw_flash_begin;

volatile const firm_ver_t firm_ver __attribute__((section(".version"))) =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin
};


static void hwPrintFault(void);



bool hwInit(void)
{
  cliInit();
  logInit();
  resetInit();          /* 부팅 안전망 포함 — 연속 실패가 쌓였으면 돌아오지 않는다 */
  swtimerInit();
  ledInit();
  ws2812Init();
  keysInit();
  uartInit();
  for (int i=0; i<HW_UART_MAX_CH; i++)
  {
    uartOpen(i, 115200);
  }

  logOpen(HW_LOG_CH, 115200);
  logPrintf("\r\n[ Firmware Begin... ]\r\n");
  logPrintf("Booting..Name \t\t: %s\r\n", _DEF_BOARD_NAME);
  logPrintf("Booting..Ver  \t\t: %s\r\n", _DEF_FIRMWATRE_VERSION);
  logPrintf("Booting..Clock\t\t: %d Mhz\r\n", (int)(clock_get_frequency(clock_cpu0)/1000000));
  logPrintf("Booting..Date \t\t: %s\r\n", __DATE__);
  logPrintf("Booting..Time \t\t: %s\r\n", __TIME__);
  logPrintf("Booting..Addr \t\t: 0x%X\r\n", (uint32_t)&_fw_flash_begin);

  resetLog();

  logPrintf("\n");

  hwPrintFault();

  /* 순서 중요 : cdcInit() 이 q_rx/q_tx 를 만든 뒤에 usbInit() 이 스택을 올려야 한다.
     반대면 초기화 안 된 링버퍼에 ISR 이 qbufferWrite 하는 레이스가 된다. */
  cdcInit();
  usbInit();

  logBoot(false);

  return true;
}

void hwPrintFault(void)
{
  const fault_log_t *p_log;

  if (itFaultIsValid() == false)
  {
    return;
  }

  p_log = itFaultGet();

  logPrintf("[E_] Fault Detected (%d)\r\n", (int)p_log->count);
  logPrintf("     mcause \t\t: 0x%08X\r\n", p_log->mcause);
  logPrintf("     mepc   \t\t: 0x%08X\r\n", p_log->mepc);
  logPrintf("     mtval  \t\t: 0x%08X\r\n", p_log->mtval);
  logPrintf("\n");

  itFaultClear();
}
