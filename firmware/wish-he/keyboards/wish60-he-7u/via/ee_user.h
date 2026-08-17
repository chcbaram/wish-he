#ifndef EE_USER_H_
#define EE_USER_H_

/*
 * 사용자 EEPROM 영역(512B)의 배치 — **여기서만 정한다.**
 *
 * QMK 는 EECONFIG_USER_DATABLOCK 부터 512바이트를 우리 몫으로 비워 둔다. 그 안을
 * 여러 모듈(조명 프로파일 · 탭홀드 · SOCD · 연타)이 나눠 쓰므로, 자리를 각자 손으로
 * 더하면 반드시 겹친다. 실제로 겹쳐서 0번 프로파일 조명이 탭홀드 값과 같은 바이트에
 * 앉은 적이 있다 — 조명을 켜면 다음 부팅에 탭홀드가 뒤집혔다.
 *
 * 그래서 구조체 하나로 잡고 offsetof 로 주소를 뽑는다. 겹칠 수가 없다.
 *
 * ★ **새 항목은 뒤에 더한다.** 앞에 끼우면 이미 쓰고 있는 보드의 값이 한 칸씩
 *   밀린다. 조명 칸이 맨 앞인 것도 그래서다.
 *
 * ★ **자리가 바뀌면 USER_MAGIC 을 올린다.** 안 올리면 옛 보드가 엉뚱한 바이트를
 *   유효한 값으로 믿는다 — 탭텀이 0 이면 탭이 영영 안 나오고, SOCD 키코드가 쓰레기면
 *   엉뚱한 키가 지워진다. 올리면 그 보드는 한 번 기본값으로 돌아간다.
 *
 *     0x5A  hold_okp 하나, 한 벌
 *     0x5B  + tapping_term
 *     0x5C  프로파일마다 한 벌씩
 *     0x5D  + SOCD, 눌러 두면 연타
 */

#include "quantum.h"
#include "socd.h"
#include "ghost.h"
#include "taphold.h"

#define USER_MAGIC    0x5D

typedef struct PACKED
{
  rgb_config_t rgb[KEYMAP_PROFILE_COUNT];   /* 프로파일별 조명 8B x 4 */
  uint8_t      user_magic;                  /* 아래 것들이 우리가 쓴 것인지 가린다 */
  uint8_t      hold_okp[KEYMAP_PROFILE_COUNT];
  uint16_t     tapping_term[KEYMAP_PROFILE_COUNT];
  socd_cfg_t   socd[KEYMAP_PROFILE_COUNT][SOCD_PAIR_CNT];
  ghost_cfg_t  ghost[KEYMAP_PROFILE_COUNT];
} ee_user_t;

#define EE_USER(field)                                                         \
  ((void *)((uint32_t)EECONFIG_USER_DATABLOCK + offsetof(ee_user_t, field)))

#define EE_USER_MAGIC           EE_USER(user_magic)
#define EE_USER_RGB(p)          EE_USER(rgb[p])
#define EE_USER_HOLD_OKP(p)     EE_USER(hold_okp[p])
#define EE_USER_TAPPING_TERM(p) EE_USER(tapping_term[p])
#define EE_USER_SOCD(p, i)      EE_USER(socd[p][i])
#define EE_USER_GHOST(p)        EE_USER(ghost[p])

_Static_assert(sizeof(ee_user_t) <= EECONFIG_USER_DATA_SIZE,
               "사용자 영역 항목이 512B 를 넘는다");

/* 조명 칸이 움직이면 이미 쓰는 보드의 프로파일별 조명이 한 칸씩 어긋난다 */
_Static_assert(offsetof(ee_user_t, rgb) == 0,
               "조명 칸은 맨 앞을 지켜야 한다 — 새 항목은 뒤에 더할 것");

#endif
