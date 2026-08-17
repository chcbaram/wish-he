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
    {     53,     27,      6,     49,     10,     13, NO_LED,     40 },
    {     58,     32,     23,     60,     19,     15,     43,     41 },
    {     54,     31,     24,     50,     20,     16,     64,     42 },
    {     52,      2,      5,     36,      9,     14,     44,     39 },
    {     56,     29,     26,     33,     22,     18,     62,     48 },
    {     55,      0,      3,     34,      7,     11,     46,     37 },
    {     57,     28,     25,     51,     21,     17,     63,     47 },
    {     30,      1,      4,     35,      8,     12,     45,     38 },
  },
  {   /* point */
    {  7,  6}, { 22,  6}, { 37,  6}, { 52,  6}, { 67,  6}, { 82,  6},
    { 97,  6}, {112,  6}, {127,  6}, {142,  6}, {157,  6}, {172,  6},
    {187,  6}, {209,  6}, {224,  6}, {224,  6}, {213, 19}, {194, 19},
    {179, 19}, {164, 19}, {149, 19}, {134, 19}, {119, 19}, {105, 19},
    { 90, 19}, { 75, 19}, { 60, 19}, { 45, 19}, { 30, 19}, { 11, 19},
    { 13, 32}, { 34, 32}, { 49, 32}, { 63, 32}, { 78, 32}, { 93, 32},
    {108, 32}, {123, 32}, {138, 32}, {153, 32}, {168, 32}, {183, 32},
    {207, 32}, {217, 45}, {196, 45}, {175, 45}, {161, 45}, {146, 45},
    {131, 45}, {116, 45}, {101, 45}, { 86, 45}, { 71, 45}, { 56, 45},
    { 41, 45}, { 17, 45}, { 11, 58}, { 30, 58}, { 49, 58}, { 77, 58},
    {112, 58}, {147, 58}, {175, 58}, {194, 58}, {213, 58}, {205, 64},
    {168, 64}, {131, 64}, { 93, 64}, { 56, 64}, { 19, 64}, {  0, 53},
    {  0, 32}, {  0, 11}, { 19,  0}, { 56,  0}, { 93,  0}, {131,  0},
    {168,  0}, {205,  0}, {224, 11}, {224, 32}, {224, 53},
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
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT, LED_FLAG_KEYLIGHT,
    LED_FLAG_KEYLIGHT, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW,
    LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW,
    LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW,
    LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW,
    LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW, LED_FLAG_UNDERGLOW,
  },
};

#endif
