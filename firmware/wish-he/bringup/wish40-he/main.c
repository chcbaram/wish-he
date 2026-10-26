/*
 * wish40-he 브링업 시험 — MCU · MUX · 홀센서 · ADC 가 살아 있는지 본다.
 *
 * 결과는 RAM 의 `probe` 에 쓰고 JTAG 으로 읽는다 (tools/bringup_wish40.py).
 * USB 도 UART 도 안 쓴다 — 시험 대상이 아닌 것에 기대면 안 되는 이유를 못 가린다.
 *
 * ★ ADC 는 시퀀스가 아니라 원샷으로 **16채널 전부**를 두 ADC 로 다 읽는다.
 *
 *   채널 <-> 패드 대응의 출처 둘이 서로 다르다 (wish60-he 문서는 PB08 = ch0,
 *   wish61-he config.h 는 PB08 = IN11). 정하지 않고 다 읽으면 된다 — 회로도가 빈
 *   MUX 입력을 GND 에 묶어 두어서, 채널마다 "몇 번 주소에서 0 이 되나" 가 다르다.
 *
 *     MAG0  주소 5, 7 에서 0       MAG5  주소 3 에서 0
 *     MAG1  0 이 없다              MAG2 · MAG3 · MAG4  주소 6 에서 0
 *
 *   MAG2~4 는 무늬가 같으니 키를 하나씩 눌러 가른다.
 */
#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_adc16_drv.h"
#include "hpm_clock_drv.h"


#define PROBE_MAGIC       0x42303457    /* "W40B" */
#define ADC_CNT           2
#define ADDR_CNT          8
#define CH_CNT            16

#define LED_PIN           31            /* PA31, 1k 직렬로 GND 쪽 — HIGH 가 켬 */
#define MUX_PIN_MASK      0x07          /* PY00~PY02 = MUX_A0~A2, MUX 6개 공통 */

/* PIOC 의 ALT3 = "패드를 SoC 쪽에 넘긴다". 안 하면 IOC 설정이 먹지 않는다 (keys.c 와 같다). */
#define PIOC_SOC_CTRL     IOC_PAD_FUNC_CTL_ALT_SELECT_SET(3)


typedef struct
{
  uint32_t magic;
  uint32_t loop;                 /* 한 바퀴(8주소 x 2ADC x 16채널)마다 +1 — 멈췄는지 본다 */
  uint32_t cpu_hz;
  uint32_t adc_hz;
  uint32_t settle;               /* 주소를 쓴 뒤 nop 수. JTAG 으로 바꿔 쓴다 */
  uint32_t clear;                /* 0 이 아니면 min/max 를 다시 잡는다 */
  uint32_t err;                  /* ADC 초기화 실패 비트 */
  uint32_t timeout;              /* 변환 완료를 못 받은 횟수 */
  uint16_t cur[ADC_CNT][ADDR_CNT][CH_CNT];
  uint16_t min[ADC_CNT][ADDR_CNT][CH_CNT];
  uint16_t max[ADC_CNT][ADDR_CNT][CH_CNT];
} probe_t;

volatile probe_t probe;

static ADC16_Type *const adc_tbl[ADC_CNT] = { HPM_ADC0, HPM_ADC1 };


static void probeInitPins(void);
static bool probeInitAdc(ADC16_Type *ptr);
static void probeScan(void);
static void probeClear(void);




int main(void)
{
  board_init();

  probe.magic  = 0;
  probe.settle = 400;            /* 1us — 원샷이라 넉넉히. 줄여 가며 번짐을 본다 */
  probe.err    = 0;

  clock_add_to_group(clock_adc0, 0);
  clock_add_to_group(clock_adc1, 0);
  clock_set_adc_source(clock_adc0, clk_adc_src_ahb0);
  clock_set_adc_source(clock_adc1, clk_adc_src_ahb0);

  probeInitPins();

  for (uint32_t i = 0; i < ADC_CNT; i++)
  {
    if (probeInitAdc(adc_tbl[i]) == false) probe.err |= (1 << i);
  }

  probe.cpu_hz = clock_get_frequency(clock_cpu0);
  probe.adc_hz = clock_get_frequency(clock_adc0);
  probeClear();
  probe.magic  = PROBE_MAGIC;

  while (1)
  {
    if (probe.clear)
    {
      probeClear();
      probe.clear = 0;
    }
    probeScan();
    probe.loop++;

    /*
     * 512바퀴마다 반전 — 눈으로 "돌고 있다" 를 본다.
     * 실측 한 바퀴 267.7us(3736/s) 라 137ms 마다 바뀌고 한 주기 274ms(3.65Hz)다.
     * 주기가 달라졌으면 스캔 시간이 변한 것이다.
     */
    gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, LED_PIN, (probe.loop >> 9) & 1);
  }
}

