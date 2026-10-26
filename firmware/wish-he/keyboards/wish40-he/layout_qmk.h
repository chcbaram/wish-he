/*
 * layout_qmk.h  —  자동 생성. 직접 고치지 말 것.
 *
 *   생성 : tools/gen_keymap.py
 *   원본 : keyboards/wish40-he/layout-kle.json
 *
 * LAYOUT 은 물리 배치 순서로 받아 매트릭스 자리에 흩뿌린다.
 * 스위치가 없는 셀은 KC_NO 로 채운다.
 */
#ifndef LAYOUT_QMK_H_
#define LAYOUT_QMK_H_

#define LAYOUT( \
  k00, k01, k02, k03, k04, k05, k06, k07, \
  k08, k09, k10, k11, k12, k13, k14, k15, \
  k16, k17, k18, k19, k20, k21, k22, k23, \
  k24, k25, k26, k27, k28, k29, k30, k31, \
  k32, k33, k34, k35, k36, k37, k38, k39, \
  k40, k41 \
  ) { \
    { k34, k35, KC_NO, KC_NO, KC_NO, k40 }, \
    { KC_NO, k36, k37, k38, k39, k41 }, \
    { KC_NO, k25, k27, k29, k31, k33 }, \
    { k23, k24, k26, k28, k30, k32 }, \
    { k00, k02, k04, k06, k08, k10 }, \
    { k01, k03, k05, k07, k09, k11 }, \
    { k13, k15, k17, k19, k21, KC_NO }, \
    { k12, k14, k16, k18, k20, k22 }, \
  }

#endif
