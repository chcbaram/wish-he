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
#include "dynamic_keymap.h"

/*
 * ── 키맵도 프로파일마다 한 벌 ────────────────────────────────────────────
 *
 * dynamic_keymap 의 주소 계산이 이 오프셋을 더한다. 키맵을 읽고 쓰는 길이 거기
 * 하나로 모이므로 VIA 프로토콜도 상위 코드도 손댈 것이 없다 — 프로파일을 바꾸면
 * 같은 (레이어, 행, 열) 이 다른 블록을 가리킬 뿐이다.
 *
 * ★ 초기화 때는 잠시 다른 프로파일을 가리켜야 한다.
 *
 *   dynamic_keymap_reset() 은 "지금 프로파일" 한 벌만 채운다. 그대로 두면 2~4번
 *   프로파일의 키맵이 전부 KC_NO 라, 옮기는 순간 키보드가 죽는다. 초기화에서는
 *   네 벌을 돌며 다 채운다.
 */
#define KEYMAP_BLOCK_SIZE                                                      \
  (DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS * MATRIX_COLS * 2)

static uint8_t km_prof_override = 0xFF;   /* 0xFF = 지금 프로파일을 따른다 */

static void keymapFillEmptyProfiles(void);
static volatile bool rgb_reload_req;

void via_init_kb(void)
{
  keymapFillEmptyProfiles();
}

uint32_t keymapProfileOffset(void)
{
  uint8_t p = (km_prof_override != 0xFF) ? km_prof_override : keysProfGet();

  if (p >= KEYMAP_PROFILE_COUNT) p = 0;
  return (uint32_t)p * KEYMAP_BLOCK_SIZE;
}

/* 네 벌을 모두 기본 키맵으로 채운다 — via.c 의 초기화가 부른다 */
void eeconfig_init_keymap_profiles_kb(void)
{
  for (uint8_t p = 0; p < KEYMAP_PROFILE_COUNT; p++)
  {
    km_prof_override = p;
    dynamic_keymap_reset();
  }
  km_prof_override = 0xFF;
}

/*
 * ★ 이미 쓰던 보드를 위한 뒤처리.
 *
 *   EEPROM 이 이미 유효하면 위 초기화는 **안 돈다**. 그러면 0번 프로파일만 키맵을
 *   갖고 2~4번은 통째로 0(KC_NO) 이라, 옮기는 순간 아무 키도 안 먹는 키보드가 된다.
 *   기능을 넣으면서 보드를 죽이는 셈이다.
 *
 *   그렇다고 VIA 버전을 올려 전체를 초기화하면 사용자가 짜 둔 키맵이 날아간다.
 *   비어 있는 블록만 골라 기본 키맵으로 채운다 — 쓰던 것은 그대로 두고 빈 칸만
 *   메운다.
 *
 *   판정은 "전부 0" 이다. 레이어 0의 첫 줄이 전부 KC_NO 인 키맵은 정상적으로는
 *   나올 수 없다 — 그 줄에 키가 하나도 없다는 뜻이기 때문이다.
 */
static void keymapFillEmptyProfiles(void)
{
  for (uint8_t p = 1; p < KEYMAP_PROFILE_COUNT; p++)
  {
    bool empty = true;

    km_prof_override = p;
    for (uint8_t col = 0; col < MATRIX_COLS && empty; col++)
    {
      if (dynamic_keymap_get_keycode(0, 0, col) != KC_NO) empty = false;
    }

    if (empty)
    {
      logPrintf("[  ] 프로파일 %d 키맵이 비어 있다 — 기본값으로 채운다\n", p + 1);
      dynamic_keymap_reset();
    }
  }
  km_prof_override = 0xFF;
}


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

  /*
   * 일반형(GENERIC)을 골랐을 때의 전 행정.
   *
   * ★ 이 값도 여기 있어야 한다.
   *
   *   키별 명령(0xC5)으로만 오갔더니 **쓰는 곳과 읽는 곳이 갈렸다.** 화면은 키를
   *   안 고르면 프로파일 전역을 읽는데 거기에 이 값이 없어, 다시 연결하면 옛 값이
   *   나온다. RT 플래그에서 똑같이 당한 적이 있다.
   */
  val_he_gen_travel = 9,   /* 일반형 전 행정  0.01mm */
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
    case val_he_gen_travel: keysSetGenTravelUm(v);        break;

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
    case val_he_gen_travel: v = keysGetGenTravelUm(); break;

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
     * ★ HE 설정은 여기서 저장해야 한다.
     *
     *   QMK 쪽 값(hold_okp · nkro)은 set 할 때 이미 EEPROM 에 넣었고 지연 플러시가
     *   모아 쓴다. 그래서 한동안 이 명령을 아무것도 안 하게 두었는데, HE 설정은
     *   QMK EEPROM 이 아니라 keys.c 의 자체 플래시 레코드에 산다. 그 결과 웹에서
     *   바꾼 입력지점·RT 값이 전원을 끄면 전부 사라졌다.
     */
    case id_custom_save:
      keysSave();
      return;

    default:
      break;
  }

  *p_cmd = id_unhandled;
}


