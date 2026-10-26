/*
 * wish40-he 부트로더.
 *
 * 부팅마다 앱으로 넘어갈지 정한다. 넘어가는 조건은 **넷 다** 참일 때다.
 *
 *   1. 0x80020000 이 "HPM\n"          — 앱이 있다
 *   2. 0x8001D000 이 0xFFFFFFFF       — 앱이 진입을 요청하지 않았다
 *   3. BOOT_BTN(PA30) 이 안 눌렸다    — 조립 전 복구용
 *   4. Esc 가 안 눌렸다                — 조립 뒤 복구용. 택트 스위치에 손이 안 닿는다
 *
 * ★ 넘어가는 길에서는 PLL 도 USB 도 건드리지 않는다.
 *
 *   앱의 board_init_clock() 은 "CPU 가 24MHz 면 프리셋부터" 를 전제로 PLL 을 잡는다.
 *   부트로더가 PLL 을 400MHz 로 올려 두면 앱은 자기를 먹이는 PLL 을 다시 설정하게
 *   된다. 켜 둔 USB 도 앱에는 재열거 문제를 남긴다 (wish60 에서 인계 정리로 겪었다).
 *   그래서 판정에는 GPIO 와 ADC 원샷만 쓰고, 업데이트 모드에서 빠져나갈 때는 점프하지
 *   않고 **리셋**한다 — 다음 부팅은 이 판정부터 다시 하고 깨끗하게 넘어간다.
 */
#include "board.h"
#include "hpm_gpio_drv.h"
#include "hpm_adc16_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_ppor_drv.h"
#include "boot.h"


#define LED_PIN           31          /* PA31, HIGH 가 켬 */
#define BTN_PIN           30          /* PA30, 누르면 HIGH (10k 풀다운) */
#define LED_TOGGLE_MS     100         /* 한 주기 200ms — 앱 · 시험 이미지와 구분된다 */

/*
 * Esc = MUX 주소 0, ADC ch11 (MAG0 = PB08). 실측 — docs/wish40-he.md
 *
 * ★ 극성을 가정하지 않는다. 이 보드는 누르면 오르지만(+56mT) 스위치를 바꾸면 뒤집힌다.
 *   VCC/2 에서 얼마나 벗어났나만 본다. 휴지 +4.7mT 는 1400, 바닥은 16600 카운트다.
 *
 * ★ 센서가 죽어 레일에 붙은 값은 눌림으로 치지 않는다. 그걸 눌림으로 보면 Esc 센서
 *   하나가 고장 나는 순간 **영영 앱으로 못 간다.** TMAG5253 의 선형 출력은
 *   0.2V ~ VCC−0.2V 라 그 밖은 센서가 아니다.
 */
#define ESC_ADC_CH        11
#define ESC_MUX_ADDR      0
#define ESC_MID           32768
#define ESC_PRESS_DELTA   8192        /* 약 25mT — 휴지(1400)와 바닥(16600)의 가운데쯤 */
#define ESC_VALID_LO      4000        /* 0.2V */
#define ESC_VALID_HI      61500       /* VCC − 0.2V */

#define MUX_PIN_MASK      0x07        /* PY00~PY02 */
#define PIOC_SOC_CTRL     IOC_PAD_FUNC_CTL_ALT_SELECT_SET(3)

#define RESET_DELAY_MS    50          /* 마지막 응답이 호스트로 나갈 시간 */


volatile uint32_t boot_why;           /* JTAG 으로 본다 */
volatile uint16_t boot_esc_value;


static uint32_t bootCheck(void);
static void     bootJumpToApp(void);
static void     bootRun(void);
static bool     btnIsPressed(void);
static bool     escIsPressed(void);
static uint32_t nowMs(void);
static void     delayUs(uint32_t us);
void            bootReset(void);




int main(void)
{
  boot_why = bootCheck();
  if (boot_why == 0)
  {
    bootJumpToApp();
  }
  bootRun();
  return 0;
}

