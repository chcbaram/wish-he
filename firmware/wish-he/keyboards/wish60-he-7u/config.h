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
 * 8편에서 정한 값이다. 레이어 8개에 64키면 키맵 한 벌이 8 x 64 x 2 = 1KB 고,
 * 나머지가 매크로와 사용자 영역이다.
 */
#define TOTAL_EEPROM_BYTE_COUNT     16384
#define DYNAMIC_KEYMAP_LAYER_COUNT  8

/*
 * ★ 키맵도 프로파일마다 한 벌 둔다.
 *
 *   프로파일이 갈리는 이유가 손끝 감각만은 아니다 — 게임용에서는 키 배치도 함께
 *   달라진다. 그래서 프로파일에 배치를 포함한다.
 *
 *   값이 싸다. 키맵 한 벌이 1KB 라 네 벌이 4KB 고, 16KB 중 나머지 12KB 가 그대로
 *   매크로에 남는다.
 *
 * ★ 0번은 자리를 안 옮긴다.
 *
 *   첫 블록이 예전과 같은 주소라 지금 쓰던 키맵이 그대로 0번 프로파일이 된다.
 *   뒤로 밀리는 것은 매크로 영역뿐이다 — 매크로를 쓰고 있었다면 그건 지워진다.
 */
#define KEYMAP_PROFILE_COUNT        4

#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR                                     \
  (DYNAMIC_KEYMAP_EEPROM_ADDR +                                                \
   (KEYMAP_PROFILE_COUNT * DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS *          \
    MATRIX_COLS * 2))
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
 *   한동안 18 이었다. 83개를 **흰색으로** 켜도 리미터에 안 걸리는 최대가 그것이라
 *   (docs/14-led-limiter.md), 최악을 기준으로 잡은 값이다. 그런데 실제로 재 보니
 *   최악은 거의 오지 않는다 — 유채색은 RGB 세 채널 중 하나만 켜지기 때문이다.
 *
 *   밝기 17, 채도 255 에서 효과별 프레임 전류 (예산은 450 - 269 = 181mA) —
 *
 *     단색 57 · 숨쉬기 76 · 그라디언트 54 · 전체순환 84 · 가로흐름 83
 *     갈매기 81 · 픽셀흐름 40 · HE 깊이 0 · HE 깊이+색상 57 · HE 물결 0
 *
 *   **가장 바쁜 효과도 예산의 절반을 못 쓴다.** 그래서 3배로 올린다.
 *
 * ★ 넘치는 것은 리미터가 알아서 되돌린다. **리미터의 역할이 바뀌었다.**
 *
 *   리미터는 프레임마다 실제 전류를 재서 배율을 정하므로, 효과별·색상별 상한을
 *   자동으로 계산해 주는 것과 같다. 효과마다 표를 손으로 채울 이유가 없다 —
 *   어차피 그 표의 값은 색에 따라 세 배씩 달라져서 하나로 못 적는다.
 *
 *   전에는 "안전망이지 밝기 조절 장치가 아니다" 라고 썼다. 이제는 **조절 장치로
 *   쓴다.** 밝기를 54까지 올리면 전판 효과는 대부분 리미터에 닿는다 (실측: 어느
 *   효과든 449mA 에서 멎는다 = 예산을 정확히 다 쓴다). 그게 목적이다.
 *
 *   출렁임 걱정은 다시 보면 겹치지 않는다 — **리미터에 걸리는 것은 켜진 개수가
 *   일정한 전판 효과**라 배율이 고정이고, **출렁일 수 있는 반응형 효과(HE 깊이·
 *   물결)는 켜진 개수가 적어 269mA 에 머문다** (= LED 몫 0, 안 걸린다).
 *
 *   경계에 걸치는 것은 HE 깊이+색상 하나다. 바탕이 전판이라 이미 리미터에 닿아
 *   있어서, 키를 누르면 그만큼 바탕이 어두워진다. 눈에 거슬리면 그 효과에만 낮은
 *   상한을 주거나 배율을 서서히 움직이게 한다.
 *
 *   ★ 최대 밝기에서는 보드가 450mA 를 다 쓴다. 예전(327mA)보다 여유가 없으니
 *     USB 쪽이 약하면 밝기를 낮춰서 쓴다 — 이제 슬라이더가 전 구간에서 의미가 있다.
 *
 *   확인은 `ws2812 info` 의 `frame current` 로 한다.
 */
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS   54

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
