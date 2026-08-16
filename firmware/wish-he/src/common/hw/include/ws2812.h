#ifndef WS2812_H_
#define WS2812_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_WS2812


/*
 * 전류 예산을 두 무리(키 LED / 언더글로우)에 어떻게 나눌지.
 *
 *   SHARED      한 통. 안 쓰는 쪽 몫이 자동으로 넘어간다
 *   KEY_FIRST   키 LED 부터 채우고 남은 것을 언더글로우에.
 *               언더글로우를 켜도 각인 밝기가 변하지 않는다
 *   UNDER_FIRST 반대
 *
 * 어느 쪽이든 **합계는 같은 상한을 넘지 않는다.** 순서만 정하는 것이다.
 */
typedef enum
{
  WS2812_PRIO_SHARED = 0,
  WS2812_PRIO_KEY_FIRST,
  WS2812_PRIO_UNDER_FIRST,
} ws2812_prio_t;


bool ws2812Init(void);

void ws2812SetColor(uint16_t ch, uint8_t red, uint8_t green, uint8_t blue);
void ws2812SetColorAll(uint8_t red, uint8_t green, uint8_t blue);
void ws2812Clear(void);

/*
 * 설정한 색을 LED 로 밀어낸다. SPI+DMA 논블로킹 — 바로 반환한다.
 * 프레임 전류가 상한을 넘으면 전 채널을 같은 비율로 줄여서 내보낸다.
 */
bool ws2812Refresh(void);

/*
 * 색이 안 바뀌어도 다음 Refresh 를 내보내게 한다.
 *
 * Refresh 는 색이 바뀐 프레임만 만든다. 전류 상한처럼 색 밖의 것이 바뀌면 여기로
 * 알려야 한다 — 안 그러면 새 상한이 다음 색 변화까지 반영되지 않는다.
 */
void ws2812Touch(void);

/* 이전 프레임이 아직 전송 중인지 */
bool ws2812IsBusy(void);

uint16_t ws2812GetMaxCh(void);

/* 프레임 전류 상한(mA). 저장되지 않으며 리셋하면 기본값으로 돌아간다. */
void     ws2812SetLimit(uint16_t max_ma);
uint16_t ws2812GetLimit(void);

void          ws2812SetPrio(ws2812_prio_t prio);
ws2812_prio_t ws2812GetPrio(void);

/*
 * 지금 설정된 색의 예상 전류(mA).
 *   limited = false  상한을 먹이기 전
 *   grp     = 0 키 LED / 1 언더글로우 (가변분만) / 그 외 전체 (고정분 포함)
 */
uint16_t ws2812GetFrameMa(bool limited, uint8_t grp);

/*
 * 리미터가 실제로 깎은 프레임 수. **평상시 0 이어야 한다.**
 * 0 이 아니면 효과가 예산을 넘긴 것이고, 켜진 개수에 따라 밝기가 출렁인다.
 */
uint32_t ws2812GetLimitHit(void);
void     ws2812ClearLimitHit(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
