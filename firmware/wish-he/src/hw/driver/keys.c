/*
 * keys.c  —  홀이펙트 키 스캔 (ADC 시퀀스 + 아날로그 MUX)
 *
 * 64키를 8채널 x 8스텝으로 읽는다. ADC0/ADC1 이 각각 4채널을 동시에 훑고,
 * 3비트 MUX 주소(PY00~PY02)를 8번 돌려 스텝을 바꾼다.
 *
 *   for step in 0..7:
 *       DO[PY].VALUE = mux_addr[step]        주소 쓰기
 *       세틀링 대기
 *       ADC0 / ADC1 시퀀스 SW 트리거
 *       완료 대기 (ISR 이 세우는 플래그를 스핀)
 *       DO[PY].VALUE = mux_addr[step + 1]    ★ 다음 주소를 결과 읽기 "전에"
 *       결과 버퍼에서 8채널 읽기
 *
 * 마지막 두 줄의 순서가 핵심이다. 다음 스텝의 세틀링이 이번 스텝의 결과 처리와
 * 겹쳐서 공짜가 된다. 그래서 주소 테이블에 되돌이용 원소가 하나 더 붙는다.
 *
 * ★ 시퀀스 DMA 는 HDMA 채널을 쓰지 않는다. ADC 가 자체 AHB 라이터로 직접 메모리에
 *   쓰므로 hw_def.h 의 DMA 채널 배분과 무관하다.
 */

#include "keys.h"


#ifdef _USE_HW_KEYS

#include "cli.h"
#include "hpm_adc16_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_interrupt.h"
#include "hpm_soc_feature.h"


#define KEYS_SEQ_LEN        KEYS_CH_MAX_PER_ADC   /* ADC 하나가 훑는 채널 수 */

/*
 * MUX 주소 순서 — 그레이 코드.
 *
 * 스텝당 1비트만 바뀌어 인접 채널 크로스토크가 적다. 9번째 원소는 마지막 스텝에서
 * "다음 주소"를 미리 쓰기 위한 되돌이값이다 (0번 스텝 주소와 같아야 한다).
 */
static const uint8_t mux_addr[KEYS_STEP_MAX + 1] =
{
  6, 7, 5, 4, 0, 1, 3, 2,   6
};

/*
 * ADC 시퀀스 채널.
 *
 * ★ 채널 번호는 패드 번호와 다르다. PB00~PB07 -> ch8~ch15, PB08~PB15 -> ch0~ch7 로
 *   8만큼 돌아가 있다. 순서도 오름차순이 아니므로 이 배열 그대로 써야 한다.
 */
static const uint8_t adc0_seq_ch[KEYS_SEQ_LEN] = { 15, 14, 12,  8 };  /* PB07 PB06 PB04 PB00 */
static const uint8_t adc1_seq_ch[KEYS_SEQ_LEN] = {  0, 13,  9, 10 };  /* PB08 PB05 PB01 PB02 */

/* 아날로그로 잡을 패드. 실제 등록은 8개지만 상용 보드와 같이 16개 전부를 잡는다. */
#define KEYS_ANALOG_PAD_FIRST   IOC_PAD_PB00
#define KEYS_ANALOG_PAD_CNT     16

/* MUX 주소 핀 — PY00~PY03. 주소는 3비트지만 4번째 핀도 출력으로 고정한다. */
#define KEYS_MUX_GPIO_PORT      GPIO_DO_GPIOY
#define KEYS_MUX_PAD_FIRST      IOC_PAD_PY00
#define KEYS_MUX_PIN_CNT        4
#define KEYS_MUX_PIN_MASK       ((1U << KEYS_MUX_PIN_CNT) - 1U)

/* PIOC 의 ALT3 = "패드를 SoC 쪽에 넘긴다". PIOC 를 안 건드리면 IOC 설정이 먹지 않는다. */
#define KEYS_PIOC_SOC_CTRL      IOC_PAD_FUNC_CTL_ALT_SELECT_SET(3)

