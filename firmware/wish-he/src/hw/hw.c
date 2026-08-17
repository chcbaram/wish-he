#include "hw.h"
#include "hpm_crc32.h"



extern uint32_t _fw_flash_begin;

volatile const firm_ver_t firm_ver __attribute__((section(".version"))) =
{
  .magic_number = VERSION_MAGIC_NUMBER,
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin
};

/*
 * 이미지 태그 — 빌드 뒤 `tools/make_release.py` 가 채운다.
 *
 * ★ 여기 0 으로 두는 것이 정상이다. 값이 안 채워진 이미지(개발 빌드)는 검사를
 *   **건너뛴다** — JTAG 나 iap_update.py 로 바로 구운 것을 막으면 안 된다.
 */
volatile const firm_tag_t firm_tag __attribute__((used, section(".tag"))) =
{
  .magic_number = TAG_MAGIC_NUMBER,
  .fw_addr      = 0,
  .fw_size      = 0,
  .fw_crc       = 0,
  .tag_crc      = 0
};


static void hwPrintFault(void);
static void hwVerifyFirm(void);


/*
 * 굽다 만 이미지가 그대로 도는 것을 막는다.
 *
 * ★ IAP 는 매직 넉 자만 본다.
 *
 *   보드의 부트로더는 0x80020000 의 "HPM\n" 만 확인하고 뛴다 — 길이도 CRC 도 안 본다
 *   (docs/board-iap.md 2절). 그런데 그 넉 자는 **맨 먼저** 써지므로 중간에 끊긴
 *   이미지도 부트로더에게는 멀쩡해 보인다. 그러면 반쯤 써진 앱이 돌다가 USB 도 못
 *   올리고, 되살릴 길이 PA09 버튼이나 JTAG 밖에 안 남는다.
 *
 *   부트로더는 벤더 것이라 못 고친다. 그래서 **앱이 스스로 본다.**
 *
 * ★ 태그가 안 채워져 있으면 건너뛴다.
 *
 *   tools/make_release.py 가 채운 배포본만 검사 대상이다. 개발 중에 JTAG 나
 *   iap_update.py 로 바로 구운 이미지는 태그가 0 이라 그냥 지나간다 — 안 그러면
 *   빌드할 때마다 업데이트 모드로 튕긴다.
 */
static void hwVerifyFirm(void)
{
  const firm_tag_t *p_tag = (const firm_tag_t *)&firm_tag;
  uint32_t crc;

  if (p_tag->magic_number != TAG_MAGIC_NUMBER || p_tag->fw_size == 0)
  {
    logPrintf("Booting..Tag  \t\t: 없음 (검사 안 함)\r\n");
    return;
  }

  crc = crc32((const uint8_t *)((uint32_t)&_fw_flash_begin + p_tag->fw_addr),
              p_tag->fw_size);

  if (crc == p_tag->fw_crc)
  {
    logPrintf("Booting..Tag  \t\t: OK  %d B  crc 0x%08X\r\n",
              (int)p_tag->fw_size, (unsigned int)crc);
    return;
  }

  /*
   * 어긋났다 — 업데이트 모드로 되돌린다.
   *
   * 로그를 먼저 뱉고 나간다. resetToBoot() 는 돌아오지 않으므로 뒤에 아무것도
   * 두지 않는다.
   */
  logPrintf("[E_] 펌웨어 CRC 불일치 — 0x%08X 여야 하는데 0x%08X 다\r\n",
            (unsigned int)p_tag->fw_crc, (unsigned int)crc);
  logPrintf("[E_] 업데이트 모드로 되돌아간다\r\n");
  delay(200);                 /* CDC 로 문구가 나갈 시간 */
  resetToBoot();
}



bool hwInit(void)
{
  cliInit();
  logInit();
  resetInit();
  flashInit();
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

  hwVerifyFirm();

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
