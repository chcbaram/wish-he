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
/*
 * QMK 기동.
 *
 * 이식 중에는 부팅 때 켜지 않고 `qmk start` 로만 켰다. qmkInit() 안에서 죽으면
 * USB 가 통째로 안 올라와 부트로더 핀을 눌러야만 살아나기 때문이다. 지금은
 * 동작이 확인돼서 부팅 때 켠다.
 *
 * `qmk start` 는 남겨둔다 — 앞으로 QMK 쪽을 건드리다 부팅이 막히면 다시
 * 수동으로 돌릴 수 있게 하는 게 싸다.
 */
static bool is_qmk_on = false;

bool apQmkStart(void)
{
  if (is_qmk_on) return true;

  is_qmk_on = qmkInit();
  return is_qmk_on;
}

bool apQmkIsOn(void)
{
  return is_qmk_on;
}

void update(void const *arg)
{
  updateLED();

  keysUpdate();                       /* ADC 스캔 + 눌림 판정 */
  if (is_qmk_on) qmkUpdate();         /* 키맵 · 레이어 · 매크로 · VIA -> HID 리포트 */

  usbUpdate();                        /* HID 로 들어온 부트/리셋/트래킹 요청 처리 */
  keysCfgUpdate();                    /* 바뀐 설정을 조용해진 뒤 한 번 저장 */
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
