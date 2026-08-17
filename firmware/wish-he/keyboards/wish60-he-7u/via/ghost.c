#include "ghost.h"
#include "socd.h"
#include "ee_user.h"

static ghost_cfg_t ghost;

static uint8_t  key_cnt   = 0;      /* 지금 눌려 있는 기본 키 수 */
static uint8_t  key_cnt_p = 0;      /* 한 주기 전의 값 */
static uint32_t change_ms = 0;      /* 키 구성이 마지막으로 바뀐 때 */
static uint32_t beat_ms   = 0;      /* 마지막 연타를 보낸 때 */
static bool     running   = false;

void ghostInit(void)
{
  ghost.enable = 0;
  ghost.delay  = GHOST_DELAY_DEFAULT;
  ghost.repeat = GHOST_REPEAT_DEFAULT;

  key_cnt = key_cnt_p = 0;
  running = false;
}

void ghostSetDefault(uint8_t prof)
{
  ghost_cfg_t blank = {0, GHOST_DELAY_DEFAULT, GHOST_REPEAT_DEFAULT};

  if (prof >= KEYMAP_PROFILE_COUNT) return;

  eeprom_update_block(&blank, (void *)EE_USER_GHOST(prof), sizeof(blank));
}

void ghostLoad(uint8_t prof)
{
  if (prof >= KEYMAP_PROFILE_COUNT) return;

  eeprom_read_block(&ghost, (const void *)EE_USER_GHOST(prof), sizeof(ghost));

  ghost.enable = ghost.enable ? 1 : 0;

  /* 0 이면 나눗셈 아닌 곳에서도 곤란하다 — 쉬지 않고 때리게 된다 */
  if (ghost.delay  == 0) ghost.delay  = GHOST_DELAY_DEFAULT;
  if (ghost.repeat == 0) ghost.repeat = GHOST_REPEAT_DEFAULT;

  /* 프로파일이 바뀌면 세던 것을 버린다 — 키 구성이 통째로 달라진다 */
  key_cnt = key_cnt_p = 0;
  running = false;
}

void ghostProcess(uint16_t keycode, keyrecord_t *record)
{
  if (ghost.enable == 0) return;

  /*
   * ★ SOCD 에 물린 키는 세지 않는다.
   *
   *   둘이 같은 키를 두고 다투면 방향이 튄다 — SOCD 가 방금 뺀 키를 연타가 다시
   *   넣어 버린다.
   */
  if (socdIsUsed(keycode)) return;
  if (!IS_BASIC_KEYCODE(keycode)) return;

  if (record->event.pressed) key_cnt++;
  else if (key_cnt > 0)      key_cnt--;

  /* 키 구성이 바뀌면 지연을 처음부터 다시 센다 */
  change_ms = millis();
}

/*
 * ★ 리포트를 두 번 보낸다 — 빈 것 한 번, 원래대로 한 번.
 *
 *   그래야 호스트가 "뗐다 다시 눌렀다" 로 본다. 하나만 보내면 상태가 안 변해
 *   아무 일도 안 일어난다.
 *
 * ★ 지금 눌린 것을 그대로 되돌려 놓는다. 여기서 키를 지어내지 않는다 — 사용자가
 *   실제로 누르고 있는 것만 다시 보낸다.
 */
static void ghostBeat(void)
{
  report_keyboard_t saved;

  memcpy(&saved, keyboard_report, sizeof(saved));

  clear_keys();
  send_keyboard_report();

  memcpy(keyboard_report, &saved, sizeof(saved));
  send_keyboard_report();
}

void ghostUpdate(void)
{
  uint32_t now = millis();

  if (ghost.enable == 0) return;

  if (running == false)
  {
    /* 두 키 이상을 지연시간만큼 쥐고 있으면 시작한다 */
    if (key_cnt >= 2 && (now - change_ms) >= (uint32_t)ghost.delay * GHOST_TIME_UNIT)
    {
      running = true;
      beat_ms = now;
    }
  }
  else if (key_cnt == 0)
  {
    running = false;
  }

  if (running == false) return;

  if ((now - beat_ms) < (uint32_t)ghost.repeat * GHOST_TIME_UNIT) return;

  beat_ms = now;

  /*
   * ★ 마지막 한 번은 키가 하나로 줄어든 뒤에도 보낸다.
   *
   *   둘 중 하나를 놓는 순간 끊어 버리면, 남은 한 키가 눌린 채로 조용해진다.
   *   한 박자를 더 보내 그 키가 살아 있음을 알린다.
   */
  if (key_cnt >= 2 || (key_cnt == 1 && key_cnt_p >= 2)) ghostBeat();

  key_cnt_p = key_cnt;
}

/*---------------------------------------------------------------------------
 *  설정 도구
 *---------------------------------------------------------------------------*/

static void ghostSave(void)
{
  eeprom_update_block(&ghost, (void *)EE_USER_GHOST(keysProfGet()), sizeof(ghost));
}

void ghostViaSet(uint8_t *p_val)
{
  switch (p_val[0])
  {
    case val_ghost_enable:
      ghost.enable = p_val[1] ? 1 : 0;
      running = false;              /* 켜고 끌 때 세던 것을 버린다 */
      key_cnt = key_cnt_p = 0;
      break;

    case val_ghost_delay:
      if (p_val[1] == 0) return;
      ghost.delay = p_val[1];
      break;

    case val_ghost_repeat:
      if (p_val[1] == 0) return;
      ghost.repeat = p_val[1];
      break;

    default:
      return;
  }

  ghostSave();
}

void ghostViaGet(uint8_t *p_val)
{
  switch (p_val[0])
  {
    case val_ghost_enable: p_val[1] = ghost.enable; break;
    case val_ghost_delay:  p_val[1] = ghost.delay;  break;
    case val_ghost_repeat: p_val[1] = ghost.repeat; break;
    default: break;
  }
}
