#ifndef RESET_H_
#define RESET_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_RESET


#define RESET_BIT_POWER       0
#define RESET_BIT_PIN         1
#define RESET_BIT_WDG         2
#define RESET_BIT_SOFT        3
#define RESET_BIT_ETC         4
#define RESET_BIT_MAX         5


#define MODE_BIT_BOOT         0
#define MODE_BIT_UPDATE       1
#define MODE_BIT_MAX          2


bool resetInit(void);
void resetLog(void);
void resetToBoot(void);

/* 메인 루프에서 주기적으로 부른다. 살아 있으면 이번 부팅을 성공으로 표시한다. */
void resetBootAlive(void);

/* 부팅 안전망 시험 — 다음 부팅에서 일부러 멈추게 한다 (.noinit) */
bool resetGetHangTest(void);
void resetSetHangTest(bool on);
void resetToReset(void);

uint32_t resetGetBits(void);
void     resetSetBits(uint32_t data);
bool     resetSetBootMode(uint32_t data);   /* 기록+검증 성공 여부 */
uint32_t resetGetBootMode(void);

#endif


#ifdef __cplusplus
}
#endif

#endif
