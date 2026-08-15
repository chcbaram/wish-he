#pragma once

/*
 * QMK 보드 설정 — wish60-he-7u
 *
 * 매트릭스는 하드웨어 그대로다. row = MUX 스텝, col = ADC 채널.
 * 배치·키맵은 전부 layout-kle.json 에서 생성된다 (tools/gen_keymap.py).
 */

#define MATRIX_ROWS                 8
#define MATRIX_COLS                 8

/*
 * ★ 디바운스를 쓰지 않는다.
 *
 *   HE 는 접점이 없어 바운스가 없다. port/matrix.c 가 debounce() 를 아예 부르지
 *   않으므로 이 값은 QMK 헤더가 참조할 때를 위한 자리표시자다.
 *   잡음은 keys.c 가 데드밴드 필터와 히스테리시스로 이미 처리한다 — 둘 다 지연이 0.
 */
#define DEBOUNCE                    0

/*
 * EEPROM — 플래시 0x0C4000 에 16KB.
 *
 * 8편에서 정한 값이다. 레이어 8개에 63키면 키맵만 8 x 64 x 2 = 8KB 고, 나머지가
 * 매크로와 사용자 영역이다.
 */
#define TOTAL_EEPROM_BYTE_COUNT     16384
#define DYNAMIC_KEYMAP_LAYER_COUNT  8
#define EECONFIG_USER_DATA_SIZE     512

#define VIA_FIRMWARE_VERSION        1

/*
 * LAYOUT 매크로. keymap.c 가 쓴다.
 *
 * QMK 는 보통 <보드>.h 에 두지만 우리는 config.h 하나만 QMK 에 알려주면 되게
 * 여기서 끌어온다. 매크로라 여기서 펼쳐지지 않는다.
 */
#include "layout_qmk.h"
