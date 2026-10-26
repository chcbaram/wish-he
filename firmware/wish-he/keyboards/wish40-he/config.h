#pragma once

/*
 * QMK 보드 설정 — wish40-he (42키 40%, HPM5361 LQFP64)
 *
 * 매트릭스는 하드웨어 그대로다. row = 스캔 루프 인덱스, col = MUX 번호 (= MAG 번호).
 * 배치 · 키맵은 layout-kle.json 과 keymap-default.json 에서 생성된다 (tools/gen_keymap.py).
 * 브링업 기록은 docs/wish40-he.md.
 */

/*
 * 8 스텝 x 6 열. MUX 가 6개라 ADC 채널도 6개다 (ADC 하나에 3개).
 *
 * ★ HW_KEYS_CH_MAX 를 여기서 준다. 이 파일이 모든 C 파일 맨 앞에 들어가므로(-include)
 *   hw_def.h 의 기본값(8)보다 먼저 정해진다. MATRIX_COLS 와 같아야 한다.
 */
#define MATRIX_ROWS                 8
#define MATRIX_COLS                 6
#define HW_KEYS_CH_MAX              6

/* HE 는 접점이 없어 바운스가 없다 — port/matrix.c 가 debounce() 를 안 부른다 */
#define DEBOUNCE                    0

/*
 * EEPROM — 플래시 0x0C4000 에 16KB (wish60-he 와 같은 자리).
 * 키맵 한 벌이 8 레이어 x 48칸 x 2 = 768B, 프로파일 네 벌이 3KB 다.
 */
#define TOTAL_EEPROM_BYTE_COUNT     16384
#define DYNAMIC_KEYMAP_LAYER_COUNT  8
#define KEYMAP_PROFILE_COUNT        4

#define DYNAMIC_KEYMAP_ENCODER_EEPROM_ADDR                                     \
  (DYNAMIC_KEYMAP_EEPROM_ADDR +                                                \
   (KEYMAP_PROFILE_COUNT * DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_ROWS *          \
    MATRIX_COLS * 2))
#define EECONFIG_USER_DATA_SIZE     512

#define VIA_FIRMWARE_VERSION        1

/*
 * RGB 매트릭스 — ★ 이 보드에는 LED 가 없다.
 *
 *   그래도 켜 둔다. 공유 코드(qmk.c · keys.c · hid_if.c · via_port.c)를 RGB 유무로
 *   가르지 않으려는 것이다. 데이터는 PA29 로 나가는데 회로도에서 PA26~PA29 는 연결이
 *   없다 — 받는 곳이 없으니 해가 없다. 비용은 wish61-he 에서 잰 RGB 전부하/소등 차이
 *   1.1% (스캔) 수준이다.
 *
 *   남는 흠은 웹앱에 듣지 않는 조명 메뉴가 보이는 것이다. 나중에 RGB 를 빼는 옵션을
 *   넣으면 같이 걷는다.
 *
 * 개수는 키마다 하나 = 42. 배치는 rgb_config.c 에 자동 생성된다 (leds.json).
 */
#define RGB_MATRIX_LED_COUNT        42
#define RGB_MATRIX_MAXIMUM_BRIGHTNESS   54
#define RGB_MATRIX_SLEEP
#define RGB_MATRIX_KEYPRESSES

#define ENABLE_RGB_MATRIX_BREATHING
#define ENABLE_RGB_MATRIX_GRADIENT_UP_DOWN
#define ENABLE_RGB_MATRIX_CYCLE_ALL
#define ENABLE_RGB_MATRIX_CYCLE_LEFT_RIGHT
#define ENABLE_RGB_MATRIX_RAINBOW_MOVING_CHEVRON
#define ENABLE_RGB_MATRIX_PIXEL_FLOW

#define RGB_MATRIX_CUSTOM_KB
#define ENABLE_RGB_MATRIX_HE_DEPTH
#define ENABLE_RGB_MATRIX_HE_DEPTH_HUE
#define ENABLE_RGB_MATRIX_HE_DEPTH_RIPPLE

#define RGB_MATRIX_DEFAULT_MODE     RGB_MATRIX_CUSTOM_HE_DEPTH

#define RGB_MATRIX_LED_PROCESS_LIMIT    ((RGB_MATRIX_LED_COUNT + 4) / 5)
#define RGB_MATRIX_LED_FLUSH_LIMIT      16

/* ------------------------------------------------------------------ *
 *  하드웨어 — 핀 · ADC · RGB  (회로도 SCH-WISH40-HE 2026-10-26)
 * ------------------------------------------------------------------ */

