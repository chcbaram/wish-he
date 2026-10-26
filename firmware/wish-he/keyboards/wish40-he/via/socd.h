#ifndef SOCD_H_
#define SOCD_H_

/*
 * SOCD — 마주 보는 두 키를 동시에 누르면 **나중에 누른 쪽**만 살린다.
 *
 * 게임에서 좌우(A/D)나 상하(W/S)를 같이 누르면 캐릭터가 멈춘다. 나중에 누른 쪽만
 * 남기면 방향이 즉시 뒤집혀 손이 빨라진다. 묶음을 둘 두어 좌우와 상하를 따로 잡는다.
 */

#include "quantum.h"

#define SOCD_PAIR_CNT   2

typedef struct PACKED
{
  uint8_t  enable;
  uint16_t keycode[2];
} socd_cfg_t;

/* 설정 도구의 값 ID — menus.json 과 짝을 맞춘다 */
enum via_socd_value
{
  val_socd_enable = 1,
  val_socd_key1   = 2,
  val_socd_key2   = 3,
};

void socdInit(void);
void socdLoad(uint8_t prof);              /* 그 프로파일 칸에서 올린다 */
void socdSetDefault(uint8_t prof);        /* EEPROM 그 칸을 기본값으로 */
void socdProcess(uint16_t keycode, keyrecord_t *record);
bool socdIsUsed(uint16_t keycode);        /* 연타가 안 건드리게 하려고 본다 */

/* VIA 커스텀 메뉴 — pair 는 0..SOCD_PAIR_CNT-1 */
void socdViaSet(uint8_t pair, uint8_t *p_val);
void socdViaGet(uint8_t pair, uint8_t *p_val);

#endif
