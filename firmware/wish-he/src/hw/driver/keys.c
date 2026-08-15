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
 * 부팅 씨앗값 — 두 단계다. 성격이 달라서 나눠 놓았다.
 *
 *   버리는 스캔 : 전원 인가 직후 ADC 레퍼런스·센서 바이어스가 자리잡는 시간.
 *                 평균에 넣으면 기준값이 오염되므로 그냥 버린다.
 *   평균낼 스캔 : 노이즈 감쇠. 샘플 수의 제곱근에 비례한다 (1024회 = 32배).
 *
 * ★ keysInit() 은 usbInit() 앞에 있다. 여기 쓰는 시간이 그대로 USB 열거 지연이 된다.
 *   38us 스캔 기준으로 1024+128 회면 약 44ms — 체감되지 않는다.
 *   상용 보드는 10000회(약 380ms)를 쓰지만, 우리는 러닝 최대값 추적이 계속 보정하므로
 *   씨앗값의 정밀도에 그만큼 기대지 않는다. 더 올리려면 이 숫자만 키우면 된다.
 */
#define KEYS_CAL_DISCARD        128
#define KEYS_CAL_SAMPLES        1024

/*
 * keys map 의 보고 임계값.
 *
 * 이 명령은 매 프레임 64셀 중 최댓값을 고르므로 사실상 노이즈의 극단을 본다.
 * 실측 노이즈 바닥이 약 300 이라 300 으로 두면 쉬지 않고 찍힌다. 넉넉히 띄운다.
 */
#define KEYS_MAP_REPORT_MIN     1200

/*
 * 판정 임계값 — 실측 기준.
 *
 *   기준값(무압)      약 40000 ~ 46000   (셀 간 편차 약 5800)
 *   풀 스트로크       약 13400 카운트    (누르면 값이 내려간다)
 *   무압 노이즈       500 미만
 *
 * 스트로크의 30% 에서 눌림, 19% 에서 해제. 둘 사이 간격이 히스테리시스다.
 */
#define KEYS_PRESS_LEVEL        4000
#define KEYS_RELEASE_LEVEL      2500

/*
 * 기준값 추적 — 안 눌린 상태가 물리적 극단(자석이 가장 멀다)이므로 러닝 최대값이다.
 * 이 덕에 키를 누른 채 부팅해도 손을 떼는 순간 기준값이 제자리를 찾는다.
 */
/*
 * ★ 드리프트 밴드는 "해제 상태로 볼 수 있는 범위" 여야 한다.
 *
 *   기준값은 러닝 최대값이라 노이즈 꼭대기에 걸린다 -> 평상시 편차가 노이즈만큼(약 300)
 *   남는다. 밴드를 300 으로 두면 그 편차가 창 밖이라 기준값이 영영 안 내려온다.
 *   해제 임계값까지 열어두면 "안 눌린 동안은 계속 보정" 이 되어 자연스럽다.
 */
#define KEYS_DRIFT_BAND         KEYS_RELEASE_LEVEL
#define KEYS_DRIFT_PERIOD       1024    /* 스캔 이만큼마다 기준값을 1 내린다 */

/*
 * 부팅 캘리브레이션 이상치 판정.
 *
 * 기준값이 전체 중앙값보다 이만큼 아래면 "그 키는 눌린 채로 측정됐다"고 본다.
 * 스트로크(13400)와 정상 편차(5800) 사이라 양쪽 모두와 안전한 거리가 있다.
 */
#define KEYS_CAL_OUTLIER        8000

/* 평활 계수. 1/(2^n) 씩 따라간다. 스트로크(13400) 대비 지연은 무시할 수준이다. */
#define KEYS_SMOOTH_SHIFT       2


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

static uint16_t raw[KEYS_STEP_MAX][KEYS_CH_MAX];    /* 평활된 값 — 판정·표시에 쓴다 */
static uint16_t base[KEYS_STEP_MAX][KEYS_CH_MAX];   /* 무압 기준값 (러닝 최대) */
static uint16_t pressed[KEYS_STEP_MAX];             /* 행별 눌림 비트마스크 */
static uint8_t  drift_cnt[KEYS_STEP_MAX][KEYS_CH_MAX];
static bool     is_calibrated = false;
static uint32_t scan_time_us = 0;
static bool     is_init      = false;
static uint32_t timeout_cnt  = 0;
static uint32_t cal_time_ms  = 0;

