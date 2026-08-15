#ifndef KEYS_H_
#define KEYS_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_KEYS


#define KEYS_STEP_MAX     HW_KEYS_STEP_MAX    /* MUX 스텝 = 논리 행 */
#define KEYS_CH_MAX       HW_KEYS_CH_MAX      /* ADC 채널 = 논리 열 */
#define KEYS_MAX          (KEYS_STEP_MAX * KEYS_CH_MAX)


bool     keysInit(void);

/* 한 바퀴(8스텝) 스캔. 원시값을 갱신한다. */
bool     keysUpdate(void);

/* 직전 스캔의 원시값. step = MUX 스텝, ch = ADC 채널 */
uint16_t keysGetRaw(uint8_t step, uint8_t ch);

/* 직전 스캔 한 바퀴에 걸린 시간 (us). 12편 예산 계산의 근거가 된다. */
uint32_t keysGetScanTime(void);

/*
 * 리포트를 내보내도 되는가.
 *
 * keys 명령(매핑·보정·관측)이 도는 동안은 false 다. 측정하려고 누른 키가 호스트로
 * 입력되면 터미널이 엉켜 측정을 못 한다.
 */
bool     keysIsReportEnabled(void);

/*
 * 무압 기준값을 다시 잡는다. 부팅 때 자동으로 한 번 한다.
 *
 * 채널마다 기준값이 다르므로(자석·센서·기구 공차) 절대값이 아니라 기준값 대비
 * 편차로 판정한다.
 */
bool     keysCalibrate(void);

/* 눌림 판정. row = MUX 스텝, col = ADC 채널 */
bool     keysGetPressed(uint16_t row, uint16_t col);

/* 레이아웃에 실재하는 셀인가 (keyboards/<모델>/layout.h) */
bool     keysIsPresent(uint16_t row, uint16_t col);

/* 그 자리의 HID Usage ID. 0 이면 배정 안 됨 */
uint8_t  keysGetKeycode(uint16_t row, uint16_t col);

/* 행 비트마스크 (col c -> bit c). QMK matrix_row_t 와 비트 순서가 같다. */
uint16_t keysGetRow(uint16_t row);

uint16_t keysGetBase(uint8_t step, uint8_t ch);
int32_t  keysGetDelta(uint8_t step, uint8_t ch);

/*
 * mm 환산 — 라이브 트래킹과 13편(래피드 트리거)이 쓴다. 단위는 0.01mm.
 *
 * 깊이의 영점은 살아 있는 기준값이고, 기울기만 보정에서 온다. 보정하지 않은 키도
 * 스위치 종류표의 공칭값으로 환산되므로 항상 값이 나온다.
 */
uint16_t keysGetTravelUm(uint16_t row, uint16_t col);   /* 그 키의 전 행정 */
uint16_t keysGetDepthUm(uint16_t row, uint16_t col);    /* 지금 눌린 깊이 */

/* 물리 배치 — {x, y, w, h, row, col} 6바이트, 1/4 키유닛. 웹 도구가 JSON 없이 그린다. */
uint32_t       keysGetLayoutCount(void);
const uint8_t *keysGetLayoutEntry(uint32_t idx);


#endif


#ifdef __cplusplus
}
#endif

#endif