static uint32_t bootCheck(void)
{
  uint32_t why = 0;

  /* ROM 이 넘겨 준 클럭 그대로다(24MHz). 판정에 쓰는 것만 붙인다 */
  clock_add_to_group(clock_gpio, 0);
  clock_add_to_group(clock_mchtmr0, 0);

  if (*(volatile uint32_t *)(BOOT_XIP_BASE + BOOT_APP_OFF) != BOOT_APP_MAGIC)
  {
    why |= BOOT_WHY_NO_APP;
  }
  if (*(volatile uint32_t *)(BOOT_XIP_BASE + BOOT_FLAG_OFF) != 0xFFFFFFFFUL)
  {
    why |= BOOT_WHY_FLAG;
  }
  if (btnIsPressed()) why |= BOOT_WHY_BTN;
  if (escIsPressed()) why |= BOOT_WHY_ESC;

  return why;
}

static void bootJumpToApp(void)
{
  void (*app_entry)(void) = (void (*)(void))(BOOT_XIP_BASE + BOOT_APP_OFF + 4);

  disable_global_irq(CSR_MSTATUS_MIE_MASK);
  write_csr(CSR_MIE, 0);
  fencei();

  app_entry();
  while (1);
}

static void bootRun(void)
{
  uint8_t  rx[64];
  uint8_t  tx[64];
  uint32_t led_ms   = 0;
  uint32_t done_ms  = 0;
  bool     led_on   = false;
  bool     is_reset_pending = false;

  board_init();                       /* 여기서부터 400MHz — 돌아가지 않고 리셋으로 나간다 */

  HPM_IOC->PAD[IOC_PAD_PA31].FUNC_CTL = IOC_PA31_FUNC_CTL_GPIO_A_31;
  gpio_set_pin_output_with_initial(HPM_GPIO0, GPIO_DO_GPIOA, LED_PIN, 1);

  iapInit();
  usbBootInit();

  while (1)
  {
    if (nowMs() - led_ms >= LED_TOGGLE_MS)
    {
      led_ms = nowMs();
      led_on = !led_on;
      gpio_write_pin(HPM_GPIO0, GPIO_DO_GPIOA, LED_PIN, led_on);
    }

    if (usbBootRecv(rx))
    {
      iapProcess(rx, tx);
      usbBootSend(tx);
      if (iapIsDone() && is_reset_pending == false)
      {
        is_reset_pending = true;
        done_ms = nowMs();
      }
    }

    /*
     * ★ "예약됨" 을 시각에 섞지 않는다.
     *
     *   처음에는 `done_ms = nowMs() | 1` 로 0 이 아님을 표시했다. nowMs() 가 짝수면
     *   done_ms 가 지금보다 1 커서 `nowMs() - done_ms` 가 0xFFFFFFFF 가 되고 **즉시**
     *   리셋했다. 종료 응답이 나갈 틈이 없어 도구가 "응답 없음 (cmd 0x83)" 을 냈다 —
     *   절반쯤만. 리셋 너머 AHB SRAM 에 시각을 남겨 재 보니 0.1ms 만에 리셋하고 있었다.
     */
    if (is_reset_pending && nowMs() - done_ms >= RESET_DELAY_MS)
    {
      bootReset();
    }
  }
}

static bool btnIsPressed(void)
{
  uint32_t high = 0;

  HPM_IOC->PAD[IOC_PAD_PA30].FUNC_CTL = IOC_PA30_FUNC_CTL_GPIO_A_30;
  gpio_set_pin_input(HPM_GPIO0, GPIO_DO_GPIOA, BTN_PIN);
  delayUs(10);

  /* wish60 부트로더가 스트랩을 보던 방식 그대로 — 10번 중 7번 */
  for (uint32_t i = 0; i < 10; i++)
  {
    if (gpio_read_pin(HPM_GPIO0, GPIO_DI_GPIOA, BTN_PIN)) high++;
    delayUs(100);
  }
  return high >= 7;
}