static void keysCalRejectOutlier(void);
static void keysTrack(uint32_t step);
static inline void keysSmooth(uint32_t step, uint32_t ch, uint32_t packed);

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

  if (ret)
  {
    /* ★ 부팅 때 키를 누르고 있으면 그 값이 기준이 된다. 6편에서 이상치 걸러내기 */
    keysCalibrate();
  }

  logPrintf("[%s] keysInit()\n", ret ? "OK" : "E_");
  if (ret)
  {
    logPrintf("     %d step x %d ch = %d keys, cal %d\n",
              KEYS_STEP_MAX, KEYS_CH_MAX, KEYS_MAX, is_calibrated);
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

/*
 * 1차 IIR 평활.
 *
 * 기준값이 러닝 최대값이라 노이즈 꼭대기를 그대로 붙잡는다. 생값을 그냥 넣으면
 * 기준값이 노이즈만큼 들리고, 그만큼 평상시 편차가 남는다. 근원에서 줄이는 게 낫다.
 *
 * 계수 1/4 이면 노이즈가 절반 이하로 줄고 지연은 스캔 몇 번(38us x 4)뿐이다.
 */
static inline void keysSmooth(uint32_t step, uint32_t ch, uint32_t packed)
{
  int32_t v = (int32_t)(packed & 0xFFFF);
  int32_t p = (int32_t)raw[step][ch];

  raw[step][ch] = (uint16_t)(p + ((v - p) >> KEYS_SMOOTH_SHIFT));
}

/*
 * 기준값 추적 + 눌림 판정.
 *
 * 안 눌린 상태가 물리적 극단(자석이 가장 멀어 값이 가장 크다)이므로 기준값은
 * 러닝 최대값이다. 이 하나로 세 가지가 같이 해결된다.
 *
 *   - 누른 채 부팅  -> 손을 떼는 순간 제 값을 찾는다
 *   - 온도 드리프트 -> 위로 새면 즉시, 아래로 새면 천천히 따라간다
 *   - 개체 편차     -> 셀마다 제 기준을 갖는다
 */
static void keysTrack(uint32_t step)
{
  for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
  {
    uint16_t v = raw[step][c];
    int32_t  d;

    /* 해제 방향으로 벗어나면 그 값이 새 기준이다 — 즉시 */
    if (v > base[step][c])
    {
      base[step][c]      = v;
      drift_cnt[step][c] = 0;
    }

    d = (int32_t)base[step][c] - (int32_t)v;    /* 누를수록 커진다 */

    /*
     * 해제 근처에 머무는 동안만 기준값을 천천히 끌어내린다.
     * 눌려 있는 셀은 d 가 커서 이 구간에 들어오지 않으므로 영향받지 않는다.
     */
    if (d > 0 && d < KEYS_DRIFT_BAND)
    {
      if (++drift_cnt[step][c] >= KEYS_DRIFT_PERIOD)
      {
        drift_cnt[step][c] = 0;
        base[step][c]--;
      }
    }
    else
    {
      drift_cnt[step][c] = 0;
    }

    /* 히스테리시스 — 임계값 부근에서 떨리지 않게 */
    if (pressed[step] & (1U << c))
    {
      if (d < KEYS_RELEASE_LEVEL) pressed[step] &= ~(1U << c);
    }
    else
    {
      if (d > KEYS_PRESS_LEVEL)   pressed[step] |=  (1U << c);
    }
  }
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
      keysSmooth(step, i,                adc0_buf[i]);
      keysSmooth(step, KEYS_SEQ_LEN + i, adc1_buf[i]);
    }

    if (is_calibrated) keysTrack(step);
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

/*
 * 무압 기준값을 잡는다.
 *
 * 채널마다 기준값이 다르다 (자석·센서·기구 공차). 절대값이 아니라 "제 기준값에서
 * 얼마나 벗어났는가"로 판정해야 하므로 부팅 때 한 번 재둔다.
 * 여러 번 평균내서 노이즈를 줄인다.
 */
bool keysCalibrate(void)
{
  uint32_t acc[KEYS_STEP_MAX][KEYS_CH_MAX];
  uint32_t t_begin;


  if (is_init == false) return false;

  t_begin = millis();

  memset(acc, 0, sizeof(acc));

  /* 안정화 — 결과는 버린다 */
  for (uint32_t n = 0; n < KEYS_CAL_DISCARD; n++)
  {
    if (keysUpdate() == false) return false;
  }

  for (uint32_t n = 0; n < KEYS_CAL_SAMPLES; n++)
  {
    if (keysUpdate() == false) return false;

    for (uint32_t s = 0; s < KEYS_STEP_MAX; s++)
    {
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
      {
        acc[s][c] += raw[s][c];
      }
    }
  }

  for (uint32_t s = 0; s < KEYS_STEP_MAX; s++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      base[s][c] = (uint16_t)(acc[s][c] / KEYS_CAL_SAMPLES);
    }
  }

  keysCalRejectOutlier();

  cal_time_ms = millis() - t_begin;

  memset(drift_cnt, 0, sizeof(drift_cnt));
  memset(pressed,   0, sizeof(pressed));
  is_calibrated = true;

  return true;
}

