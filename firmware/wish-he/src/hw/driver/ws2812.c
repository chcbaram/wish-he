/*
 * ws2812.c
 *
 * WS2812(NeoPixel) 드라이버 — SPI1 MOSI 비트패턴 방식.
 *
 * 이 보드에는 외부 디버깅용 LED 도, UART 헤더도 없다. 네오픽셀이 유일한
 * 시각적 상태 표시 수단이라 일찍 올린다.
 *
 * 하드웨어 (docs/00-hardware.md):
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
 * 전송은 SPI TX + HDMA 논블로킹이다. CPU 는 프레임버퍼만 채우고 바로 빠져나오므로
 * 키 스캔 루프를 막지 않는다 — 고속 스캔과 LED 가 공존하려면 이래야 한다.
 *
 * 주의: DMA 는 D-cache 를 보지 않는다. 전송 전에 프레임버퍼를 라이트백해야 한다.
 *
 * 전류 리미터 (docs/00-hardware.md 5절):
 *   83개를 밝게 켜면 USB 예산을 훌쩍 넘겨 브라운아웃된다. 그래서 색은 원본 그대로
 *   rgb_buf 에 담아 두고, ws2812Refresh() 에서 프레임 전체 전류를 합산해 넘치면
 *   전 채널을 같은 비율로 줄인 뒤에 비트패턴으로 펼친다.
 */
#include "ws2812.h"


#ifdef _USE_HW_WS2812
#include "cli.h"

#include "hpm_spi_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_iomux.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_misc.h"
#include "hpm_l1c_drv.h"


#if CLI_USE(HW_WS2812)
static void cliWs2812(cli_args_t *args);
#endif


#define WS2812_SPI            HPM_SPI1
#define WS2812_DMA_CH         HW_DMA_CH_WS2812   /* 채널 배분은 hw_def.h 참조 */
#define WS2812_SPI_HZ         8000000U

#define WS2812_BIT_0          0xE0
#define WS2812_BIT_1          0xFC

#define WS2812_BYTES_PER_LED  24              /* 8bit x 3색 x 1byte */
#define WS2812_RESET_BYTES    60              /* 60us low (규격 50us 이상) */

/*
 * 프레임 앞뒤에 모두 리셋 구간을 둔다.
 *
 * 뒤쪽만 두면 첫 LED 가 색을 잘못 받는다 — 실제로 LED 1개가 초록으로 남았고,
 * WS2812 는 GRB 순서라 초록이 첫 바이트다. 즉 전송 시작 시점의 라인 상태가
 * 불확실해 첫 비트가 깨진 것이다. 앞에 60us low 를 깔면 LED 가 확실히
 * 리셋 상태에서 데이터를 받기 시작한다.
 */
#define WS2812_LEAD_BYTES     WS2812_RESET_BYTES
#define WS2812_DATA_OFF       WS2812_LEAD_BYTES
#define WS2812_DATA_LEN       (HW_WS2812_MAX_CH * WS2812_BYTES_PER_LED)

#define WS2812_BUF_LEN        (WS2812_LEAD_BYTES + WS2812_DATA_LEN + WS2812_RESET_BYTES)

/*
 * LED 두 무리. 경계는 **전류계와 눈으로 확정했다.**
 *
 *   위쪽 0~64 (65개)     키마다 하나. 각인을 비추는 **기능**이다
 *   언더글로우 65~82 (18개)  아래를 향한다. 장식이다
 *
 * ★ 처음에는 "키가 63개이니 63 + 20" 으로 짐작했다가 틀렸다. 63~82 를 켜니 그중
 *   둘이 위에서 켜졌다. 63·64 만 빨강, 65~82 를 파랑으로 켜 보고서야 갈렸다.
 *   실제 키는 63개인데 위쪽 LED 가 65개인 것은 **7u 스페이스바 밑에만 3개**가
 *   들어가서다 — 길어서 하나로는 고르게 못 비춘다. 한 키에 LED 하나라는 전제가
 *   깨지는 자리가 이 보드에 있었다.
 *
 * 합산이라 한쪽을 끄면 그 몫이 자동으로 다른 쪽으로 넘어간다. 그건 공짜고, 옵션이
 * 필요한 건 **반대 방향**이다 — 언더글로우를 켰다고 각인이 어두워지지 않게 하려면
 * 순서를 정해야 한다 (ws2812_prio_t).
 */
