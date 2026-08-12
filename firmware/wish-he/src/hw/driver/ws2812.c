/*
 * ws2812.c
 *
 * WS2812(NeoPixel) 드라이버 — SPI1 MOSI 비트패턴 방식.
 *
 * 이 보드에는 외부 디버깅용 LED 도, UART 헤더도 없다. 네오픽셀이 유일한
 * 시각적 상태 표시 수단이라 일찍 올린다.
 *
 * 하드웨어 (덤프 분석으로 확정 — ../hpm5361-fw/docs/flash_dump.md 6절):
 *   - 데이터 핀 : PA29 = SPI1.MOSI (ALT5)
 *   - SCLK      : 8 MHz  → SPI 1 바이트 = 1.0us = WS2812 1 비트
 *   - LED 개수  : 83
 *
 *   PA29 먹싱은 IAP 부트로더가 이미 해주지만(0x8000EDD2), 우리 앱만으로도
 *   서도록 여기서 다시 설정한다.
 *
 * 비트 인코딩 (125ns/SPI비트 기준, WS2812B 규격 T0H 0.4us / T1H 0.8us):
 *   0 -> 0xE0 (상위 3비트 high = 0.375us)
 *   1 -> 0xFC (상위 6비트 high = 0.750us)
 *   주기는 1.0us 로 규격(1.25us ±600ns) 안이다.
 *
 * 현재는 블로킹 전송이다. 프레임 1992 B / 8MHz = 약 2ms.
 * 키 스캔이 들어오면 상용 펌웨어처럼 SPI+DMA 로 바꿔야 한다
 * (상용은 hpm_spi_dma_install_callback 을 쓴다).
 */
#include "ws2812.h"


#ifdef _USE_HW_WS2812
#include "cli.h"

#include "hpm_spi_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_iomux.h"


#if CLI_USE(HW_WS2812)
static void cliWs2812(cli_args_t *args);
#endif


#define WS2812_SPI            HPM_SPI1
#define WS2812_SPI_HZ         8000000U

#define WS2812_BIT_0          0xE0
#define WS2812_BIT_1          0xFC

#define WS2812_BYTES_PER_LED  24              /* 8bit x 3색 x 1byte */
#define WS2812_RESET_BYTES    60              /* 60us low = latch (규격 50us 이상) */

#define WS2812_BUF_LEN        ((HW_WS2812_MAX_CH * WS2812_BYTES_PER_LED) + WS2812_RESET_BYTES)


static bool     is_init = false;
static uint8_t  frame_buf[WS2812_BUF_LEN];
static spi_control_config_t ctrl_config;


/* 테스트 패턴 밝기. 전류 예산 때문에 낮게 유지한다 (docs/README.md 4절). */
#define WS2812_TEST_LEVEL     12


/*
 * 색상환 -> RGB. hue 0~255 를 6구간으로 나눈 간이 변환이며, 채도·명도는
 * level 하나로만 준다. 테스트 패턴 전용이라 정확도보다 단순함을 택했다.
 */
static void ws2812Hue(uint8_t hue, uint8_t level, uint8_t *p_r, uint8_t *p_g, uint8_t *p_b)
{
  uint8_t seg  = hue / 43;            /* 0~5 */
  uint8_t frac = (uint8_t)(((hue % 43) * level) / 43);
  uint8_t up   = frac;
  uint8_t down = (uint8_t)(level - frac);

  switch (seg)
  {
    case 0:  *p_r = level; *p_g = up;    *p_b = 0;     break;
    case 1:  *p_r = down;  *p_g = level; *p_b = 0;     break;
    case 2:  *p_r = 0;     *p_g = level; *p_b = up;    break;
    case 3:  *p_r = 0;     *p_g = down;  *p_b = level; break;
    case 4:  *p_r = up;    *p_g = 0;     *p_b = level; break;
    default: *p_r = level; *p_g = 0;     *p_b = down;  break;
  }
}

