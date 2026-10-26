#ifndef TAPHOLD_H_
#define TAPHOLD_H_

/*
 * 탭홀드 — 탭과 홀드를 가르는 두 값.
 *
 *   Hold On Other Key Press  탭홀드 키를 누른 채 다른 키를 치면 곧바로 홀드로 확정
 *   Tapping Term             그 판정이 도는 시간 창 (ms)
 *
 * 앞의 것은 **창 안에서만** 물어본다. 창을 벗어나면 무조건 홀드라, 둘을 같이 두어야
 * 사용자가 제 손에 맞출 수 있다.
 */

#include "quantum.h"

#define HOLD_OKP_DEFAULT      1

/*
 * 상한은 자료형이 아니라 타이머가 정한다. WITHIN_TAPPING_TERM 이 TIMER_DIFF_16 을
 * 쓰므로 32767ms 를 넘기면 "지났다" 와 "아직" 을 못 가린다. 500 은 한참 아래다.
 *
 * 하한을 0 으로 두면 안 된다 — `< 0` 이 늘 거짓이라 모든 탭홀드가 즉시 홀드가 된다.
 */
#define TAPPING_TERM_DEFAULT  TAPPING_TERM      /* QMK 기본값 200 을 그대로 쓴다 */
#define TAPPING_TERM_MIN      50
#define TAPPING_TERM_MAX      500

/* 설정 도구의 값 ID — menus.json 과 짝을 맞춘다 */
enum via_taphold_value
{
  val_hold_okp     = 1,
  val_tapping_term = 2,
};

void tapHoldInit(void);
void tapHoldLoad(uint8_t prof);
void tapHoldSetDefault(uint8_t prof);

void tapHoldViaSet(uint8_t *p_val);
void tapHoldViaGet(uint8_t *p_val);

#endif