#define WS2812_KEY_CNT        65
#define WS2812_GRP_MAX        2
#define WS2812_GRP_KEY        0
#define WS2812_GRP_UNDER      1

#define WS2812_GRP_OF(ch)     (((ch) < WS2812_KEY_CNT) ? WS2812_GRP_KEY : WS2812_GRP_UNDER)

/*
 * 전류 모델. 재는 자리는 **USB 입력**이고, 세는 것은 보드 전체 전류다.
 *
 *   보드 전류 = 고정분 + Σ(무리별 채널 값의 합 × 무리별 채널 전류 / 255)
 *
 * ★ 두 무리는 **서로 다른 LED 다.** 위쪽이 채널당 11.5mA, 언더글로우가 4.66mA 로
 *   2.5배 차이가 난다. 하나의 기울기로 세면 언더글로우를 2.5배 과대평가해 쓸 수
 *   있는 밝기를 그만큼 깎는다.
 *
 * ★ 처음에는 고정분을 LED 칩 몫(83 x 1mA = 83mA)으로, 기울기를 데이터시트의 20mA 로
 *   잡았다. **둘 다 틀렸다.** 전류계로 재니 고정분은 269mA(홀센서 64개가 상시 켜져
 *   있다), 기울기는 그 절반 이하였다.
 *
 *   고정분을 실측값으로 두면 WS2812_LIMIT_MA 가 곧 **USB 가 끌어가는 전체 전류**가
 *   되어 전류계로 그대로 검증된다.
 *
 * 실측 (docs/14-led-limiter.md) — 다섯 점이 ±3mA 안에 든다.
 *
 *              합       실측     모델
 *   전체 v=12   2988     384      386
 *   전체 v=24   5976     504      504
 *   위쪽 v=20   3900     445      445
 *   언더 v=160  8640     427      427
 *   63~82 v=38  2280     320      317    <- 두 무리가 섞인 경우
 *
 * uA 로 두는 이유는 4.66mA 를 정수 mA 로 반올림하면 7% 가 날아가기 때문이다.
 */
#define WS2812_CH_FULL_UA_KEY    11510   /* 위쪽 채널 1개 풀스케일. 실측 */
#define WS2812_CH_FULL_UA_UNDER   4660   /* 언더글로우 채널 1개 풀스케일. 실측 */
#define WS2812_IDLE_MA             269   /* LED 소등 시 보드 전체. 실측 */

/*
 * 보드 전체 전류 상한. USB 선언 500mA 에 50mA 여유를 뒀다.
 *
 * 사용자가 낮출 수는 있어도 넘길 수는 없는 안전선이라 컴파일 타임에 둔다.
 * CLI `ws2812 limit` 은 실측·시험용이며 저장되지 않는다.
 */
#define WS2812_LIMIT_MA_DEF   450

static const uint32_t ch_full_ua[WS2812_GRP_MAX] =
{
  [WS2812_GRP_KEY]   = WS2812_CH_FULL_UA_KEY,
  [WS2812_GRP_UNDER] = WS2812_CH_FULL_UA_UNDER,
};


static bool     is_init = false;

/*
 * ★ 캐시라인(32B)에 맞춰야 한다.
 *
 *   DMA 가 직접 읽으므로 전송 전에 l1c_dc_writeback() 으로 밀어내는데, 그 API 는
 *   시작 주소가 캐시라인 경계일 것을 assert 로 요구한다. 어긋나면 부팅 중에
 *   assert 로 멈추고 USB 도 못 올라온다.
 *
 *   이 정렬을 명시하기 전까지는 **우연히 맞아서** 돌고 있었다. QMK 를 얹으며
 *   .bss 가 16KB 늘자 이 버퍼가 밀렸고 곧바로 터졌다. 원인이 LED 와 상관없는
 *   곳에서 왔기 때문에 찾는 데 JTAG 이 필요했다.
 */
static __attribute__((aligned(HPM_L1C_CACHELINE_SIZE)))
uint8_t         frame_buf[HPM_L1C_CACHELINE_ALIGN_UP(WS2812_BUF_LEN)];