/*
 * 세틀링 — 주소를 쓴 뒤 아날로그가 안정될 때까지.
 *
 * 홀 센서 자체의 응답은 0.1us 미만이고 MUX 스위칭도 그 수준이라 매우 짧다.
 * 400MHz 에서 16 사이클 = 40ns.
 */
#define KEYS_SETTLE_CYCLES      16

/* 완료를 기다리다 이만큼 돌면 포기한다. 스핀이 영원히 걸리는 것만 막으면 된다. */
#define KEYS_WAIT_LIMIT         100000


/*
 * ADC 시퀀스 DMA 버퍼.
 *
 * ADC 가 직접 쓰므로 캐시에 걸리면 안 된다. .noncacheable.non_init 은 NOLOAD 라
 * 0 초기화되지 않는다 — 첫 스캔 전에는 쓰레기값이 들어 있다고 봐야 한다.
 * 결과는 32비트 워드에 패킹되고 하위 16비트가 값이다 (dma_seq16bit 미사용).
 */
ATTR_PLACE_AT_NONCACHEABLE_BSS __attribute__((aligned(ADC_SOC_DMA_ADDR_ALIGNMENT)))
static uint32_t adc0_buf[KEYS_SEQ_LEN];

ATTR_PLACE_AT_NONCACHEABLE_BSS __attribute__((aligned(ADC_SOC_DMA_ADDR_ALIGNMENT)))
static uint32_t adc1_buf[KEYS_SEQ_LEN];

static volatile bool adc0_done = false;
static volatile bool adc1_done = false;

static uint16_t raw[KEYS_STEP_MAX][KEYS_CH_MAX];
static uint32_t scan_time_us = 0;
static bool     is_init      = false;
static uint32_t timeout_cnt  = 0;

#if CLI_USE(HW_KEYS)
static void cliKeys(cli_args_t *args);
#endif




/*---------------------------------------------------------------------------
 *  ISR — 시퀀스 완료만 알린다
 *---------------------------------------------------------------------------*/
void isr_adc0(void)
{
  uint32_t sts = adc16_get_status_flags(HPM_ADC0);

  if (sts & ADC16_INT_STS_SEQ_CMPT_MASK)
  {
    adc16_clear_status_flags(HPM_ADC0, ADC16_INT_STS_SEQ_CMPT_MASK);
    adc0_done = true;
  }
}
SDK_DECLARE_EXT_ISR_M(IRQn_ADC0, isr_adc0)

void isr_adc1(void)
{
  uint32_t sts = adc16_get_status_flags(HPM_ADC1);

  if (sts & ADC16_INT_STS_SEQ_CMPT_MASK)
  {
    adc16_clear_status_flags(HPM_ADC1, ADC16_INT_STS_SEQ_CMPT_MASK);
    adc1_done = true;
  }
}
SDK_DECLARE_EXT_ISR_M(IRQn_ADC1, isr_adc1)




/*---------------------------------------------------------------------------
 *  초기화
 *---------------------------------------------------------------------------*/
static void keysInitPins(void)
{
  /* 아날로그 입력 — PB00~PB15 */
  for (uint32_t i = 0; i < KEYS_ANALOG_PAD_CNT; i++)
  {
    HPM_IOC->PAD[KEYS_ANALOG_PAD_FIRST + i].FUNC_CTL = IOC_PAD_FUNC_CTL_ANALOG_MASK;
  }

  /*
   * MUX 주소 — PY00~PY03.
   *
   * ★ PY 패드는 IOC 만으로는 연결되지 않는다. PIOC 도 함께 SOC(3) 으로 넘겨야
   *   SoC 쪽 GPIO 가 패드를 잡는다.
   */
  for (uint32_t i = 0; i < KEYS_MUX_PIN_CNT; i++)
  {
    HPM_IOC->PAD[KEYS_MUX_PAD_FIRST + i].FUNC_CTL  = IOC_PY00_FUNC_CTL_GPIO_Y_00;
    HPM_PIOC->PAD[KEYS_MUX_PAD_FIRST + i].FUNC_CTL = KEYS_PIOC_SOC_CTRL;
  }

  gpio_enable_port_output_with_mask(HPM_GPIO0, KEYS_MUX_GPIO_PORT, KEYS_MUX_PIN_MASK);
  gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[0]);
}