/*
 * ── 프로파일 전환 키코드 ────────────────────────────────────────────────
 *
 * ★ 키보드만으로 바꿀 수 있어야 한다.
 *
 *   게임 중에 프로파일을 바꾸려고 브라우저를 띄울 수는 없다. VIA 의 커스텀
 *   키코드(QK_KB_0..)로 내면 사용자가 키 선택기에서 원하는 자리에 붙인다.
 *   layout-via.json 의 customKeycodes 순서와 **여기 순서가 같아야 한다.**
 *
 * ★ 뗄 때가 아니라 **누를 때** 바꾼다.
 *
 *   바뀌는 순간 그 키가 속한 키맵도 함께 바뀐다. 뗄 때 처리하면 이미 새 프로파일의
 *   키맵을 보고 있어 "뗀 키" 가 무엇인지 어긋난다.
 *
 * ★ 플래시 쓰기는 미룬다.
 *
 *   전환은 메모리 한 줄이라 싸지만 남기기는 2~3ms 다. 키를 누른 그 자리에서 쓰면
 *   그동안 스캔이 멎는다 — 게임 중에 쓰라고 만든 키에서 그러면 안 된다.
 *   메인 루프가 조용해진 뒤에 쓴다 (keysCfgTouch 와 같은 방식).
 */
enum {
  KC_PROF_1 = QK_KB_0,
  KC_PROF_2,
  KC_PROF_3,
  KC_PROF_4,
  KC_PROF_NEXT,
  KC_PROF_PREV,
};

bool process_record_kb(uint16_t keycode, keyrecord_t *record)
{
  if (record->event.pressed)
  {
    switch (keycode)
    {
      case KC_PROF_1:
      case KC_PROF_2:
      case KC_PROF_3:
      case KC_PROF_4:
        if (keysProfSelect((uint8_t)(keycode - KC_PROF_1))) keysProfTouch();
        return false;

      case KC_PROF_NEXT:
        if (keysProfSelect((uint8_t)((keysProfGet() + 1) % keysProfCount())))
          keysProfTouch();
        return false;

      /*
       * 뒤로. 빼기 대신 (개수 - 1) 을 더한다 — uint8_t 라 0 에서 빼면 뒤집힌다.
       */
      case KC_PROF_PREV:
        if (keysProfSelect((uint8_t)((keysProfGet() + keysProfCount() - 1) %
                                     keysProfCount())))
          keysProfTouch();
        return false;

      default:
        break;
    }
  }
  return process_record_user(keycode, record);
}


/*
 * ── 조명 설정도 프로파일마다 ────────────────────────────────────────────
 *
 * ★ 키맵과 다루는 법이 다르다.
 *
 *   키맵은 쓸 때마다 EEPROM 을 직접 읽으므로 주소만 바꾸면 그 순간부터 새것이다.
 *   조명 설정은 부팅 때 RAM(rgb_matrix_config)으로 읽어 두고 거기서 쓴다. 주소를
 *   바꿔 봐야 RAM 에 든 옛 값이 계속 나온다.
 *
 *   그래서 옮겨 담는다 — 바뀌기 전에 지금 값을 그 프로파일 칸에 내려놓고, 바뀐 뒤에
 *   새 칸에서 올려 RAM 을 다시 채운다.
 *
 * ★ 칸은 사용자 데이터 영역에 둔다.
 *
 *   EECONFIG_USER_DATABLOCK 512B 가 우리 몫으로 비어 있다. 여덟 바이트짜리 네 칸이면
 *   32B 라, EEPROM 을 넓히거나 주소를 옮길 것이 없다.
 */
