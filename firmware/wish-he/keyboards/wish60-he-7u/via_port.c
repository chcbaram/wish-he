/*
 * via_port.c  —  VIA 커스텀 메뉴 처리
 *
 * VIA 는 정의 JSON 의 menus 를 보고 UI 를 스스로 그린다. 펌웨어는 채널 ID 별로
 * 값을 읽고 쓰기만 하면 된다. 그래서 **설정 화면은 웹앱을 포크하지 않아도 된다** —
 * 포크가 꼭 필요한 것은 라이브 트래킹처럼 VIA 에 없는 위젯뿐이다.
 *
 *   호스트 -> 장치   [0] id_custom_get/set_value  [1] 채널  [2] 값 ID  [3..] 값
 *
 * ★ 디바운스 메뉴는 두지 않는다.
 *
 *   참고한 보드에는 있지만 우리는 matrix.c 가 debounce() 를 아예 부르지 않는다.
 *   HE 는 접점이 없어 바운스가 없고 잡음은 keys.c 가 지연 0 으로 거른다. 그 메뉴를
 *   두면 아무것도 하지 않는 스위치가 된다.
 */

#include "quantum.h"
#include "via.h"
#include "eeprom.h"
#include "keys.h"


/* 채널 ID. 다른 보드와 겹쳐도 상관없지만, 도구를 공유하려고 같은 값을 쓴다. */
/*
 * 부트로더 진입과 EEPROM 초기화는 여기 두지 않는다.
 *
 * 참고한 보드는 토글 세 개를 연달아 켜야 지워지는 식으로 만들었는데, 표준 위젯만
 * 쓰다 보니 나온 모양이다. 우리는 라이브 트래킹 때문에 어차피 웹앱을 포크하므로
 * 그 탭에서 제대로 만든다 — 확인 대화상자 하나면 될 일이다.
 */
enum via_channel
{
  id_ch_qmk  = 14,
  id_ch_nkro = 15,
  id_ch_he   = 16,    /* 우리 것 */
};

/* 값 ID — QMK 헤더의 id_* 와 겹치지 않게 접두어를 다르게 둔다 */
enum via_qmk_value
{
  val_hold_okp = 1,
};

enum via_nkro_value
{
  val_nkro_enable = 1,
};

/*
 * HE 설정. 8편의 keys_cfg_t 가 이미 이 값들을 들고 있다.
 *
 * 래피드 트리거와 데드존은 13편에서 펌웨어 로직이 생긴 뒤에 연다 — 지금 노출하면
 * 돌려도 아무 일이 없는 손잡이가 된다.
 */
enum via_he_value
{
  val_he_press   = 1,   /* 입력지점 0.01mm */
  val_he_release = 2,   /* 해제지점 0.01mm */
  val_he_switch     = 3,   /* 스위치 종류 */
};


/* 사용자 영역 배치 (EECONFIG_USER_DATABLOCK 기준 오프셋) */
#define EE_USER_HOLD_OKP    ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + 0))   /* 1B */

static uint8_t hold_okp = 1;




/*---------------------------------------------------------------------------
 *  Hold On Other Key Press — 런타임 토글
 *
 *  탭홀드 키를 누른 채 다른 키를 치면 곧바로 홀드로 확정할지 정한다. 게임에서는
 *  켜는 쪽이, 타이핑에서는 끄는 쪽이 낫다고들 해서 사용자가 고르게 한다.
 *---------------------------------------------------------------------------*/
bool get_hold_on_other_key_press(uint16_t keycode, keyrecord_t *record)
{
  (void)keycode;
  (void)record;
  return (hold_okp != 0);
}

void viaPortInit(void)
{
  hold_okp = eeprom_read_byte((const uint8_t *)EE_USER_HOLD_OKP);
  if (hold_okp > 1) hold_okp = 1;      /* 지운 적 없는 EEPROM 은 0xFF 다 */
}




/*---------------------------------------------------------------------------
 *  채널별 처리
 *---------------------------------------------------------------------------*/

static void viaQmkSet(uint8_t *p_val)
{
  if (p_val[0] == val_hold_okp)
  {
    hold_okp = p_val[1] ? 1 : 0;
    eeprom_update_byte((uint8_t *)EE_USER_HOLD_OKP, hold_okp);
  }
}

static void viaQmkGet(uint8_t *p_val)
{
  if (p_val[0] == val_hold_okp) p_val[1] = hold_okp;
}

static void viaNkroSet(uint8_t *p_val)
{
  if (p_val[0] == val_nkro_enable)
  {
    keymap_config.nkro = p_val[1] ? 1 : 0;
    eeconfig_update_keymap(&keymap_config);
    clear_keyboard();          /* 프로토콜이 바뀌므로 눌린 키를 다 떼고 간다 */
  }
}

static void viaNkroGet(uint8_t *p_val)
{
  if (p_val[0] == val_nkro_enable) p_val[1] = keymap_config.nkro;
}

static void viaHeSet(uint8_t *p_val)
{
  uint16_t v = (uint16_t)((p_val[1] << 8) | p_val[2]);   /* VIA 는 빅엔디안 */

  switch (p_val[0])
  {
    case val_he_press:   keysSetPressUm(v);            break;
    case val_he_release: keysSetReleaseUm(v);          break;
    case val_he_switch:     keysSetSwitchType(p_val[1]);  break;
    default: break;
  }
}

static void viaHeGet(uint8_t *p_val)
{
  uint16_t v = 0;

  switch (p_val[0])
  {
    case val_he_press:   v = keysGetPressUm();   break;
    case val_he_release: v = keysGetReleaseUm(); break;
    case val_he_switch:     p_val[1] = keysGetSwitchType(); return;
    default: return;
  }

  p_val[1] = (uint8_t)(v >> 8);
  p_val[2] = (uint8_t)(v & 0xFF);
}




void via_custom_value_command_kb(uint8_t *data, uint8_t length)
{
  (void)length;

  /* data = [ command_id, channel_id, value_id, value_data... ] */
  uint8_t *p_cmd = &data[0];
  uint8_t  ch    = data[1];
  uint8_t *p_val = &data[2];

  switch (*p_cmd)
  {
    case id_custom_set_value:
      switch (ch)
      {
        case id_ch_qmk:    viaQmkSet(p_val);    return;
        case id_ch_nkro:   viaNkroSet(p_val);   return;
        case id_ch_he:     viaHeSet(p_val);     return;
        default: break;
      }
      break;

    case id_custom_get_value:
      switch (ch)
      {
        case id_ch_qmk:     viaQmkGet(p_val);     return;
        case id_ch_nkro:    viaNkroGet(p_val);    return;
        case id_ch_he:      viaHeGet(p_val);      return;
        default: break;
      }
      break;

    /*
     * 저장은 따로 하지 않는다. set 할 때 이미 EEPROM 에 반영했고, 플래시 기록은
     * 지연 플러시가 알아서 모아 쓴다 (port/platforms/eeprom.c).
     */
    case id_custom_save:
      return;

    default:
      break;
  }

  *p_cmd = id_unhandled;
}
