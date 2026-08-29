#include "taphold.h"
#include "ee_user.h"

static uint8_t  hold_okp     = HOLD_OKP_DEFAULT;
static uint16_t tapping_term = TAPPING_TERM_DEFAULT;

void tapHoldInit(void)
{
  hold_okp     = HOLD_OKP_DEFAULT;
  tapping_term = TAPPING_TERM_DEFAULT;
}

void tapHoldSetDefault(uint8_t prof)
{
  if (prof >= KEYMAP_PROFILE_COUNT) return;

  eeprom_update_byte((uint8_t *)EE_USER_HOLD_OKP(prof), HOLD_OKP_DEFAULT);
  eeprom_update_word((uint16_t *)EE_USER_TAPPING_TERM(prof), TAPPING_TERM_DEFAULT);
}

void tapHoldLoad(uint8_t prof)
{
  uint16_t term;

  if (prof >= KEYMAP_PROFILE_COUNT) return;

  hold_okp = eeprom_read_byte((const uint8_t *)EE_USER_HOLD_OKP(prof)) ? 1 : 0;

  term = eeprom_read_word((const uint16_t *)EE_USER_TAPPING_TERM(prof));

  /* 범위를 벗어난 값은 안 믿는다 — 슬라이더 범위가 좁아졌을 수도 있다 */
  if (term < TAPPING_TERM_MIN) term = TAPPING_TERM_MIN;
  if (term > TAPPING_TERM_MAX) term = TAPPING_TERM_MAX;

  tapping_term = term;
}

/*---------------------------------------------------------------------------
 *  QMK 콜백 — per-key 경로로 들어온다 (CMakeLists 의 *_PER_KEY 참고)
 *---------------------------------------------------------------------------*/

bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return (hold_okp != 0);
}

uint16_t get_tapping_term(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return tapping_term;
}

/*
 * 퀵탭텀도 같이 따라가게 한다.
 *
 * ★ 안 그러면 어긋난다. QMK 는 QUICK_TAP_TERM 을 안 주면 TAPPING_TERM 으로 잡는데
 *   (action_tapping.c:26) 그건 **컴파일 때 200 으로 굳는다.** 사용자가 탭텀을 150 으로
 *   내려도 퀵탭텀은 200 으로 남아, 원래 지키려던 `퀵탭텀 <= 탭텀` 이 깨진다.
 */
uint16_t get_quick_tap_term(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return tapping_term;
}

/*---------------------------------------------------------------------------
 *  설정 도구
 *---------------------------------------------------------------------------*/

void tapHoldViaSet(uint8_t *p_val)
{
  /* 값은 **지금 프로파일 칸**에 쓴다 */
  if (p_val[0] == val_hold_okp)
  {
    hold_okp = p_val[1] ? 1 : 0;
    eeprom_update_byte((uint8_t *)EE_USER_HOLD_OKP(keysProfGet()), hold_okp);
  }

  /*
   * ★ 탭텀은 두 바이트, 큰 자리가 먼저다.
   *
   *   앱은 슬라이더 최댓값이 255 를 넘으면 자동으로 2바이트로 보낸다
   *   (range-constraints.ts 의 encodeRangeValue). 500 이 최댓값이라 여기로 온다.
   */
  if (p_val[0] == val_tapping_term)
  {
    uint16_t v = ((uint16_t)p_val[1] << 8) | p_val[2];

    if (v < TAPPING_TERM_MIN) v = TAPPING_TERM_MIN;
    if (v > TAPPING_TERM_MAX) v = TAPPING_TERM_MAX;

    tapping_term = v;
    eeprom_update_word((uint16_t *)EE_USER_TAPPING_TERM(keysProfGet()), tapping_term);
  }
}

void tapHoldViaGet(uint8_t *p_val)
{
  if (p_val[0] == val_hold_okp) p_val[1] = hold_okp;

  if (p_val[0] == val_tapping_term)
  {
    p_val[1] = (uint8_t)(tapping_term >> 8);
    p_val[2] = (uint8_t)(tapping_term & 0xFF);
  }
}