static bool keysInitAdc(ADC16_Type *ptr, const uint8_t *seq_ch, uint32_t *buf)
{
  adc16_config_t         cfg;
  adc16_channel_config_t ch_cfg;
  adc16_seq_config_t     seq_cfg;
  adc16_dma_config_t     dma_cfg;

  adc16_get_default_config(&cfg);
  cfg.res         = adc16_res_12_bits;
  cfg.conv_mode   = adc16_conv_mode_sequence;
  cfg.adc_clk_div = 4;
  cfg.sel_sync_ahb = false;
  cfg.adc_ahb_en  = true;          /* 시퀀스 모드는 AHB 라이터가 필요하다 */

  if (adc16_init(ptr, &cfg) != status_success) return false;

  adc16_get_channel_default_config(&ch_cfg);
  ch_cfg.sample_cycle = 5;
  for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
  {
    ch_cfg.ch = seq_ch[i];
    if (adc16_init_channel(ptr, &ch_cfg) != status_success) return false;
  }

  memset(&seq_cfg, 0, sizeof(seq_cfg));
  seq_cfg.seq_len    = KEYS_SEQ_LEN;
  seq_cfg.sw_trig_en = true;       /* 스캔 루프가 직접 트리거한다 */
  seq_cfg.hw_trig_en = false;

  /*
   * ★ 이름에 속으면 안 된다.
   *   CONT_EN    = "트리거 한 번에 큐 끝까지 진행" — 우리가 원하는 것
   *   RESTART_EN = "끝나면 다시 처음부터" (CONT_EN 과 같이 켤 때만 의미)
   *
   *   CONT_EN 을 끄면 트리거 1회에 변환이 딱 1개만 일어나고 멈춘다.
   *   그러면 완료 인터럽트가 영영 오지 않아 스캔이 통째로 타임아웃한다.
   */
  seq_cfg.cont_en    = true;
  seq_cfg.restart_en = false;
  for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
  {
    seq_cfg.queue[i].ch = seq_ch[i];

    /*
     * ★ SEQ_INT_EN 은 시퀀스 전체가 아니라 "이 큐 원소가 끝나면 알려라" 다.
     *   전부 false 로 두면 완료 인터럽트가 영영 오지 않는다 (스캔이 통째로 타임아웃).
     *   마지막 원소에만 켜서 시퀀스 끝에 한 번만 받는다.
     */
    seq_cfg.queue[i].seq_int_en = (i == (KEYS_SEQ_LEN - 1));
  }
  if (adc16_set_seq_config(ptr, &seq_cfg) != status_success) return false;

  memset(&dma_cfg, 0, sizeof(dma_cfg));

  /*
   * ADC 는 시스템 버스로 쓰므로 코어 로컬 주소(ILM/DLM)가 아니라 시스템 주소를 줘야 한다.
   * AXI SRAM 이면 변환이 항등이지만, 버퍼를 옮겼을 때 조용히 깨지는 걸 막는다.
   */
  dma_cfg.start_addr          = (uint32_t *)core_local_mem_to_sys_address(0, (uint32_t)buf);
  dma_cfg.buff_len_in_4bytes  = KEYS_SEQ_LEN;
  dma_cfg.stop_pos            = 0;
  dma_cfg.stop_en             = false;
  if (adc16_init_seq_dma(ptr, &dma_cfg) != status_success) return false;

  adc16_enable_interrupts(ptr, ADC16_INT_STS_SEQ_CMPT_MASK);

  return true;
}

