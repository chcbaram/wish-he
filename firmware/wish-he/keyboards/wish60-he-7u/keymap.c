/*
 * keymap.c  —  자동 생성. 직접 고치지 말 것.
 *
 *   생성 : tools/gen_keymap.py
 *   원본 : keyboards/wish60-he-7u/layout-kle.json
 *
 * 여기 값은 '공장 기본값'일 뿐이다. VIA 로 바꾼 키맵은 EEPROM 에 들어가고,
 * dynamic_keymap 이 그쪽을 먼저 본다.
 */
#include QMK_KEYBOARD_H

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  [0] = LAYOUT(
    KC_ESC, KC_1, KC_2, KC_3, KC_4, KC_5, KC_6, KC_7,
    KC_8, KC_9, KC_0, KC_MINS, KC_EQL, KC_BSPC, KC_NO, KC_NO,
    KC_TAB, KC_Q, KC_W, KC_E, KC_R, KC_T, KC_Y, KC_U,
    KC_I, KC_O, KC_P, KC_LBRC, KC_RBRC, KC_BSLS, KC_CAPS, KC_A,
    KC_S, KC_D, KC_F, KC_G, KC_H, KC_J, KC_K, KC_L,
    KC_SCLN, KC_QUOT, KC_ENT, KC_LSFT, KC_Z, KC_X, KC_C, KC_V,
    KC_B, KC_N, KC_M, KC_COMM, KC_DOT, KC_SLSH, KC_RSFT, MO(1),
    KC_LCTL, KC_LGUI, KC_LALT, KC_SPC, KC_RALT, KC_RGUI, KC_RCTL
  ),
  [1] = LAYOUT(
    KC_GRV, KC_F1, KC_F2, KC_F3, KC_F4, KC_F5, KC_F6, KC_F7,
    KC_F8, KC_F9, KC_F10, KC_F11, KC_F12, KC_DEL, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_PGUP, KC_TRNS,
    KC_UP, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_HOME, KC_LEFT, KC_DOWN, KC_RGHT,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    RGB_TOG, RGB_MODE_FORWARD, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
    KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
  ),
};
