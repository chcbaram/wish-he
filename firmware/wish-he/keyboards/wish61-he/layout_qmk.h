/*
 * layout_qmk.h  —  자동 생성. 직접 고치지 말 것.
 *
 *   생성 : tools/gen_keymap.py
 *   원본 : keyboards/wish61-he/layout-kle.json
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
  k56, k57, k58, k59, k60 \
  ) { \
    { k57, k46, k42, k29, k16, k03, k10, k23 }, \
    { k52, k56, k41, k28, k15, k02, k09, k37 }, \
    { KC_NO, k35, k53, k00, k14, k01, k22, k36 }, \
    { k58, k47, k43, k30, k17, k04, k11, k24 }, \
    { k60, k50, k44, k33, k20, k06, k13, k25 }, \
    { k59, k49, k54, k32, k19, k07, k27, k38 }, \
    { KC_NO, k51, k45, k34, k21, k05, k12, k26 }, \
    { KC_NO, k48, k55, k31, k18, k08, k40, k39 }, \
  }

#endif
