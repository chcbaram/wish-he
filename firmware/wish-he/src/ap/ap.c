#include "ap.h"
#include "qmk/qmk.h"




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
 * 한 바퀴.
 *
 * ★ 스캔과 키 처리를 갈라 둔다.
 *
 *   keysUpdate() 는 ADC 한 바퀴(31us)를 돌려 깊이를 갱신하고 눌림을 정한다.
 *   qmkUpdate() 는 그 결과 비트마스크를 받아 키맵·레이어를 거쳐 리포트를 만든다.
 *
 *   둘의 주기가 달라도 된다는 게 요점이다. 나중에 래피드 트리거를 넣으면 판정은
 *   스캔 주기에서 돌아야 하는데, QMK 루프가 느려져도 그쪽은 영향을 안 받는다.
 */
void update(void const *arg)
{
  updateLED();

  keysUpdate();                       /* ADC 스캔 + 눌림 판정 */
  if (qmkIsOn()) qmkUpdate();         /* 키맵 · 레이어 · 매크로 · VIA -> HID 리포트 */

  usbUpdate();                        /* HID 로 들어온 부트/리셋/트래킹 요청 처리 */
  keysCfgUpdate();                    /* 바뀐 설정을 조용해진 뒤 한 번 저장 */
  keysSwUpdate();                     /* 바뀐 스위치 정의도 (플래시라 ISR 밖) */
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
  .init     = qmkCliInit,
  .update   = update,
};
