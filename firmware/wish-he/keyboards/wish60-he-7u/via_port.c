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
 *   우리는 matrix.c 가 debounce() 를 아예 부르지 않는다.
 *   HE 는 접점이 없어 바운스가 없고 잡음은 keys.c 가 지연 0 으로 거른다. 그 메뉴를
 *   두면 아무것도 하지 않는 스위치가 된다.
 */

#include "quantum.h"
#include "via.h"
#include "eeprom.h"
#include "keys.h"
#include "log.h"


/* 채널 ID. 다른 보드와 겹쳐도 상관없지만, 도구를 공유하려고 같은 값을 쓴다. */
/*
 * 부트로더 진입과 EEPROM 초기화는 여기 두지 않는다.
 *
 * 표준 위젯만으로는 토글을 연달아 켜게 하는 식이 되는데 조잡하다. 우리는 라이브
 * 트래킹 때문에 어차피 웹앱을 포크하므로 그 탭에서 제대로 만든다 — 확인 대화상자
 * 하나면 될 일이다.
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
 * HE 설정. keys_cfg_t 가 이 값들을 들고 있다.
 *
 * 전부 0.01mm 단위고 VIA 규약대로 빅엔디안 16비트다. 플래그만 8비트다.
 *
 * ★ 여기서 쓰면 64키 전부에 적용된다.
 *
 *   판정은 키별 값을 보지만, 이 채널은 값 ID 두 바이트뿐이라 키 인덱스를 실을 자리가
 *   없다. 그래서 전역(= 모두 선택)만 다룬다. 키를 골라 설정하는 것은 포크한 웹앱의
 *   HE 탭에서 별도 명령으로 한다.
 */
enum via_he_value
{
  val_he_press      = 1,   /* 입력지점      0.01mm */
  val_he_release    = 2,   /* 해제지점      0.01mm */
  val_he_switch     = 3,   /* 스위치 종류   8비트 */
  val_he_rt_press   = 4,   /* RT 재입력     0.01mm */
  val_he_rt_release = 5,   /* RT 입력 해제  0.01mm */
  val_he_bottom     = 6,   /* 바닥 보호     0.01mm */
  val_he_dead       = 7,   /* 데드존        0.01mm */
  val_he_rt_flags   = 8,   /* RT 켬 / 바닥 보호 / 연속 RT  8비트 */
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
  keymap_config_t clean;

  hold_okp = eeprom_read_byte((const uint8_t *)EE_USER_HOLD_OKP);
  if (hold_okp > 1) hold_okp = 1;      /* 지운 적 없는 EEPROM 은 0xFF 다 */

  /*
   * 이미 망가진 EEPROM 을 되살린다.
   *
   * 위 버그가 keymap_config 에 쓰레기를 써 놓은 보드가 있다. 우리는 매직 스왑을
   * 어디에도 노출하지 않으므로 — 메뉴에도, 키맵에도 없다 — 켜져 있다면 그 쓰레기다.
   * nkro 만 남기고 지운다. 사용자 키맵은 다른 영역이라 건드리지 않는다.
   *
   * ★ 매직 키코드를 실제로 노출하게 되면 이 청소를 걷어내야 한다.
   */
  clean.raw  = 0;
  clean.nkro = keymap_config.nkro;

  if (clean.raw != keymap_config.raw)
  {
    logPrintf("[  ] keymap_config 정리 0x%04X -> 0x%04X\n",
              (unsigned)keymap_config.raw, (unsigned)clean.raw);
    keymap_config.raw = clean.raw;
    eeconfig_update_keymap(keymap_config.raw);
  }
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

    /*
     * ★ .raw 를 넘긴다. 포인터가 아니다.
     *
     *   eeconfig_update_keymap(uint16_t) 는 **값**을 받는데 처음에 &keymap_config 를
     *   넘겼다. 포인터가 16비트로 잘려 keymap_config 전체로 저장됐고, 하필 매직 스왑
     *   비트에 걸렸다 — NKRO 를 한 번 토글한 뒤로
     *
     *     swap_backslash_backspace  BSPC <-> BSLS
     *     swap_lalt_lgui            LALT <-> LGUI
     *     swap_lctl_lgui            LCTL  -> LGUI
     *
     *   가 켜져서, 매트릭스도 키맵도 맞는데 **나가는 키코드만** 달랐다. 찾는 데
     *   오래 걸린 이유가 이거다 — 눈에 보이는 곳은 전부 정상이었다.
     */
    eeconfig_update_keymap(keymap_config.raw);
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
    case val_he_press:      keysSetPressUm(v);            break;
    case val_he_release:    keysSetReleaseUm(v);          break;
    case val_he_rt_press:   keysSetRtPressUm(v);          break;
    case val_he_rt_release: keysSetRtReleaseUm(v);        break;
    case val_he_bottom:     keysSetBottomUm(v);           break;
    case val_he_dead:       keysSetDeadUm(v);             break;

    /* 8비트짜리는 첫 바이트만 본다 */
    case val_he_switch:     keysSetSwitchType(p_val[1]);  break;
    case val_he_rt_flags:   keysSetRtFlags(p_val[1]);     break;
    default: break;
  }
}

static void viaHeGet(uint8_t *p_val)
{
  uint16_t v = 0;

  switch (p_val[0])
  {
    case val_he_press:      v = keysGetPressUm();     break;
    case val_he_release:    v = keysGetReleaseUm();   break;
    case val_he_rt_press:   v = keysGetRtPressUm();   break;
    case val_he_rt_release: v = keysGetRtReleaseUm(); break;
    case val_he_bottom:     v = keysGetBottomUm();    break;
    case val_he_dead:       v = keysGetDeadUm();      break;

    case val_he_switch:     p_val[1] = keysGetSwitchType(); return;
    case val_he_rt_flags:   p_val[1] = keysGetRtFlags();    return;
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
