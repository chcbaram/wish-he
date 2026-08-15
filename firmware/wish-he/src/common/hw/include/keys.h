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

/* 직전 스캔 한 바퀴에 걸린 시간 (us). 8편 이후 예산 계산의 근거가 된다. */
uint32_t keysGetScanTime(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