static bool escIsPressed(void)
{
  adc16_config_t         cfg;
  adc16_channel_config_t ch_cfg;
  uint32_t               sum = 0;
  uint32_t               cnt = 0;
  uint32_t               adc_hz;
  int32_t                d;

  HPM_IOC->PAD[IOC_PAD_PB08].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
  for (uint32_t i = 0; i < 3; i++)
  {
    HPM_IOC->PAD[IOC_PAD_PY00 + i].FUNC_CTL  = IOC_PY00_FUNC_CTL_GPIO_Y_00;
    HPM_PIOC->PAD[IOC_PAD_PY00 + i].FUNC_CTL = PIOC_SOC_CTRL;
  }
  gpio_enable_port_output_with_mask(HPM_GPIO0, GPIO_DO_GPIOY, MUX_PIN_MASK);
  gpio_write_port(HPM_GPIO0, GPIO_DO_GPIOY, ESC_MUX_ADDR);

  clock_add_to_group(clock_adc0, 0);
  clock_set_adc_source(clock_adc0, clk_adc_src_ahb0);
  adc_hz = clock_get_frequency(clock_adc0);

  /*
   * ★ 버스를 잡지 않는다 (wait_dis = true). 시험 이미지에서 원샷 모드를 안 켠 채
   *   버스를 잡게 했더니 코어가 디버그 정지조차 못 받았다 (docs/wish40-he.md).
   *   부트로더에서 그러면 USB 로도 JTAG 으로도 못 살린다.
   */
  adc16_get_default_config(&cfg);
  cfg.res         = adc16_res_12_bits;
  cfg.conv_mode   = adc16_conv_mode_oneshot;
  cfg.adc_clk_div = (adc_hz + 44999999) / 45000000;   /* 45MHz 아래로 */
  cfg.wait_dis    = true;
  if (cfg.adc_clk_div == 0) cfg.adc_clk_div = 1;
  if (adc16_init(HPM_ADC0, &cfg) != status_success) return false;

  adc16_get_channel_default_config(&ch_cfg);
  ch_cfg.ch           = ESC_ADC_CH;
  ch_cfg.sample_cycle = 20;           /* 한 번 읽는 것이라 넉넉히 */
  if (adc16_init_channel(HPM_ADC0, &ch_cfg) != status_success) return false;
  adc16_enable_oneshot_mode(HPM_ADC0);

  delayUs(100);                       /* MUX 정착 */

  for (uint32_t i = 0; i < 8; i++)
  {
    uint16_t v;

    for (uint32_t n = 0; n < 10000; n++)
    {
      if (adc16_get_oneshot_result(HPM_ADC0, ESC_ADC_CH, &v) == status_success)
      {
        sum += v;
        cnt++;
        break;
      }
    }
  }
  adc16_disable_oneshot_mode(HPM_ADC0);
  if (cnt == 0) return false;

  boot_esc_value = sum / cnt;
  if (boot_esc_value < ESC_VALID_LO || boot_esc_value > ESC_VALID_HI) return false;

  d = (int32_t)boot_esc_value - ESC_MID;
  if (d < 0) d = -d;
  return d > ESC_PRESS_DELTA;
}

static uint32_t nowMs(void)
{
  return (uint32_t)(mchtmr_get_count(HPM_MCHTMR) / 24000);   /* mchtmr = 24MHz */
}

static void delayUs(uint32_t us)
{
  uint64_t end = mchtmr_get_count(HPM_MCHTMR) + (uint64_t)us * 24;

  while (mchtmr_get_count(HPM_MCHTMR) < end);
}

__attribute__((noinline)) void bootReset(void)
{
  ppor_sw_reset(HPM_PPOR, 10);
  while (1);
}
