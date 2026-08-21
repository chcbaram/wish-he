// Copyright 2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later

#include "suspend.h"
#include "matrix.h"
#include "action.h"

// TODO: Move to more correct location
__attribute__((weak)) void matrix_power_up(void) {}
__attribute__((weak)) void matrix_power_down(void) {}

/** \brief Run user level Power down
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_power_down_user(void) {}

/** \brief Run keyboard level Power down
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_power_down_kb(void) {
    suspend_power_down_user();
}

/** \brief run user level code immediately after wakeup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_wakeup_init_user(void) {}

/** \brief run keyboard level code immediately after wakeup
 *
 * FIXME: needs doc
 */
__attribute__((weak)) void suspend_wakeup_init_kb(void) {
    suspend_wakeup_init_user();
}

/** \brief suspend wakeup condition
 *
 * FIXME: needs doc
 */
bool suspend_wakeup_condition(void) {
    matrix_power_up();
    matrix_scan();
    matrix_power_down();
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
        if (matrix_get_row(r)) return true;
    }
    return false;
}

void suspend_power_down(void)
{
  suspend_power_down_quantum();
}

/*
 * ★ 깨어나면 눌린 것을 먼저 비운다.
 *
 *   자는 동안에도 스캔은 계속 돈다. 그런데 리포트는 못 나간다 — 호스트가 안
 *   가져가니 엔드포인트에 실린 채로 남는다. 그래서 잠들 때 눌려 있던 키를 자는
 *   사이에 뗐으면, **호스트는 눌림만 받고 뗌은 못 받는다.**
 *
 *   반대쪽도 있다. 호스트는 서스펜드에서 제 키 상태를 비우는데 우리는 안 비우면,
 *   깨어난 뒤 우리 쪽 리포트가 "안 바뀌었다"로 걸러져 실제로 누르고 있는 키를
 *   호스트가 모르는 채로 간다.
 *
 *   어느 쪽이든 답은 같다 — 깨어나는 자리에서 양쪽을 0 으로 맞춘다. 상류 QMK 의
 *   AVR 포트도 같은 자리에서 같은 것을 한다.
 */
void suspend_wakeup_init(void)
{
  clear_keyboard();
  suspend_wakeup_init_quantum();
}