#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"

#include "board.h"
#include "hpm_common.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"


/*
 * STM32 에서는 CMSIS 가 제공하던 매크로들이다.
 * 공용 프레임워크 소스(src/common)가 이 이름을 그대로 쓰므로 bsp 계층에서 채워준다.
 */
#ifndef __WEAK
#define __WEAK    __attribute__((weak))
#endif

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif


void logPrintf(const char *fmt, ...);



bool bspInit(void);

void delay(uint32_t time_ms);
void delayUs(uint32_t delay_us);
uint32_t millis(void);
uint32_t micros(void);

void Error_Handler(void);


#ifdef __cplusplus
}
#endif

#endif