bool keysInit(void)
{
  bool ret = true;


  clock_add_to_group(clock_adc0, 0);
  clock_add_to_group(clock_adc1, 0);
  clock_set_adc_source(clock_adc0, clk_adc_src_ahb0);
  clock_set_adc_source(clock_adc1, clk_adc_src_ahb0);

  keysInitPins();

  if (keysInitAdc(HPM_ADC0, adc0_seq_ch, adc0_buf) == false) ret = false;
  if (keysInitAdc(HPM_ADC1, adc1_seq_ch, adc1_buf) == false) ret = false;

  /*
   * ★ USB 보다 낮게 둔다. 리포트가 스캔에 밀리면 8kHz 폴링을 놓친다.
   *   (USB 는 usbBegin() 에서 2로 잡는다. 값이 작을수록 높은 우선순위가 아니라
   *    HPM PLIC 은 값이 클수록 높으므로 USB=2 > ADC=1 이다.)
   */
  intc_set_irq_priority(IRQn_ADC0, 1);
  intc_set_irq_priority(IRQn_ADC1, 1);
  intc_m_enable_irq(IRQn_ADC0);
  intc_m_enable_irq(IRQn_ADC1);

  is_init = ret;

#if CLI_USE(HW_KEYS)
  cliAdd("keys", cliKeys);
#endif

  logPrintf("[%s] keysInit()\n", ret ? "OK" : "E_");
  if (ret)
  {
    logPrintf("     %d step x %d ch = %d keys\n", KEYS_STEP_MAX, KEYS_CH_MAX, KEYS_MAX);
  }

  return ret;
}




/*---------------------------------------------------------------------------
 *  스캔
 *---------------------------------------------------------------------------*/
static inline void keysSettle(void)
{
  for (uint32_t i = 0; i < KEYS_SETTLE_CYCLES; i++)
  {
    __asm volatile ("nop");
  }
}

/* 완료 플래그를 스핀으로 기다린다. 걸리면 false. */
static inline bool keysWaitDone(void)
{
  uint32_t spin = 0;

  while (!(adc0_done && adc1_done))
  {
    if (++spin > KEYS_WAIT_LIMIT)
    {
      timeout_cnt++;
      return false;
    }
  }
  return true;
}

bool keysUpdate(void)
{
  uint32_t t_begin;
  bool     ret = true;


  if (is_init == false) return false;

  t_begin = micros();

  for (uint32_t step = 0; step < KEYS_STEP_MAX; step++)
  {
    gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[step]);
    keysSettle();

    adc0_done = false;
    adc1_done = false;
    adc16_trigger_seq_by_sw(HPM_ADC0);
    adc16_trigger_seq_by_sw(HPM_ADC1);

    if (keysWaitDone() == false)
    {
      ret = false;
      break;
    }

    /* ★ 다음 주소를 결과 읽기 전에 — 세틀링을 결과 처리와 겹친다 */
    gpio_write_port(HPM_GPIO0, KEYS_MUX_GPIO_PORT, mux_addr[step + 1]);

    for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++)
    {
      raw[step][i]                = (uint16_t)(adc0_buf[i] & 0xFFFF);
      raw[step][KEYS_SEQ_LEN + i] = (uint16_t)(adc1_buf[i] & 0xFFFF);
    }
  }

  scan_time_us = micros() - t_begin;

  return ret;
}

uint16_t keysGetRaw(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  return raw[step][ch];
}

uint32_t keysGetScanTime(void)
{
  return scan_time_us;
}




/*---------------------------------------------------------------------------
 *  CLI
 *---------------------------------------------------------------------------*/
#if CLI_USE(HW_KEYS)

static void keysPrintTable(void)
{
  cliPrintf("      ");
  for (uint32_t ch = 0; ch < KEYS_CH_MAX; ch++)
  {
    cliPrintf(" ch%-2d", (int)ch);
  }
  cliPrintf("\n");

  for (uint32_t step = 0; step < KEYS_STEP_MAX; step++)
  {
    cliPrintf("  s%-2d ", (int)step);
    for (uint32_t ch = 0; ch < KEYS_CH_MAX; ch++)
    {
      cliPrintf(" %4d", (int)raw[step][ch]);
    }
    cliPrintf("\n");
  }
}

