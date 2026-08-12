#ifndef WS2812_H_
#define WS2812_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_WS2812


bool ws2812Init(void);

void ws2812SetColor(uint16_t ch, uint8_t red, uint8_t green, uint8_t blue);
void ws2812SetColorAll(uint8_t red, uint8_t green, uint8_t blue);
void ws2812Clear(void);

/* 프레임버퍼를 LED 로 밀어낸다. 현재는 블로킹 전송이다(2 ms). */
bool ws2812Refresh(void);

uint16_t ws2812GetMaxCh(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