/*
 * 부팅 때 눌려 있던 키를 걸러낸다.
 *
 * 누르면 값이 내려가므로, 기준값이 전체 중앙값보다 크게 낮은 셀은 "눌린 채 측정됐다".
 * 그 셀만 중앙값으로 밀어두면 지금은 눌림으로 판정되고(맞다), 손을 떼는 순간
 * 러닝 최대값 추적이 제 값을 찾아준다.
 *
 * 스트로크(약 13400)가 셀 간 정상 편차(약 5800)의 2배가 넘어서 이 판별이 성립한다.
 */
static void keysCalRejectOutlier(void)
{
  uint16_t sorted[KEYS_MAX];
  uint32_t n = 0;
  uint16_t median;


  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++) sorted[n++] = base[st][c];
  }

  /* 64개뿐이라 삽입정렬로 충분하다 */
  for (uint32_t i = 1; i < n; i++)
  {
    uint16_t v = sorted[i];
    uint32_t j = i;
    while (j > 0 && sorted[j - 1] > v) { sorted[j] = sorted[j - 1]; j--; }
    sorted[j] = v;
  }
  median = sorted[n / 2];

  for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
  {
    for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
    {
      if ((int32_t)median - (int32_t)base[st][c] > KEYS_CAL_OUTLIER)
      {
        logPrintf("[  ] s%d/ch%d 눌린 채 캘리 (%d -> 중앙값 %d)\n",
                  (int)st, (int)c, (int)base[st][c], (int)median);
        base[st][c] = median;
      }
    }
  }
}

/* 기준값 대비 편차. 부호가 어느 쪽으로 움직이는지는 실측으로 정한다. */
int32_t keysGetDelta(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  if (is_calibrated == false)                     return 0;

  return (int32_t)raw[step][ch] - (int32_t)base[step][ch];
}

uint16_t keysGetBase(uint8_t step, uint8_t ch)
{
  if (step >= KEYS_STEP_MAX || ch >= KEYS_CH_MAX) return 0;
  return base[step][ch];
}

bool keysGetPressed(uint16_t row, uint16_t col)
{
  if (row >= KEYS_STEP_MAX || col >= KEYS_CH_MAX) return false;
  return (pressed[row] & (1U << col)) != 0;
}

/*
 * 행 비트마스크를 그대로 준다. QMK 의 matrix_row_t 와 비트 순서가 같아서
 * 상위 계층은 이 보드가 HE 인지 일반 매트릭스인지 몰라도 된다.
 */
