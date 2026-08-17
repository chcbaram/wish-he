#pragma once

/*
 * QMK 포트 계층 — 이 보드가 QMK 에 채워 넣는 것들.
 *
 * ★ HE 판정은 QMK 안이 아니라 matrix 아래에 있다.
 *
 *   keys.c 가 ADC 스캔 주기로 깊이를 보고 눌림을 정한 뒤 matrix_row_t 로 내놓는다.
 *   QMK 는 그 비트만 본다. 그래서
 *
 *     - 디바운스를 쓰지 않는다. HE 는 접점이 없어 바운스가 없고, QMK 의 기본
 *       디바운스(5~20ms)는 순수한 지연이다.
 *     - 래피드 트리거(13편)도 여기가 아니라 keys.c 에서 처리한다. 0.1mm 단위
 *       방향 반전은 스캔 주기에서 봐야 하고, QMK 루프 주기에 묶이면 안 된다.
 */

#include "via_hid.h"
#include "via.h"
#include "eeconfig.h"


#define QMK_BUILDDATE   "2026-08-15-00:00:00"
