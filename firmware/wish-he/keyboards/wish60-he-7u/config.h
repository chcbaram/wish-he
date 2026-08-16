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
 * RGB 매트릭스 — LED 83개 (위쪽 65 + 언더글로우 18).
 *
 * 배치(g_led_config)는 rgb_config.c 에 자동 생성된다. 체인 순서·좌표는
 * layout-kle.json 에서 계산되므로 여기서 손댈 것이 없다.
 */
#define RGB_MATRIX_LED_COUNT        83

/*
 * ★ 밝기 상한. **이 값이 전류 리미터와 짝을 이룬다.**
 *
 *   상한 450mA 에서 83개를 흰색으로 켜도 리미터에 안 걸리는 최대가 18 이다
 *   (docs/14-led-limiter.md). 이보다 높이면 리미터가 걸리기 시작하고, 그러면
 *   **켜진 개수에 따라 이미 켜져 있던 LED 의 밝기까지 변한다** — 타이핑 내내
 *   전체가 출렁인다. 리미터는 안전망이지 밝기 조절 장치가 아니다.
 *
 *   확인은 `ws2812 info` 의 `limit hit` 로 한다. 0 이 아니면 이 값이 너무 높다.
 */
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS   18

/* 호스트가 자면 LED 를 끈다 (USBD_EVENT_SUSPEND -> rgb_matrix_set_suspend_state) */
#define RGB_MATRIX_SLEEP

/* 반응형 효과가 눌린 키를 알아야 한다 */
#define RGB_MATRIX_KEYPRESSES

/*
 * 효과 목록.
 *
 * ★ 전부 켜지 않는다. 46개가 다 들어오면 플래시만 먹고, VIA 에서 모드를 넘기다
 *   지친다. 성격이 겹치지 않는 것만 고른다.
 */
#define ENABLE_RGB_MATRIX_BREATHING              /* 전체가 숨쉰다 */
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN       /* 정지 그라디언트 — 가장 싸다 */
#define ENABLE_RGB_MATRIX_CYCLE_ALL              /* 판 전체가 같은 색으로 순환 */
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT       /* 무지개가 가로로 흐른다 */
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON /* 갈매기 무늬 */
#define ENABLE_RGB_MATRIX_PIXEL_FLOW             /* 점이 흩어져 흐른다 */

/*
 * ★ 여기부터가 HE 라서 만들 수 있는 것들 (rgb_matrix_kb.inc).
 *
 *   기계식은 눌림이 0/1 이라 반응형 효과가 전부 "눌린 순간부터 시간에 따라
 *   사라지는" 모양이 된다. HE 는 깊이가 연속값이라 **지금 얼마나 눌려 있는지**에
 *   직접 반응한다 — 손가락을 멈추면 빛도 멈춘다.
 */
#define RGB_MATRIX_CUSTOM_KB
#define ENABLE_RGB_MATRIX_HE_DEPTH          /* 깊이만큼 밝아진다 */
#define ENABLE_RGB_MATRIX_HE_DEPTH_HUE      /* 깊이만큼 색이 돈다 (밝기 일정) */
#define ENABLE_RGB_MATRIX_HE_DEPTH_RIPPLE   /* 깊이만큼 파문이 넓어진다 */

/* 커스텀 효과는 enum 이름에 CUSTOM_ 이 붙는다 */
#define RGB_MATRIX_DEFAULT_MODE     RGB_MATRIX_CUSTOM_HE_DEPTH

/*
 * 렌더 부하를 잘라 넣는 두 손잡이. 기본값을 그대로 쓰되 여기 적어 둔다 —
 * 스캔이 느려지면 제일 먼저 볼 자리다.
 *
 *   PROCESS_LIMIT  한 번에 처리할 LED 수 (기본 = 전체의 1/5)
 *   FLUSH_LIMIT    플러시 간격 ms (기본 16 = 약 60fps)
 */
#define RGB_MATRIX_LED_PROCESS_LIMIT    ((RGB_MATRIX_LED_COUNT + 4) / 5)
#define RGB_MATRIX_LED_FLUSH_LIMIT      16

/*
 * LAYOUT 매크로. keymap.c 가 쓴다.
 *
 * QMK 는 보통 <보드>.h 에 두지만 우리는 config.h 하나만 QMK 에 알려주면 되게
 * 여기서 끌어온다. 매크로라 여기서 펼쳐지지 않는다.
 */
#include "layout_qmk.h"