uint16_t keysGetRow(uint16_t row)
{
  if (row >= KEYS_STEP_MAX) return 0;
  return pressed[row];
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
    cliPrintf("\ncalibrated  : %d  (%d + %d scan, %d ms)\n",
              is_calibrated, KEYS_CAL_DISCARD, KEYS_CAL_SAMPLES, (int)cal_time_ms);
    cliPrintf("scan        : %d us\n", (int)scan_time_us);
    cliPrintf("timeout     : %d\n", (int)timeout_cnt);
    ret = true;
  }

  /* 눌린 키를 실시간으로 본다 */
  if (args->argc == 0)
  {
    while (cliKeepLoop())
    {
      keysUpdate();
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          cliPrintf(" %s", keysGetPressed(st, c) ? "[#]" : " . ");
        }
        cliPrintf("   0x%02X\n", (int)keysGetRow(st));
      }
      cliMoveUp(KEYS_STEP_MAX);
      delay(30);
    }
    cliMoveDown(KEYS_STEP_MAX);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "base"))
  {
    cliPrintf("기준값을 다시 잡는다 — 키에서 손을 떼고 있을 것\n");
    delay(300);
    if (keysCalibrate())
    {
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" %5d", (int)base[st][c]);
        cliPrintf("\n");
      }
    }
    else
    {
      cliPrintf("[E_] 캘리브레이션 실패\n");
    }
    ret = true;
  }

  /*
   * 키 하나를 누르면 어느 셀이 얼마나 움직이는지 알려준다.
   * 64셀 <-> 실제 키 위치 표를 이걸로 만든다.
   */
  if (args->argc == 1 && args->isStr(0, "map"))
  {
    int32_t  last_d  = 0;
    uint32_t last_st = 0, last_ch = 0;

    cliPrintf("키를 하나씩 눌러본다. 가장 크게 움직인 셀을 표시한다.\n\n");

    while (cliKeepLoop())
    {
      int32_t  best   = 0;
      uint32_t best_s = 0, best_c = 0;

      keysUpdate();

      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++)
        {
          int32_t d = keysGetDelta(st, c);
          int32_t a = (d < 0) ? -d : d;

          if (a > ((best < 0) ? -best : best)) { best = d; best_s = st; best_c = c; }
        }
      }

      /* 값이 크게 바뀐 순간에만 한 줄 남긴다 — 스크롤이 넘치지 않게 */
      if (((best < 0) ? -best : best) > KEYS_MAP_REPORT_MIN &&
          (best_s != last_st || best_c != last_ch ||
           ((best - last_d) > 200) || ((last_d - best) > 200)))
      {
        cliPrintf("  s%d / ch%d   delta %+6d   (raw %5d, base %5d)\n",
                  (int)best_s, (int)best_c, (int)best,
                  (int)raw[best_s][best_c], (int)base[best_s][best_c]);
        last_d = best; last_st = best_s; last_ch = best_c;
      }
      delay(20);
    }
    ret = true;
  }

  /* 편차 표. 눌린 셀이 표에서 바로 보인다. */
  if (args->argc == 1 && args->isStr(0, "watch"))
  {
    while (cliKeepLoop())
    {
      keysUpdate();
      cliPrintf("      ");
      for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" ch%-3d", (int)c);
      cliPrintf("\n");
      for (uint32_t st = 0; st < KEYS_STEP_MAX; st++)
      {
        cliPrintf("  s%-2d ", (int)st);
        for (uint32_t c = 0; c < KEYS_CH_MAX; c++) cliPrintf(" %+5d", (int)keysGetDelta(st, c));
        cliPrintf("\n");
      }
      cliMoveUp(KEYS_STEP_MAX + 1);
      delay(50);
    }
    cliMoveDown(KEYS_STEP_MAX + 1);
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
    cliPrintf("keys           눌린 키 표시\n");
    cliPrintf("keys base\n");
    cliPrintf("keys map\n");
    cliPrintf("keys watch\n");
    cliPrintf("keys dump\n");
    cliPrintf("keys raw\n");
    cliPrintf("keys time\n");
  }
}
#endif

#endif
