/*
 * rgb_config.c  —  자동 생성. 직접 고치지 말 것.
 *   tools/gen_keymap.py 가 layout-kle.json 에서 만든다.
 *
 * g_led_config — QMK rgb_matrix 가 보는 배치.
 *   matrix_co  (row, col) -> LED 인덱스, 없으면 NO_LED
 *   point      LED 물리 좌표. x 0~224, y 0~64 로 normalize 한 값
 *   flags      LED_FLAG_KEYLIGHT(4) = 키 밑, LED_FLAG_UNDERGLOW(2) = 언더글로우
 */
#include "quantum.h"

#ifdef RGB_MATRIX_ENABLE

led_config_t g_led_config = {
  {   /* matrix_co */
    {     41,     40, NO_LED, NO_LED, NO_LED,     35 },
    { NO_LED,     39,     38,     37,     36,     34 },
    { NO_LED,     25,     27,     29,     31,     33 },
    {     23,     24,     26,     28,     30,     32 },
    {      0,      2,      4,      6,      8,     10 },
    {      1,      3,      5,      7,      9,     11 },
    {     21,     19,     17,     15,     13, NO_LED },
    {     22,     20,     18,     16,     14,     12 },
  },
  {   /* point */
    {  9,  8}, { 28,  8}, { 47,  8}, { 65,  8}, { 84,  8}, {103,  8},
    {121,  8}, {140,  8}, {159,  8}, {177,  8}, {196,  8}, {215,  8},
    {208, 24}, {182, 24}, {163, 24}, {145, 24}, {126, 24}, {107, 24},
    { 89, 24}, { 70, 24}, { 51, 24}, { 33, 24}, { 12, 24}, { 16, 40},
    { 42, 40}, { 61, 40}, { 79, 40}, { 98, 40}, {117, 40}, {135, 40},
    {154, 40}, {173, 40}, {194, 40}, {215, 40}, {212, 56}, {191, 56},
    {170, 56}, {138, 56}, { 91, 56}, { 54, 56}, { 33, 56}, { 12, 56},
  },
  {   /* flags */
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
  },
};

#endif