/*
 * 호출자가 준 색 원본. frame_buf 는 비트패턴이라 되읽어 합산할 수 없으므로
 * 리미터가 볼 수 있는 원본을 따로 들고 있는다. GRB 가 아니라 RGB 순서다 —
 * 순서 바꾸기는 펼칠 때 한다.
 */
static uint8_t  rgb_buf[HW_WS2812_MAX_CH][3];

static spi_control_config_t ctrl_config;
static volatile bool is_busy = false;
static uint16_t limit_ma = WS2812_LIMIT_MA_DEF;
static ws2812_prio_t prio = WS2812_PRIO_SHARED;

/*
 * 리미터가 실제로 깎은 프레임 수.
 *
 * ★ 이 값은 평상시 0 이어야 한다.
 *
 *   전역 배율에는 고유한 결함이 있다 — 켜진 개수가 변하면 **이미 켜져 있던 LED 의
 *   밝기까지 같이 변한다.** 반응형 효과에서 1키만 눌렸을 때 밝던 LED 가 10키를
 *   누르면 1/10 로 어두워진다. 타이핑 내내 전체가 출렁인다.
 *
 *   해법은 리미터를 손보는 게 아니라 층을 나누는 것이다. 효과가 쓰는 **밝기 상한**을
 *   전부 켜도 예산에 들어가도록 미리 정해 두면 배율이 걸릴 일이 없어 출렁임도 없다.
 *   리미터는 그 아래 깔린 안전망이고, 걸렸다면 효과 쪽이 예산을 넘긴 것이다.
 *
 *   그래서 세어 둔다. `ws2812 info` 가 0 이 아니면 밝기 상한을 다시 정해야 한다.
 */
static uint32_t limit_hit = 0;


/*
 * 테스트 패턴 밝기. 리미터가 생긴 뒤로는 안전을 위해 낮출 이유가 없어졌지만
 * 값을 바꾸면 이전 프레임들과 비교가 안 되므로 그대로 둔다.
 *
 * 상한 450mA 에서 **83개를 흰색으로 켜도 리미터에 안 걸리는 최대는 18**이다.
 * 효과를 얹을 때 정할 밝기 상한이 그 값이며, 그보다 낮게 두면 리미터가 영영 안 걸려
 * 켜진 개수에 따라 밝기가 출렁이는 일도 없다.
 */
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

/* 무리별 채널 값 합. 전체 최대가 83 x 3 x 255 = 63495 이라 uint32 로 충분하다. */
static void ws2812SumUnits(uint32_t *p_sum)
{
  p_sum[WS2812_GRP_KEY]   = 0;
  p_sum[WS2812_GRP_UNDER] = 0;

  for (uint16_t i = 0; i < HW_WS2812_MAX_CH; i++)
  {
    p_sum[WS2812_GRP_OF(i)] += rgb_buf[i][0] + rgb_buf[i][1] + rgb_buf[i][2];
  }
}

/*
 * 무리의 채널 값 합 -> 그 무리가 쓰는 전류(uA).
 *
 * 위쪽 최대는 65 x 3 x 255 x 11510 / 255 = 5.7e8 로 uint32 안에 있다.
 */
static uint32_t ws2812GrpUa(uint8_t grp, uint32_t sum)
{
  return (sum * ch_full_ua[grp]) / 255;
}

/*
 * 무리마다 예산에 맞추는 배율을 Q16 으로 채운다 (65536 = 그대로).
 *
 * 고정분은 줄일 수 없으므로 예산에서 먼저 빼고 남은 몫으로 가변분을 재단한다.
 * 나눗셈이 무리당 한 번 드는데 프레임당 두 번이라 문제되지 않는다 — 채널마다 하면
 * 안 된다.
 *
 * SHARED 는 둘을 한 통으로 보고 같은 배율을 준다. 우선순위가 걸리면 앞선 무리부터
 * 예산을 채우고 **남은 것**을 뒤에 준다. 어느 쪽이든 합계는 같은 상한 안에 있다.
 *
 * ★ Q8 로는 모자란다. 83개를 흰색으로 켜면 배율이 3% 근처까지 내려가는데 그 언저리에서
 *   Q8 눈금(0.4%)은 10% 넘는 오차가 된다. 배율 8.58 이 8 로 잘려 밝기를 6% 더 깎았다.
 *   **줄이는 쪽 오차라 안전하지만 색이 틀어진다** — R:G:B 가 각각 다르게 잘려 흰색이
 *   흰색으로 안 나온다. Q16 이면 눈금이 0.0015% 라 그 문제가 사라진다.
 *
 *   나눗셈은 배율이 1 미만일 때만 하므로 분자 쪽이 항상 분모보다 작다. 즉
 *   `x * 65536 < 63495 * 65536 = 4.16e9` 로 uint32 안에 있다.
 */