void cliKeys(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("keys init   : %d\n", is_init);
    cliPrintf("step x ch   : %d x %d = %d\n", KEYS_STEP_MAX, KEYS_CH_MAX, KEYS_MAX);
    cliPrintf("ADC0 seq ch : ");
    for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) cliPrintf("%d ", adc0_seq_ch[i]);
    cliPrintf("\nADC1 seq ch : ");
    for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) cliPrintf("%d ", adc1_seq_ch[i]);
    cliPrintf("\nmux addr    : ");
    for (uint32_t i = 0; i < KEYS_STEP_MAX; i++) cliPrintf("%d ", mux_addr[i]);
    cliPrintf("\ntimeout     : %d\n", (int)timeout_cnt);
    ret = true;
  }

  /* 한 번만 찍는다. 스크립트로 캡처할 때 쓴다. */
  if (args->argc == 1 && args->isStr(0, "dump"))
  {
    keysUpdate();
    keysPrintTable();
    cliPrintf("  scan : %d us\n", (int)scan_time_us);
    ret = true;
  }

  /* 살아있는 8x8 표. 키를 눌러 값이 어떻게 움직이는지 본다. */
  if (args->argc == 1 && args->isStr(0, "raw"))
  {
    while (cliKeepLoop())
    {
      keysUpdate();
      keysPrintTable();
      cliPrintf("  scan : %d us   \n", (int)scan_time_us);
      cliMoveUp(KEYS_STEP_MAX + 2);
      delay(50);
    }
    cliMoveDown(KEYS_STEP_MAX + 2);
    ret = true;
  }

  /* 스캔 한 바퀴 시간 — 8kHz(125us) 예산과 비교하는 근거 */
  if (args->argc == 1 && args->isStr(0, "time"))
  {
    uint32_t cnt = 0;
    uint32_t t_begin = micros();

    while (micros() - t_begin < 1000000)
    {
      keysUpdate();
      cnt++;
    }

    cliPrintf("scan   : %d 회 / 초\n", (int)cnt);
    cliPrintf("주기   : %d us\n", (int)(1000000 / (cnt ? cnt : 1)));
    cliPrintf("8kHz 예산 125us 대비 : %d %%\n",
              (int)((1000000 / (cnt ? cnt : 1)) * 100 / 125));
    cliPrintf("timeout: %d\n", (int)timeout_cnt);
    ret = true;
  }

  /* 진단 — 시퀀스가 실제로 도는지, 어떤 인터럽트 비트가 서는지 본다 */
  if (args->argc == 1 && args->isStr(0, "adc"))
  {
    struct { const char *name; ADC16_Type *ptr; volatile bool *done; uint32_t *buf; }
    tbl[2] = { {"ADC0", HPM_ADC0, &adc0_done, adc0_buf},
               {"ADC1", HPM_ADC1, &adc1_done, adc1_buf} };

    for (uint32_t n = 0; n < 2; n++)
    {
      uint32_t sts = 0;
      uint32_t spin;

      cliPrintf("%s\n", tbl[n].name);
      cliPrintf("  SEQ_CFG0 : 0x%08X\n", (unsigned)tbl[n].ptr->SEQ_CFG0);
      cliPrintf("  INT_EN   : 0x%08X\n", (unsigned)tbl[n].ptr->INT_EN);
      cliPrintf("  INT_STS  : 0x%08X\n", (unsigned)adc16_get_status_flags(tbl[n].ptr));

      *tbl[n].done = false;
      for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) tbl[n].buf[i] = 0xDEADBEEF;

      adc16_trigger_seq_by_sw(tbl[n].ptr);

      for (spin = 0; spin < 200000; spin++)
      {
        sts = adc16_get_status_flags(tbl[n].ptr);
        if (sts) break;
      }

      cliPrintf("  트리거 후 INT_STS : 0x%08X (spin %d)\n", (unsigned)sts, (int)spin);
      cliPrintf("  ISR done flag     : %d\n", *tbl[n].done);
      cliPrintf("  DMA 버퍼          : ");
      for (uint32_t i = 0; i < KEYS_SEQ_LEN; i++) cliPrintf("0x%08X ", (unsigned)tbl[n].buf[i]);
      cliPrintf("\n");
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("keys info\n");
    cliPrintf("keys adc\n");
    cliPrintf("keys dump\n");
    cliPrintf("keys raw\n");
    cliPrintf("keys time\n");
  }
}
#endif

#endif
