/*
 * layout_qmk.h  —  자동 생성. 직접 고치지 말 것.
 *
 *   생성 : tools/gen_keymap.py
 *   원본 : keyboards/wish60-he-7u/layout-kle.json
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
  k40, k41, k42, k43, k44, k45, k46, k47, \
  k48, k49, k50, k51, k52, k53, k54, k55, \
  k56, k57, k58, k59, k60, k61, k62 \
  ) { \
    { k45, k18, k06, k49, k10, k13, KC_NO, k40 }, \
    { k58, k32, k22, k59, k26, k15, k55, k41 }, \
    { k44, k31, k21, k48, k25, k29, k62, k42 }, \
    { k46, k02, k05, k36, k09, k14, k54, k39 }, \
    { k56, k16, k19, k33, k23, k27, k60, k50 }, \
    { k43, k00, k03, k34, k07, k11, k52, k37 }, \
    { k57, k17, k20, k47, k24, k28, k61, k51 }, \
    { k30, k01, k04, k35, k08, k12, k53, k38 }, \
  }

#endif