#define WS2812_SCALE_ONE      65536U

static void ws2812Scales(const uint32_t *p_sum, uint32_t *p_scale)
{
  uint32_t ua[WS2812_GRP_MAX];
  uint32_t budget_ua;

  /*
   * ★ 재단은 **전류**로 한다. 무리마다 채널당 전류가 다르므로 "채널 값의 합" 은
   *   더 이상 공통 화폐가 아니다 — 언더글로우 1000 과 위쪽 1000 은 같은 무게가
   *   아니다. 예산도 배분도 uA 로 셈해야 맞다.
   */
  for (int g = 0; g < WS2812_GRP_MAX; g++)
  {
    ua[g] = ws2812GrpUa((uint8_t)g, p_sum[g]);
  }

  budget_ua = (limit_ma > WS2812_IDLE_MA)
              ? ((uint32_t)(limit_ma - WS2812_IDLE_MA) * 1000U) : 0;

  if (prio == WS2812_PRIO_SHARED)
  {
    uint32_t tot = ua[0] + ua[1];
    /* tot 이 0 이면 0 <= budget 이라 나눗셈에 닿지 않는다 */
    uint32_t s   = (tot <= budget_ua) ? WS2812_SCALE_ONE
                                      : (uint32_t)(((uint64_t)budget_ua << 16) / tot);

    p_scale[0] = s;
    p_scale[1] = s;
    return;
  }

  {
    uint8_t  first = (prio == WS2812_PRIO_KEY_FIRST) ? WS2812_GRP_KEY : WS2812_GRP_UNDER;
    uint32_t left  = budget_ua;

    for (int i = 0; i < WS2812_GRP_MAX; i++)
    {
      uint8_t g = (i == 0) ? first : (uint8_t)(first ^ 1);

      if (ua[g] <= left)
      {
        p_scale[g] = WS2812_SCALE_ONE;
        left -= ua[g];
      }
      else
      {
        /* ua[g] > left >= 0 이라 0 나눗셈이 아니다 */
        p_scale[g] = (uint32_t)(((uint64_t)left << 16) / ua[g]);
        left = 0;
      }
    }
  }
}

/*
 * rgb_buf -> frame_buf. 여기서 리미터가 걸린다.
 *
 * 펼치기를 ws2812SetColor() 가 아니라 여기서 하는 이유는 **프레임 전체를 봐야
 * 비율을 정할 수 있기 때문**이다. 총 작업량은 그대로고(83색 x 24바이트) 자리만
 * 옮긴 것이며, 덤으로 SetColor() 가 24바이트 쓰기에서 3바이트 쓰기로 가벼워졌다.
 *
 * 반올림은 잘라내기다. 예산을 넘기지 않는 쪽으로만 틀린다. 대신 배율이 크게
 * 걸리면 어두운 채널이 0 으로 사라지는데, 전역 배율 방식에 따라오는 성질이다.
 */
static void ws2812Encode(void)
{
  uint32_t sum[WS2812_GRP_MAX];
  uint32_t scale[WS2812_GRP_MAX];

  ws2812SumUnits(sum);
  ws2812Scales(sum, scale);

  if (scale[0] != WS2812_SCALE_ONE || scale[1] != WS2812_SCALE_ONE)
  {
    limit_hit++;
  }

  for (uint16_t i = 0; i < HW_WS2812_MAX_CH; i++)
  {
    uint8_t *p_buf = &frame_buf[WS2812_DATA_OFF + i * WS2812_BYTES_PER_LED];
    uint32_t s     = scale[WS2812_GRP_OF(i)];
    uint8_t  r     = rgb_buf[i][0];
    uint8_t  g     = rgb_buf[i][1];
    uint8_t  b     = rgb_buf[i][2];

    /* 안전망으로만 쓰면 평상시 여기로 안 들어온다 */
    if (s != WS2812_SCALE_ONE)
    {
      r = (uint8_t)((r * s) >> 16);
      g = (uint8_t)((g * s) >> 16);
      b = (uint8_t)((b * s) >> 16);
    }

    /* WS2812 는 GRB 순서다 */
    ws2812WriteByte(&p_buf[0],  g);
    ws2812WriteByte(&p_buf[8],  r);
    ws2812WriteByte(&p_buf[16], b);
  }
}