/*
 * 아날로그 입력 — PB08~PB13 = MAG0~MAG5 (MUX 출력, 100Ω 직렬).
 *
 * LQFP64 에는 PB02~PB07 이 없다. 쓰는 여섯만 잡는다.
 */
#define HW_KEYS_ANALOG_PAD_FIRST    IOC_PAD_PB08
#define HW_KEYS_ANALOG_PAD_CNT      6

/*
 * ADC 시퀀스 채널 — 열 번호 = MAG 번호가 되게 늘어놓는다.
 *
 *   열    0     1     2     3     4     5
 *   MAG   0     1     2     3     4     5
 *   패드  PB08  PB09  PB10  PB11  PB12  PB13
 *   채널  11    1     2     3     4     5
 *
 * ★ 채널 <-> 패드는 **쟀다** (docs/wish40-he.md). 빈 MUX 입력을 GND 에 묶은 자리가
 *   MAG 마다 달라서 키를 안 누르고 갈렸다. MAG2 · 3 · 4 는 GND 무늬가 같아 거기서는
 *   못 갈랐는데, `keys learn` 으로 42키를 다 눌러 레이아웃과 한 칸도 안 어긋나는 것을
 *   봤다 (2026-10-27) — 이 표가 맞다.
 */
#define HW_KEYS_ADC0_SEQ_CH         { 11, 1, 2 }   /* PB08 PB09 PB10 */
#define HW_KEYS_ADC1_SEQ_CH         {  3, 4, 5 }   /* PB11 PB12 PB13 */

/* MUX 주소 — PY00~PY02, MUX 6개 공통. PY03 은 연결이 없다 */
#define HW_KEYS_MUX_PORT            GPIO_DO_GPIOY
#define HW_KEYS_MUX_PAD_FIRST       IOC_PAD_PY00
#define HW_KEYS_MUX_PIN_CNT         3

/*
 * MUX 세틀링 nop 수 — wish60-he 값 그대로. **안 쟀다.**
 *
 * MUX 는 같은 부품(C_D 11pF)이지만 센서 출력 커패시터가 820pF 라 주소 전환 뒤의
 * 전하 재분배가 다르다. `keys settle <n>` 으로 16 / 64 / 256 을 바꿔 가며 이웃 칸
 * 번짐을 본다.
 */
#define HW_KEYS_SETTLE_CYCLES       16

/*
 * 누르면 값이 오른다 — keys.c 가 입구에서 뒤집는다 (KEYS_RAW_IN).
 *
 * 센서가 바텀이고 스위치 자석이 N극 하향이라 패키지 뒷면으로 N극이 온다. 데이터시트의
 * 양의 B 방향이다. Esc 실측: 휴지 +4.7mT -> 바닥 +56mT, 0 을 지나지 않는다.
 */
#define HW_KEYS_PRESS_RISES         1

/*
 * 부팅 보정 이상치 문턱 (12비트 눈금) — wish60-he 값 그대로. **안 쟀다.**
 *
 * 정상 편차와 스트로크 사이여야 한다. 스트로크는 Esc 하나로 약 1040 이다. 정상
 * 편차는 키를 다 올린 뒤 `keys base` 로 잰다.
 */
#define HW_KEYS_CAL_OUTLIER_12B     500

/*
 * RGB — 체인 1개. PA29 = SPI1.MOSI (ALT5). wish60-he 와 같은 핀이다.
 * 이 보드에서 PA29 는 연결이 없다 (위 RGB 주석).
 */
#define HW_RGB_CHAIN_CNT            1
#define HW_RGB_LED_CNT_CH0          42
#define HW_RGB_LED_CNT_CH1          0
#define HW_RGB_CHAINS                                                          \
{                                                                              \
  { HPM_SPI1, clock_spi1, HW_DMA_CH_WS2812_0, HPM_DMA_SRC_SPI1_TX,             \
    IOC_PAD_PA29, 5, 0, HW_RGB_LED_CNT_CH0 },                                  \
}

#define HW_RGB_PWR_PIN_CNT          0
#define HW_RGB_KEY_LED_CNT          42

/* 전류 모델 — LED 가 없으니 리미터가 셈만 한다. wish60-he 값을 자리표시자로 둔다 */
#define HW_RGB_IDLE_MA              269
#define HW_RGB_CH_FULL_UA_KEY      11510
#define HW_RGB_CH_FULL_UA_UNDER     4660

#include "layout_qmk.h"