#define PROF_RGB_SIZE   ((uint16_t)sizeof(rgb_config_t))
#define PROF_RGB_SLOT(p) ((void *)(EECONFIG_USER_DATABLOCK + (p) * PROF_RGB_SIZE))

_Static_assert(KEYMAP_PROFILE_COUNT * sizeof(rgb_config_t) <= EECONFIG_USER_DATA_SIZE,
               "조명 프로파일 칸이 사용자 데이터 영역을 넘는다");

/*
 * 지금 조명 설정을 그 프로파일 칸에 내려놓는다.
 *
 * RAM 에 든 것을 먼저 표준 자리에 쓴 다음(eeconfig_update_rgb_matrix) 그 바이트를
 * 칸으로 옮긴다. RAM 을 바로 칸에 쓰면 표준 자리가 낡은 채로 남아, 다음 부팅 때
 * 옛 값으로 시작한다.
 */
static void profRgbStore(uint8_t p)
{
  uint8_t buf[sizeof(rgb_config_t)];

  if (p >= KEYMAP_PROFILE_COUNT) return;

  eeconfig_update_rgb_matrix();
  eeprom_read_block(buf, (const void *)EECONFIG_RGB_MATRIX, sizeof(buf));
  eeprom_update_block(buf, PROF_RGB_SLOT(p), sizeof(buf));
}

/*
 * 그 프로파일 칸의 조명 설정을 꺼내 쓴다.
 *
 * 칸이 비어 있으면(한 번도 저장한 적 없음) 지금 것을 그대로 둔다 — 처음 옮겨 간
 * 프로파일이 캄캄해지는 것보다 쓰던 조명이 따라오는 편이 낫다.
 */
static void profRgbApply(uint8_t p)
{
  uint8_t buf[sizeof(rgb_config_t)];
  bool    blank = true;

  if (p >= KEYMAP_PROFILE_COUNT) return;

  eeprom_read_block(buf, PROF_RGB_SLOT(p), sizeof(buf));
  for (uint32_t i = 0; i < sizeof(buf); i++)
  {
    if (buf[i] != 0x00 && buf[i] != 0xFF) { blank = false; break; }
  }
  if (blank)
  {
    profRgbStore(p);      /* 지금 것을 그 칸의 첫 값으로 삼는다 */
    return;
  }

  eeprom_update_block(buf, (void *)EECONFIG_RGB_MATRIX, sizeof(buf));
  rgb_reload_req = true;      /* 켜는 것은 메인 루프에서 */
}

/*
 * keys.c 가 프로파일을 바꿀 때 부른다. 0xFF = 바뀌기 직전.
 *
 * ★ **ISR 안에서 불릴 수 있다.**
 *
 *   HID 명령으로 바꾸면 이 길이 USB 인터럽트 안이다. 그래서 여기서는 메모리만
 *   만진다 — 칸에 내려놓고 올리는 것은 EEPROM RAM 버퍼 복사라 싸다.
 *
 *   조명을 실제로 다시 켜는 일(rgb_matrix_reload_from_eeprom)은 표시만 하고 메인
 *   루프로 넘긴다. 그 안에서 로그를 찍고 효과 상태를 갈아 끼우는데, ISR 에서
 *   그러다 USB 가 멈췄다 — 도구가 "읽는 중 2 / 4" 에서 굳은 것이 이것이다.
 *
 *   전환 자체(키맵·판정)는 이미 즉시 반영된다. 미루는 것은 조명을 켜는 마지막
 *   한 걸음뿐이라 사람 눈에는 차이가 없다.
 */
static volatile bool rgb_reload_req = false;

void keysProfChanged_kb(uint8_t idx)
{
  if (idx == 0xFF) profRgbStore(keysProfGet());
  else             profRgbApply(idx);
}

/* 메인 루프에서 부른다 (keys.c 의 keysCfgUpdate 가 이어서 부른다) */
void keysProfUpdate_kb(void)
{
  if (rgb_reload_req == false) return;

  rgb_reload_req = false;
  rgb_matrix_reload_from_eeprom();
}