/* 한 바이트(8비트)를 프레임버퍼 8 바이트로 펼친다. MSB first. */
static void ws2812WriteByte(uint8_t *p_buf, uint8_t data)
{
  for (int i = 0; i < 8; i++)
  {
    p_buf[i] = (data & (0x80 >> i)) ? WS2812_BIT_1 : WS2812_BIT_0;
  }
}


bool ws2812Init(void)
{
  spi_timing_config_t timing_config = {0};
  spi_format_config_t format_config = {0};

  /* PA29 = SPI1.MOSI. IAP 가 이미 설정하지만 자립을 위해 다시 잡는다. */
  HPM_IOC->PAD[IOC_PAD_PA29].FUNC_CTL = IOC_PA29_FUNC_CTL_SPI1_MOSI;

  clock_add_to_group(clock_spi1, 0);

  spi_master_get_default_timing_config(&timing_config);
  timing_config.master_config.clk_src_freq_in_hz = clock_get_frequency(clock_spi1);
  timing_config.master_config.sclk_freq_in_hz    = WS2812_SPI_HZ;
  if (spi_master_timing_init(WS2812_SPI, &timing_config) != status_success)
  {
    cliPrintf("[NG] ws2812Init() timing\n");
    return false;
  }

  spi_master_get_default_format_config(&format_config);
  format_config.common_config.data_len_in_bits = 8;
  format_config.common_config.data_merge       = false;
  format_config.common_config.mosi_bidir       = false;
  format_config.common_config.lsb              = false;   /* MSB first */
  format_config.common_config.mode             = spi_master_mode;
  format_config.common_config.cpol             = spi_sclk_low_idle;
  format_config.common_config.cpha             = spi_sclk_sampling_odd_clk_edges;
  spi_format_init(WS2812_SPI, &format_config);

  /* 매 전송마다 쓰는 제어 설정. 커맨드/주소 없이 데이터만 보낸다. */
  spi_master_get_default_control_config(&ctrl_config);
  ctrl_config.master_config.cmd_enable  = false;
  ctrl_config.master_config.addr_enable = false;
  ctrl_config.common_config.tx_dma_enable = false;
  ctrl_config.common_config.rx_dma_enable = false;
  ctrl_config.common_config.trans_mode    = spi_trans_write_only;

  ws2812Clear();

  is_init = true;
  cliPrintf("[OK] ws2812Init()\n");
  cliPrintf("     ch : %d\n", HW_WS2812_MAX_CH);

#if CLI_USE(HW_WS2812)
  cliAdd("ws2812", cliWs2812);
#endif

  return is_init;
}

void ws2812SetColor(uint16_t ch, uint8_t red, uint8_t green, uint8_t blue)
{
  uint8_t *p_buf;

  if (ch >= HW_WS2812_MAX_CH) return;

  p_buf = &frame_buf[ch * WS2812_BYTES_PER_LED];

  /* WS2812 는 GRB 순서다 */
  ws2812WriteByte(&p_buf[0],  green);
  ws2812WriteByte(&p_buf[8],  red);
  ws2812WriteByte(&p_buf[16], blue);
}

void ws2812SetColorAll(uint8_t red, uint8_t green, uint8_t blue)
{
  for (uint16_t i = 0; i < HW_WS2812_MAX_CH; i++)
  {
    ws2812SetColor(i, red, green, blue);
  }
}

void ws2812Clear(void)
{
  ws2812SetColorAll(0, 0, 0);

  /* latch 구간은 항상 low */
  for (int i = 0; i < WS2812_RESET_BYTES; i++)
  {
    frame_buf[HW_WS2812_MAX_CH * WS2812_BYTES_PER_LED + i] = 0x00;
  }
}

bool ws2812Refresh(void)
{
  hpm_stat_t status;

  if (is_init != true) return false;

  status = spi_transfer(WS2812_SPI, &ctrl_config,
                        NULL, NULL,
                        frame_buf, WS2812_BUF_LEN,
                        NULL, 0);

  return (status == status_success);
}