bool ws2812Init(void)
{
  spi_timing_config_t timing_config = {0};
  spi_format_config_t format_config = {0};

  /* PA29 = SPI1.MOSI. IAP 가 이미 설정하지만 자립을 위해 다시 잡는다. */
  HPM_IOC->PAD[IOC_PAD_PA29].FUNC_CTL = IOC_PA29_FUNC_CTL_SPI1_MOSI;

  clock_add_to_group(clock_spi1, 0);

  /*
   * IAP 도 SPI1 로 LED 를 켠다. 넘어온 직후에는 전송이 아직 안 끝나 SPI 가
   * active 일 수 있고, 그러면 첫 spi_setup_dma_transfer() 가 busy 로 거부된다.
   * 실제로 부팅 시 소등만 안 되고 나중의 'ws2812 off' 는 되는 증상이 있었다.
   */
  (void)spi_wait_for_idle_status(WS2812_SPI);
  (void)spi_poll_reset_complete(WS2812_SPI, spi_reset_all, 1000);

  spi_master_get_default_timing_config(&timing_config);
  timing_config.master_config.clk_src_freq_in_hz = clock_get_frequency(clock_spi1);
  timing_config.master_config.sclk_freq_in_hz    = WS2812_SPI_HZ;
  if (spi_master_timing_init(WS2812_SPI, &timing_config) != status_success)
  {
    cliPrintf("[E_] ws2812Init() timing\n");
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
  ctrl_config.common_config.tx_dma_enable = true;    /* DMA 전송 */
  ctrl_config.common_config.rx_dma_enable = false;
  ctrl_config.common_config.trans_mode    = spi_trans_write_only;

  /*
   * IAP 도 WS2812 를 SPI1+DMA 로 돌린다. 채널이 살아있는 채로 넘어올 수 있으므로
   * 쓰기 전에 정리한다.
   */
  dma_abort_channel(HPM_HDMA, 1u << WS2812_DMA_CH);
  dma_disable_channel(HPM_HDMA, WS2812_DMA_CH);
  (void)dma_check_transfer_status(HPM_HDMA, WS2812_DMA_CH);   /* 남은 플래그 W1C */
  is_busy = false;

  /*
   * 앞뒤 리셋 구간은 항상 low 이고 데이터가 아니다. Encode() 는 데이터 구간만
   * 손대므로 여기서 한 번만 깔아 둔다.
   */
  for (int i = 0; i < WS2812_LEAD_BYTES; i++)
  {
    frame_buf[i] = 0x00;
  }
  for (int i = 0; i < WS2812_RESET_BYTES; i++)
  {
    frame_buf[WS2812_DATA_OFF + WS2812_DATA_LEN + i] = 0x00;
  }

  ws2812Clear();

  is_init = true;

  /*
   * 버퍼만 지우면 소용없다 — WS2812 는 마지막으로 받은 값을 래치하고 있으므로
   * 실제로 밀어내야 꺼진다. IAP 가 켜둔 색이 그대로 남는 것을 막는다.
   */
  ws2812Refresh();

  cliPrintf("[OK] ws2812Init()\n");
  cliPrintf("     ch : %d\n", HW_WS2812_MAX_CH);

#if CLI_USE(HW_WS2812)
  cliAdd("ws2812", cliWs2812);
#endif

  return is_init;
}

void ws2812SetColor(uint16_t ch, uint8_t red, uint8_t green, uint8_t blue)
{
  if (ch >= HW_WS2812_MAX_CH) return;

  /* 원본만 담아 둔다. 비트패턴으로 펼치는 것도 리미터도 Refresh 에서 한다. */
  rgb_buf[ch][0] = red;
  rgb_buf[ch][1] = green;
  rgb_buf[ch][2] = blue;
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
}

void ws2812SetLimit(uint16_t max_ma)
{
  limit_ma = max_ma;
}

uint16_t ws2812GetLimit(void)
{
  return limit_ma;
}

void ws2812SetPrio(ws2812_prio_t new_prio)
{
  prio = new_prio;
}

ws2812_prio_t ws2812GetPrio(void)
{
  return prio;
}

uint32_t ws2812GetLimitHit(void)
{
  return limit_hit;
}

void ws2812ClearLimitHit(void)
{
  limit_hit = 0;
}

/*
 * limited = true 면 리미터를 먹인 뒤의 값이다. 배율을 무리 합계에 한 번 곱한 것이라
 * 채널마다 잘라내는 실제보다 근소하게 높게 나온다 — 즉 안전한 쪽으로 틀린다.
 *
 * grp 가 WS2812_GRP_MAX 면 전체, 아니면 그 무리만.
 */
uint16_t ws2812GetFrameMa(bool limited, uint8_t grp)
{
  uint32_t sum[WS2812_GRP_MAX];
  uint32_t scale[WS2812_GRP_MAX];
  uint32_t ua[WS2812_GRP_MAX];

  ws2812SumUnits(sum);

  if (limited)
  {
    ws2812Scales(sum, scale);
    sum[0] = (sum[0] * scale[0]) >> 16;   /* 49725 x 65536 = 3.3e9, uint32 안 */
    sum[1] = (sum[1] * scale[1]) >> 16;
  }

  ua[0] = ws2812GrpUa(WS2812_GRP_KEY,   sum[0]);
  ua[1] = ws2812GrpUa(WS2812_GRP_UNDER, sum[1]);

  if (grp < WS2812_GRP_MAX)
  {
    /* 고정분은 무리로 나눌 수 없다. 개별 조회에는 가변분만 준다. */
    return (uint16_t)(ua[grp] / 1000);
  }

  return (uint16_t)(WS2812_IDLE_MA + (ua[0] + ua[1]) / 1000);
}

/*
 * 주의: SDK 의 dma_check_transfer_status() 는 완료 플래그가 하나도 없으면
 * ONGOING 을 돌려준다. 즉 "한 번도 안 쓴 채널" 도 진행 중으로 보인다.
 * 그래서 유휴 판정에 그대로 쓰면 안 되고, 시작 시점을 우리가 기억해야 한다.
 */
bool ws2812IsBusy(void)
{
  uint32_t status;

  if (is_init != true) return false;
  if (is_busy != true) return false;

  status = dma_check_transfer_status(HPM_HDMA, WS2812_DMA_CH);
  if (status & (DMA_CHANNEL_STATUS_TC | DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT))
  {
    is_busy = false;
  }

  return is_busy;
}

bool ws2812Refresh(void)
{
  dma_channel_config_t ch_config = {0};

  if (is_init != true)   return false;
  if (ws2812IsBusy())    return false;   /* 이전 프레임이 아직 나가는 중 */

  /* 색 -> 비트패턴. 전류 리미터가 여기서 걸린다. */
  ws2812Encode();

  /*
   * DMA 는 캐시를 거치지 않는다. 버퍼를 메모리까지 밀어낸다.
   * 주소는 정렬돼 있고(위 선언), 길이도 캐시라인 배수로 올려 마지막 줄까지 덮는다.
   */
  l1c_dc_writeback((uint32_t)frame_buf, HPM_L1C_CACHELINE_ALIGN_UP(WS2812_BUF_LEN));

  if (spi_setup_dma_transfer(WS2812_SPI, &ctrl_config,
                             NULL, NULL, WS2812_BUF_LEN, 0) != status_success)
  {
    return false;
  }

  dma_default_channel_config(HPM_HDMA, &ch_config);
  ch_config.src_addr      = core_local_mem_to_sys_address(0, (uint32_t)frame_buf);
  ch_config.dst_addr      = (uint32_t)&WS2812_SPI->DATA;
  ch_config.src_width     = DMA_TRANSFER_WIDTH_BYTE;
  ch_config.dst_width     = DMA_TRANSFER_WIDTH_BYTE;
  ch_config.src_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
  ch_config.dst_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;   /* SPI DATA 는 고정 주소 */
  ch_config.src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;
  ch_config.dst_mode      = DMA_HANDSHAKE_MODE_HANDSHAKE; /* SPI 가 요청할 때만 */
  ch_config.size_in_byte  = WS2812_BUF_LEN;

  dmamux_config(HPM_DMAMUX,
                DMA_SOC_CHN_TO_DMAMUX_CHN(HPM_HDMA, WS2812_DMA_CH),
                HPM_DMA_SRC_SPI1_TX, true);

  if (dma_setup_channel(HPM_HDMA, WS2812_DMA_CH, &ch_config, true) != status_success)
  {
    return false;
  }

  is_busy = true;
  return true;
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
    const char *prio_str[] = {"shared", "key-first", "under-first"};
    uint16_t raw = ws2812GetFrameMa(false, WS2812_GRP_MAX);
    uint16_t lim = ws2812GetFrameMa(true,  WS2812_GRP_MAX);

    cliPrintf("ws2812 ch    : %d  (키 %d + 언더글로우 %d)\n",
              HW_WS2812_MAX_CH, WS2812_KEY_CNT, HW_WS2812_MAX_CH - WS2812_KEY_CNT);
    cliPrintf("frame bytes  : %d\n", WS2812_BUF_LEN);
    cliPrintf("spi clk      : %d Hz\n", (int)clock_get_frequency(clock_spi1));
    cliPrintf("busy         : %d\n", ws2812IsBusy() ? 1 : 0);
    cliPrintf("limit        : %d mA  (idle %d mA)\n", ws2812GetLimit(), WS2812_IDLE_MA);
    cliPrintf("ch full      : 위 %d.%02d mA / 언더 %d.%02d mA  (채널 255 일 때)\n",
              WS2812_CH_FULL_UA_KEY / 1000,   (WS2812_CH_FULL_UA_KEY % 1000) / 10,
              WS2812_CH_FULL_UA_UNDER / 1000, (WS2812_CH_FULL_UA_UNDER % 1000) / 10);
    cliPrintf("prio         : %s\n", prio_str[ws2812GetPrio()]);
    cliPrintf("frame current: %d mA%s\n", lim, (lim < raw) ? "   <- 제한됨" : "");
    cliPrintf("  키          : %d mA\n", ws2812GetFrameMa(true, WS2812_GRP_KEY));
    cliPrintf("  언더글로우    : %d mA\n", ws2812GetFrameMa(true, WS2812_GRP_UNDER));
    if (lim < raw)
    {
      cliPrintf("  제한 없었다면 : %d mA\n", raw);
    }
    /* 평상시 0 이어야 한다. 0 이 아니면 밝기 상한이 예산보다 높다는 뜻이다. */
    cliPrintf("limit hit    : %u 프레임\n", (unsigned)ws2812GetLimitHit());
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "limit"))
  {
    uint16_t ma = (uint16_t)args->getData(1);

    ws2812SetLimit(ma);
    ws2812Refresh();          /* 새 상한으로 지금 프레임을 다시 내보낸다 */
    cliPrintf("limit %d mA -> frame %d mA\n",
              ma, ws2812GetFrameMa(true, WS2812_GRP_MAX));
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "prio"))
  {
    if      (args->isStr(1, "shared")) ws2812SetPrio(WS2812_PRIO_SHARED);
    else if (args->isStr(1, "key"))    ws2812SetPrio(WS2812_PRIO_KEY_FIRST);
    else if (args->isStr(1, "under"))  ws2812SetPrio(WS2812_PRIO_UNDER_FIRST);
    else
    {
      cliPrintf("prio shared|key|under\n");
      return;
    }

    ws2812Refresh();
    cliPrintf("prio -> 키 %d mA, 언더글로우 %d mA\n",
              ws2812GetFrameMa(true, WS2812_GRP_KEY),
              ws2812GetFrameMa(true, WS2812_GRP_UNDER));
    ret = true;
  }

  /*
   * 체인 순서를 눈으로 훑는다. 한 개씩 켜며 번호를 찍으므로 몇 번이 위를 향하고
   * 몇 번이 아래를 향하는지 한 바퀴로 갈린다.
   *
   * ★ 이게 필요한 이유 — 무리 경계를 개수로 짐작했다가 틀렸다. 63~82 를 켰더니
   *   그중 둘이 위에서 켜졌다. LED 인덱스와 물리 위치의 대응은 재는 수밖에 없다.
   */
  if ((args->argc == 1 || args->argc == 2) && args->isStr(0, "walk"))
  {
    uint32_t ms = (args->argc == 2) ? (uint32_t)args->getData(1) : 400;

    cliPrintf("한 개씩 켠다. 위/아래 어느 쪽인지 보면서 번호를 적는다.\n");
    cliPrintf("(아무 키나 누르면 종료)\n");

    for (uint16_t i = 0; i < HW_WS2812_MAX_CH && cliKeepLoop(); i++)
    {
      ws2812Clear();
      ws2812SetColor(i, WS2812_TEST_LEVEL, WS2812_TEST_LEVEL, WS2812_TEST_LEVEL);
      ws2812Refresh();
      cliPrintf("  %d\n", i);
      delay(ms);
    }
    ws2812Clear();
    ws2812Refresh();
    ret = true;
  }

  /*
   * 구간을 한 번에 켠다. 경계를 이분법으로 좁힐 때 쓴다.
   *
   * ★ 지우지 않는다. `set` 과 같은 뜻이어야 두 구간을 다른 색으로 같이 켤 수 있다.
   *   처음에는 앞서 켠 것을 지우게 짰다가, 정작 경계를 가리려고 두 구간을 같이
   *   켜려는 순간 못 쓰게 됐다. 지우려면 `ws2812 off` 를 먼저 부른다.
   */
  if (args->argc == 6 && args->isStr(0, "range"))
  {
    uint16_t from = (uint16_t)args->getData(1);
    uint16_t to   = (uint16_t)args->getData(2);
    uint8_t  r    = (uint8_t)args->getData(3);
    uint8_t  g    = (uint8_t)args->getData(4);
    uint8_t  b    = (uint8_t)args->getData(5);

    for (uint16_t i = from; i <= to && i < HW_WS2812_MAX_CH; i++)
    {
      ws2812SetColor(i, r, g, b);
    }
    cliPrintf("%d~%d : %s\n", from, to, ws2812Refresh() ? "OK" : "NG");
    ret = true;
  }

  /*
   * 무리 경계 확인. 앞 63개가 정말 키 LED 인지는 눈으로만 갈린다 —
   * 위를 향한 것들이 빨강, 아래를 향한 것들이 파랑으로 보여야 맞다.
   */
  if (args->argc == 1 && args->isStr(0, "group"))
  {
    for (uint16_t i = 0; i < HW_WS2812_MAX_CH; i++)
    {
      if (WS2812_GRP_OF(i) == WS2812_GRP_KEY)
        ws2812SetColor(i, WS2812_TEST_LEVEL, 0, 0);   /* 키 = 빨강 */
      else
        ws2812SetColor(i, 0, 0, WS2812_TEST_LEVEL);   /* 언더글로우 = 파랑 */
    }
    ws2812Refresh();
    cliPrintf("키 0~%d = 빨강, 언더글로우 %d~%d = 파랑\n",
              WS2812_KEY_CNT - 1, WS2812_KEY_CNT, HW_WS2812_MAX_CH - 1);
    cliPrintf("위를 향한 것이 빨강이면 경계가 맞다\n");
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
    cliPrintf("ws2812 limit <mA>  - 프레임 전류 상한 (저장 안 됨)\n");
    cliPrintf("ws2812 prio shared|key|under\n");
    cliPrintf("ws2812 range <from> <to> <r> <g> <b>   - 안 지우고 덧칠한다\n");
    cliPrintf("ws2812 walk [ms]   - 하나씩 켜며 번호를 찍는다 (체인 순서 확인)\n");
    cliPrintf("ws2812 group       - 무리 경계 확인 (키=빨강, 언더글로우=파랑)\n");
    cliPrintf("ws2812 test        - 흐르는 점\n");
    cliPrintf("ws2812 rainbow     - 무지개 회전\n");
  }
}
#endif

#endif /* _USE_HW_WS2812 */
