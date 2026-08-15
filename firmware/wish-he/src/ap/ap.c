#include "ap.h"




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

void update(void const *arg)
{
  updateLED();
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