uint16_t ws2812GetMaxCh(void)
{
  return HW_WS2812_MAX_CH;
}


#if CLI_USE(HW_WS2812)
void cliWs2812(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("ws2812 ch    : %d\n", HW_WS2812_MAX_CH);
    cliPrintf("frame bytes  : %d\n", WS2812_BUF_LEN);
    cliPrintf("spi clk      : %d Hz\n", (int)clock_get_frequency(clock_spi1));
    ret = true;
  }

  if (args->argc == 4 && args->isStr(0, "all"))
  {
    uint8_t r = (uint8_t)args->getData(1);
    uint8_t g = (uint8_t)args->getData(2);
    uint8_t b = (uint8_t)args->getData(3);

    ws2812SetColorAll(r, g, b);
    cliPrintf("all %d %d %d : %s\n", r, g, b, ws2812Refresh() ? "OK" : "NG");
    ret = true;
  }

  if (args->argc == 5 && args->isStr(0, "set"))
  {
    uint16_t ch = (uint16_t)args->getData(1);
    uint8_t  r  = (uint8_t)args->getData(2);
    uint8_t  g  = (uint8_t)args->getData(3);
    uint8_t  b  = (uint8_t)args->getData(4);

    ws2812SetColor(ch, r, g, b);
    cliPrintf("set %d : %s\n", ch, ws2812Refresh() ? "OK" : "NG");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "off"))
  {
    ws2812Clear();
    cliPrintf("off : %s\n", ws2812Refresh() ? "OK" : "NG");
    ret = true;
  }

  /* 흐르는 점 — R/G/B 3개가 순회한다. 켜지는 LED 가 3개뿐이라 전류가 거의 없다. */
  if (args->argc == 1 && args->isStr(0, "test"))
  {
    uint16_t pos = 0;

    cliPrintf("chase... (아무 키나 누르면 종료)\n");
    while (cliKeepLoop())
    {
      ws2812Clear();
      ws2812SetColor( pos                        , WS2812_TEST_LEVEL, 0, 0);
      ws2812SetColor((pos + 1) % HW_WS2812_MAX_CH, 0, WS2812_TEST_LEVEL, 0);
      ws2812SetColor((pos + 2) % HW_WS2812_MAX_CH, 0, 0, WS2812_TEST_LEVEL);
      ws2812Refresh();

      pos = (pos + 1) % HW_WS2812_MAX_CH;
      delay(40);
    }
    ws2812Clear();
    ws2812Refresh();
    ret = true;
  }

  /* 무지개 — 전체 LED 에 색상환을 뿌리고 천천히 회전시킨다. */
  if (args->argc == 1 && args->isStr(0, "rainbow"))
  {
    uint16_t offset = 0;

    cliPrintf("rainbow... (아무 키나 누르면 종료)\n");
    while (cliKeepLoop())
    {
      for (uint16_t i = 0; i < HW_WS2812_MAX_CH; i++)
      {
        uint8_t r, g, b;

        ws2812Hue((uint8_t)(((i * 256) / HW_WS2812_MAX_CH + offset) & 0xFF),
                  WS2812_TEST_LEVEL, &r, &g, &b);
        ws2812SetColor(i, r, g, b);
      }
      ws2812Refresh();

      offset = (offset + 2) & 0xFF;
      delay(20);
    }
    ws2812Clear();
    ws2812Refresh();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("ws2812 info\n");
    cliPrintf("ws2812 all <r> <g> <b>\n");
    cliPrintf("ws2812 set <ch> <r> <g> <b>\n");
    cliPrintf("ws2812 off\n");
    cliPrintf("ws2812 test        - 흐르는 점\n");
    cliPrintf("ws2812 rainbow     - 무지개 회전\n");
  }
}
#endif

#endif /* _USE_HW_WS2812 */
