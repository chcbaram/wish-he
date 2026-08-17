#ifndef GHOST_H_
#define GHOST_H_

/*
 * 눌러 두면 연타 — 누르고 있는 것을 한 번으로 세는 게임을 위해.
 *
 * 두 키 이상을 지연시간만큼 누르고 있으면 그때부터 반복시간마다 **다 뗐다 다시 누른**
 * 리포트를 보낸다. 손은 그대로 두는데 입력은 연타가 된다.
 *
 * ★ 두 키 이상일 때만이다. 한 키는 OS 자동반복이 이미 해 준다.
 */

#include "quantum.h"

/*
 * 시간은 10ms 단위로 담는다. 설정 도구의 드롭다운 값이 그대로 들어와 한 바이트면
 * 족하다 (50~300ms → 5~30).
 */
#define GHOST_TIME_UNIT       10
#define GHOST_DELAY_DEFAULT   20    /* 200ms */
#define GHOST_REPEAT_DEFAULT  8     /*  80ms */

typedef struct PACKED
{
  uint8_t enable;
  uint8_t delay;    /* 10ms 단위 */
  uint8_t repeat;   /* 10ms 단위 */
} ghost_cfg_t;

/* 설정 도구의 값 ID — menus.json 과 짝을 맞춘다 */
enum via_ghost_value
{
  val_ghost_enable = 1,
  val_ghost_delay  = 2,
  val_ghost_repeat = 3,
};

void ghostInit(void);
void ghostLoad(uint8_t prof);
void ghostSetDefault(uint8_t prof);
void ghostProcess(uint16_t keycode, keyrecord_t *record);
void ghostUpdate(void);                  /* 메인 루프에서 부른다 */

void ghostViaSet(uint8_t *p_val);
void ghostViaGet(uint8_t *p_val);

#endif
