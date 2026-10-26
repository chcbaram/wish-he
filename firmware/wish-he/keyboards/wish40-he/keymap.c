/*
 * keymap.c  —  자동 생성. 직접 고치지 말 것.
 *
 *   생성 : tools/gen_keymap.py
 *   원본 : keyboards/wish40-he/layout-kle.json
 *
 * 여기 값은 '공장 기본값'일 뿐이다. VIA 로 바꾼 키맵은 EEPROM 에 들어가고,
 * dynamic_keymap 이 그쪽을 먼저 본다.
 */
#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  [0] = LAYOUT(
    QK_GESC, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U,
    KC_I, KC_O, KC_P, KC_BSPC, LT(1, KC_TAB), KC_A, KC_S, KC_D,
    KC_F, KC_G, KC_H, KC_J, KC_K, KC_L, KC_ENT, KC_LSFT,
    KC_Z, KC_X, KC_C, KC_V, KC_B, KC_N, KC_M, KC_COMM,
    KC_UP, LT(2, KC_SLSH), KC_LCTL, KC_LGUI, KC_LALT, KC_SPC, KC_SPC, KC_LEFT,
    KC_DOWN, KC_RGHT
  ),
  [1] = LAYOUT(
    QK_BOOT, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7,
    KC_8, KC_9, KC_0, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_COMM,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS
  ),
  [2] = LAYOUT(
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, RGB_TOG, RGB_MODE_FORWARD, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS
  ),
};
