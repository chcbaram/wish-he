/*
 * rgb_matrix_port.c  —  QMK rgb_matrix 를 우리 WS2812 드라이버에 잇는다.
 *
 * QMK 의 ws2812 드라이버는 쓰지 않는다. 우리 것은 SPI + HDMA 논블로킹이고
 * **프레임 전류 리미터**가 붙어 있어서다 (docs/14-led-limiter.md). 여기서는
 * 네 함수만 채우면 된다.
 *
 * ★ flush 가 실패할 수 있다.
 *
 *   ws2812Refresh() 는 앞 프레임이 아직 나가는 중이면 false 를 준다. 83개를
 *   밀어내는 데 2ms 가 걸리므로 60fps(16ms) 에서는 겹칠 일이 없지만, 겹치면
 *   그 프레임을 버리는 것이 맞다 — 기다리면 메인 루프가 막혀 스캔이 밀린다.
 *   다음 플러시가 최신 색으로 다시 나간다.
 */
#include "quantum.h"

#ifdef RGB_MATRIX_ENABLE

#include "rgb_matrix.h"
#include "ws2812.h"


static void rgbInit(void)
{
  /* ws2812Init() 은 hw 초기화에서 이미 끝났다. 여기서 다시 잡을 것이 없다. */
}

static void rgbSetColor(int index, uint8_t red, uint8_t green, uint8_t blue)
{
  ws2812SetColor((uint16_t)index, red, green, blue);
}

static void rgbSetColorAll(uint8_t red, uint8_t green, uint8_t blue)
{
  ws2812SetColorAll(red, green, blue);
}

static void rgbFlush(void)
{
  (void)ws2812Refresh();   /* 앞 프레임이 나가는 중이면 이번 건 버린다 */
}

const rgb_matrix_driver_t rgb_matrix_driver =
{
  .init          = rgbInit,
  .set_color     = rgbSetColor,
  .set_color_all = rgbSetColorAll,
  .flush         = rgbFlush,
};

#endif
