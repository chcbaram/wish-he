#include "socd.h"
#include "ee_user.h"

static socd_cfg_t socd[SOCD_PAIR_CNT];

/* 어느 쪽이 눌려 있는지 기억한다 — 놓을 때 반대쪽을 되살리려고 */
static bool socd_down[SOCD_PAIR_CNT][2];

void socdInit(void)
{
  for (uint8_t s = 0; s < SOCD_PAIR_CNT; s++)
  {
    socd[s].enable     = 0;
    socd[s].keycode[0] = KC_NO;
    socd[s].keycode[1] = KC_NO;

    socd_down[s][0] = false;
    socd_down[s][1] = false;
  }
}

void socdSetDefault(uint8_t prof)
{
  socd_cfg_t blank = {0, {KC_NO, KC_NO}};

  if (prof >= KEYMAP_PROFILE_COUNT) return;

  for (uint8_t s = 0; s < SOCD_PAIR_CNT; s++)
  {
    eeprom_update_block(&blank, (void *)EE_USER_SOCD(prof, s), sizeof(blank));
  }
}

void socdLoad(uint8_t prof)
{
  if (prof >= KEYMAP_PROFILE_COUNT) return;

  for (uint8_t s = 0; s < SOCD_PAIR_CNT; s++)
  {
    eeprom_read_block(&socd[s], (const void *)EE_USER_SOCD(prof, s),
                      sizeof(socd[s]));

    socd[s].enable = socd[s].enable ? 1 : 0;

    /*
     * ★ 눌림 기억을 반드시 지운다.
     *
     *   프로파일을 옮기면 키 구성이 통째로 바뀐다. 옛 기억이 남아 있으면 다음에
     *   한쪽을 놓을 때 **누른 적도 없는 반대 키를 눌러 버린다.**
     */
    socd_down[s][0] = false;
    socd_down[s][1] = false;
  }
}

/*
 * ★ 리포트를 여기서 보내지 않는다.
 *
 *   이 함수는 QMK 가 리포트를 만드는 도중에 불리고, 끝나면 QMK 가 알아서 보낸다.
 *   여기서 또 보내면 같은 상태가 두 번 나간다.
 */
void socdProcess(uint16_t keycode, keyrecord_t *record)
{
  if (keycode == KC_NO) return;

  for (uint8_t s = 0; s < SOCD_PAIR_CNT; s++)
  {
    if (socd[s].enable == 0) continue;

    for (uint8_t i = 0; i < 2; i++)
    {
      uint8_t other = 1 - i;

      if (keycode != socd[s].keycode[i]) continue;

      if (record->event.pressed)
      {
        socd_down[s][i] = true;

        /* 나중에 누른 쪽이 이긴다 — 반대 키를 리포트에서 뺀다 */
        if (socd_down[s][other]) del_key(socd[s].keycode[other]);
      }
      else
      {
        socd_down[s][i] = false;

        /*
         * ★ 되살리는 것이 핵심이다.
         *
         *   나중 키를 놓았는데 앞서 누른 키가 아직 눌려 있으면 그쪽을 다시 넣는다.
         *   안 그러면 손은 누르고 있는데 아무것도 안 나가는 상태로 남는다.
         */
        if (socd_down[s][other]) add_key(socd[s].keycode[other]);
      }
    }
  }
}

bool socdIsUsed(uint16_t keycode)
{
  if (keycode == KC_NO) return false;

  for (uint8_t s = 0; s < SOCD_PAIR_CNT; s++)
  {
    if (socd[s].enable == 0) continue;
    if (keycode == socd[s].keycode[0]) return true;
    if (keycode == socd[s].keycode[1]) return true;
  }
  return false;
}

/*---------------------------------------------------------------------------
 *  설정 도구
 *---------------------------------------------------------------------------*/

static void socdSave(uint8_t pair)
{
  eeprom_update_block(&socd[pair], (void *)EE_USER_SOCD(keysProfGet(), pair),
                      sizeof(socd[pair]));
}

void socdViaSet(uint8_t pair, uint8_t *p_val)
{
  if (pair >= SOCD_PAIR_CNT) return;

  switch (p_val[0])
  {
    case val_socd_enable:
    {
      bool was_on = (socd[pair].enable != 0);

      socd[pair].enable = p_val[1] ? 1 : 0;

      /*
       * ★ 끌 때는 **억눌러 둔 키를 되살린다.**
       *
       *   억제는 리포트를 한 번 고치는 일이다(del_key). 되살리는 것은 상대 키를
       *   뗄 때뿐인데, 여기서 눌림 기억을 지워 버리면 그 조건이 영영 거짓이 되어
       *   **손은 누르고 있는데 호스트에는 없는** 상태가 된다.
       *
       *   재현했다 — X 를 누르고 W 를 눌러 X 가 억눌린 상태에서 SOCD 를 끄고 W 만
       *   떼면 호스트에 아무 키도 안 남는다. X 를 뗐다 다시 눌러야 산다.
       *
       *   꺼진 뒤에는 두 키가 다 눌려 있는 것이 맞는 상태다. 지금 눌려 있는 것을
       *   전부 다시 넣는다. 이미 들어 있는 키에 add_key 를 해도 무해하다.
       */
      if (was_on && socd[pair].enable == 0)
      {
        for (uint8_t i = 0; i < 2; i++)
        {
          if (socd_down[pair][i] && socd[pair].keycode[i] != KC_NO)
            add_key(socd[pair].keycode[i]);
        }
        send_keyboard_report();
      }

      /* 눌림 기억은 지운다 — 켠 뒤의 판정은 다음 누름부터 새로 센다 */
      socd_down[pair][0] = false;
      socd_down[pair][1] = false;
      break;
    }

    /* 키코드는 두 바이트, 큰 자리가 먼저다 */
    case val_socd_key1:
      socd[pair].keycode[0] = ((uint16_t)p_val[1] << 8) | p_val[2];
      break;

    case val_socd_key2:
      socd[pair].keycode[1] = ((uint16_t)p_val[1] << 8) | p_val[2];
      break;

    default:
      return;
  }

  socdSave(pair);
}

void socdViaGet(uint8_t pair, uint8_t *p_val)
{
  uint16_t v;

  if (pair >= SOCD_PAIR_CNT) return;

  switch (p_val[0])
  {
    case val_socd_enable:
      p_val[1] = socd[pair].enable;
      return;

    case val_socd_key1: v = socd[pair].keycode[0]; break;
    case val_socd_key2: v = socd[pair].keycode[1]; break;

    default:
      return;
  }

  p_val[1] = (uint8_t)(v >> 8);
  p_val[2] = (uint8_t)(v & 0xFF);
}
