#include "ghost.h"
#include "socd.h"
#include "ee_user.h"
#include "action_util.h"
#include "keycode_config.h"

static ghost_cfg_t ghost;

static uint8_t  key_cnt   = 0;      /* 지금 눌려 있는 기본 키 수 */
static uint8_t  key_cnt_p = 0;      /* 한 주기 전의 값 */
static uint32_t change_ms = 0;      /* 키 구성이 마지막으로 바뀐 때 */
static uint32_t beat_ms   = 0;      /* 마지막 연타를 보낸 때 */
static bool     running   = false;

/*
 * 박자를 몇 번 쳤나 — **시도한 횟수**다.
 *
 * ★ 실제로 나간 리포트 수와 견주라고 둔다.
 *
 *   한 박자는 리포트 두 개다(빈 것 -> 원래대로). 그런데 아래 계층은 큐가 아니라
 *   **최신 상태 한 칸**이라, 앞 전송이 아직 나가 있으면 빈 리포트가 원래 리포트로
 *   덮여 **그 박자가 통째로 사라진다.** 호스트는 아무 변화도 못 본다.
 *
 *   그것을 가리려면 "몇 번 치려 했나" 와 "몇 개가 나갔나" 를 나란히 놓아야 한다.
 *   나간 것만 세면 안 나간 것이 안 보인다.
 */
static uint32_t beat_cnt = 0;

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

/*
 * 지금 **리포트에 들어 있는** 기본 키 수.
 *
 * ★ 사건(누름/놓음)을 세지 않는다. 짝이 깨지기 때문이다.
 *
 *   예전에는 process_record 훅에서 누름에 +1, 놓음에 -1 을 했다. 그런데 그 앞에
 *   게이트가 셋 있었다 — enable, socdIsUsed, IS_BASIC_KEYCODE. **누름과 놓음
 *   사이에 그중 하나라도 바뀌면 짝이 영영 깨진다.**
 *
 *   재현했다. 두 키를 눌러 연타를 돌리는 중에 그중 하나를 SOCD 묶음에 넣으면,
 *   놓을 때 socdIsUsed 가 참이라 -1 이 실행되지 않는다. key_cnt 가 1 남고
 *   running 은 key_cnt == 0 에서만 꺼지므로 영영 안 꺼진다. 그 뒤로는
 *   **키 하나만 눌러도 2 가 되어 연타가 걸린다.**
 *
 *   탭홀드의 합성 릴리즈(누른 적 없는 놓음 사건)와 프로파일 전환(같은 자리가 다른
 *   키코드가 된다)도 같은 짝을 깬다. 게이트를 하나씩 고치는 것으로는 안 끝난다.
 *
 *   리포트를 세면 그 문제가 통째로 없어진다. **지금 호스트가 눌린 것으로 아는
 *   것**이 곧 답이고, 거기엔 짝이라는 개념이 없다.
 *
 * ★ SOCD 와 다툴 일도 없다. ghostBeat 은 지금 리포트를 그대로 되돌려 놓을 뿐
 *   키를 지어내지 않는다 — SOCD 가 뺀 키는 애초에 리포트에 없다.
 */
static uint8_t ghostHeldCount(void)
{
  uint8_t n = 0;

#ifdef NKRO_ENABLE
  if (keymap_config.nkro)
  {
    for (uint32_t i = 0; i < NKRO_REPORT_BITS; i++)
      n += (uint8_t)__builtin_popcount(nkro_report->bits[i]);
    return n;
  }
#endif

  for (uint32_t i = 0; i < KEYBOARD_REPORT_KEYS; i++)
    if (keyboard_report->keys[i] != KC_NO) n++;

  return n;
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
#ifdef NKRO_ENABLE
  report_nkro_t saved_nkro;
#endif

  /*
   * ★ **clear_keys() 앞에서** 저장해야 한다. 그 함수는 NKRO 를 알아서, NKRO 가
   *   물려 있으면 nkro_report 를 비운다. 뒤에서 저장하면 이미 비워진 것을 담는다.
   *
   * ★ 두 벌을 다 저장한다. NKRO 일 때 키는 keyboard_report 가 아니라 nkro_report
   *   로 나간다 (action_util.c 의 send_keyboard_report). 6KRO 것만 되돌리면 키가
   *   비워진 채로 남아 연타가 멈춘다.
   */
  memcpy(&saved, keyboard_report, sizeof(saved));
#ifdef NKRO_ENABLE
  memcpy(&saved_nkro, nkro_report, sizeof(saved_nkro));
#endif

  clear_keys();
  send_keyboard_report();

  memcpy(keyboard_report, &saved, sizeof(saved));
#ifdef NKRO_ENABLE
  memcpy(nkro_report, &saved_nkro, sizeof(saved_nkro));
#endif
  send_keyboard_report();

  beat_cnt++;
}

uint32_t ghostGetBeatCount(void)
{
  return beat_cnt;
}

void ghostUpdate(void)
{
  uint32_t now = millis();

  if (ghost.enable == 0) return;

  /* 리포트에서 센다. 바뀌었으면 지연을 처음부터 다시 센다 */
  {
    uint8_t n = ghostHeldCount();

    if (n != key_cnt)
    {
      key_cnt   = n;
      change_ms = now;
    }
  }

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