static void probeInitPins(void)
{
  /* LED */
  HPM_IOC->PAD[IOC_PAD_PA31].FUNC_CTL = IOC_PA31_FUNC_CTL_GPIO_A_31;
  gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOA, LED_PIN, 1);

  /*
   * 아날로그 입력 — LQFP64 에 나와 있는 PB00 · PB01 · PB08~PB15 만 잡는다.
   * PB02~PB07 은 본딩이 안 된 패드다. 해가 없을 수도 있지만 시험할 이유가 없다.
   */
  HPM_IOC->PAD[IOC_PAD_PB00].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
  HPM_IOC->PAD[IOC_PAD_PB01].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
  for (uint32_t pad = IOC_PAD_PB08; pad <= IOC_PAD_PB15; pad++)
  {
    HPM_IOC->PAD[pad].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
  }

  /* MUX 주소 */
  for (uint32_t i = 0; i < 3; i++)
  {
    HPM_IOC->PAD[IOC_PAD_PY00 + i].FUNC_CTL  = IOC_PY00_FUNC_CTL_GPIO_Y_00;
    HPM_PIOC->PAD[IOC_PAD_PY00 + i].FUNC_CTL = PIOC_SOC_CTRL;
  }
  gpio_enable_port_output_with_mask(HPM_GPIO0, GPIO_DO_GPIOY, MUX_PIN_MASK);
  gpio_write_port(HPM_GPIO0, GPIO_DO_GPIOY, 0);
}

static bool probeInitAdc(ADC16_Type *ptr)
{
  adc16_config_t         cfg;
  adc16_channel_config_t ch_cfg;

  /* 분해능 · 분주 · 샘플 사이클은 본 펌웨어(keys.c) 값 그대로 — 같은 조건에서 본다 */
  adc16_get_default_config(&cfg);
  cfg.res         = adc16_res_12_bits;
  cfg.conv_mode   = adc16_conv_mode_oneshot;
  cfg.adc_clk_div = 3;

  /*
   * ★ 버스를 잡지 않는다 — 완료 비트를 폴링한다.
   *
   *   처음에 false(읽기가 변환 완료까지 버스를 잡는다)로 두고 아래의
   *   adc16_enable_oneshot_mode() 를 빠뜨렸더니, 첫 읽기가 영영 안 돌아왔다.
   *   그 상태에서는 코어가 디버그 정지조차 못 받는다 — JTAG 이 "Unable to halt"
   *   만 내고, 리셋과 정지 요청을 DMI 로 동시에 걸어야 겨우 붙었다.
   *   폴링이면 ADC 가 죽어도 코어는 살아 있고 실패가 timeout 카운터로 남는다.
   */
  cfg.wait_dis    = true;
  if (adc16_init(ptr, &cfg) != status_success) return false;

  adc16_get_channel_default_config(&ch_cfg);
  ch_cfg.sample_cycle = 5;
  for (uint32_t ch = 0; ch < CH_CNT; ch++)
  {
    ch_cfg.ch = ch;
    if (adc16_init_channel(ptr, &ch_cfg) != status_success) return false;
  }

  /* 원샷(버스) 모드를 켠다. conv_mode 만으로는 안 켜진다 — SDK 예제도 따로 부른다 */
  adc16_enable_oneshot_mode(ptr);
  return true;
}

static void probeScan(void)
{
  for (uint32_t addr = 0; addr < ADDR_CNT; addr++)
  {
    gpio_write_port(HPM_GPIO0, GPIO_DO_GPIOY, addr);
    for (uint32_t n = probe.settle; n > 0; n--)
    {
      __asm volatile("nop");
    }

    for (uint32_t a = 0; a < ADC_CNT; a++)
    {
      for (uint32_t ch = 0; ch < CH_CNT; ch++)
      {
        uint16_t v = 0;
        uint32_t n = 0;

        while (adc16_get_oneshot_result(adc_tbl[a], ch, &v) != status_success)
        {
          if (++n > 10000)
          {
            probe.timeout++;
            break;
          }
        }
        probe.cur[a][addr][ch] = v;
        if (v < probe.min[a][addr][ch]) probe.min[a][addr][ch] = v;
        if (v > probe.max[a][addr][ch]) probe.max[a][addr][ch] = v;
      }
    }
  }
}

static void probeClear(void)
{
  for (uint32_t a = 0; a < ADC_CNT; a++)
  {
    for (uint32_t addr = 0; addr < ADDR_CNT; addr++)
    {
      for (uint32_t ch = 0; ch < CH_CNT; ch++)
      {
        probe.min[a][addr][ch] = 0xFFFF;
        probe.max[a][addr][ch] = 0;
      }
    }
  }
}
