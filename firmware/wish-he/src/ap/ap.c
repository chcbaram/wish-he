#include "ap.h"
#include "usb/cherryusb/hid_kbd_if.h"




void apInit(void)
{
  moduleInit();
}

void apMain(void)
{
  while(1)
  {
    moduleUpdate();
  }
}

void updateLED(void)
{
  static uint32_t pre_time = 0;


  if (millis() - pre_time >= 500)
  {
    pre_time = millis();
    ledToggle(_DEF_LED1);
  }
}

/*
 * 스캔 결과를 부트 키보드 리포트로 옮긴다.
 *
 * 여기서는 섀도만 갱신하고 실제 전송은 USB 완료 콜백이 이어간다. 그래야 스캔 주기와
 * 리포트 주기가 분리되어, 스캔이 빠르든 느리든 호스트가 물어보는 125us 마다
 * "그 순간의 최신 상태"가 나간다.
 */
static void updateKeyboard(void)
{
  uint8_t  modifier = 0;
  uint8_t  keys[KBD_ROLLOVER];
  uint32_t cnt = 0;


  keysUpdate();

  for (uint16_t row = 0; row < KEYS_STEP_MAX; row++)
  {
    uint16_t bits = keysGetRow(row);

    if (bits == 0) continue;

    for (uint16_t col = 0; col < KEYS_CH_MAX; col++)
    {
      uint8_t kc;

      if ((bits & (1U << col)) == 0) continue;

      kc = keysGetKeycode(row, col);
      if (kc == 0) continue;                        /* 배정 안 된 자리 */

      if (kc >= 0xE0 && kc <= 0xE7)
      {
        modifier |= (uint8_t)(1U << (kc - 0xE0));   /* 모디파이어는 [0] 바이트 비트 */
      }
      else if (cnt < KBD_ROLLOVER)
      {
        keys[cnt++] = kc;                           /* 6키 롤오버 */
      }
    }
  }

  hidKbdSetReport(modifier, keys, cnt);
}

void update(void const *arg)
{
  updateLED();
  updateKeyboard();
  usbUpdate();          /* HID 로 들어온 부트/리셋 요청 처리 */
}

void cliLoopIdle(void)
{
  cliMgrEnable(false);
  moduleUpdate();
  cliMgrEnable(true);
}


MODULE_DEF(ap)
{
  .name     = "ap",
  .priority = MODULE_PRI_LOW,
  .update   = update,
};
