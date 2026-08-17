/*
 * uart.c
 *
 * hpm5300evk 콘솔 UART(UART0, PA00=TXD / PA01=RXD) 1채널.
 * 온보드 FT2232 디버거의 VCP 로 그대로 나온다.
 *
 * RX 는 DMAV2 무한루프(en_infiniteloop) 로 qbuffer 에 계속 채운다.
 * out(읽은 위치)은 qbufferRead 가 관리하고, in(쓴 위치)만 DMA 잔여 카운터로 역산해서 넣는다.
 *   qbuffer.in = len - dma_get_remaining_transfer_size()
 * STM32 판에서 GPDMA 순환 링크드리스트 + CBR1 카운터로 하던 것과 같은 방식이다.
 *
 * TX 는 폴링이다. 로그/CLI 출력량 정도에는 충분하다.
 */

#include "uart.h"


#ifdef _USE_HW_UART

#include "cli.h"
#include "qbuffer.h"
#if HW_USE_CDC == 1
#include "cdc.h"
#endif
#include "hpm_uart_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_dmav2_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_misc.h"
#include "hpm_soc_feature.h"


#define UART_RX_BUF_SIZE      512     /* DMA 링버퍼 크기 */


typedef struct
{
  bool           is_open;
  uint32_t       baud;
  uint32_t       rx_cnt;
  uint32_t       tx_cnt;
  UART_Type     *p_uart;
  clock_name_t   clock;

  /* RX DMA */
  DMAV2_Type    *p_dma;
  uint8_t        dma_ch;
  uint8_t        dma_req;
  qbuffer_t      qbuffer;

  uart_driver_t *p_driver;
} uart_tbl_t;


/* DMA 버퍼는 non-cacheable 영역에 4바이트 정렬로 둔다.
   D-Cache 가 켜져 있어서 일반 영역에 두면 DMA 가 쓴 내용을 CPU 가 못 본다. */
ATTR_PLACE_AT_NONCACHEABLE_BSS_WITH_ALIGNMENT(4)
static uint8_t uart_rx_buf[UART_MAX_CH][UART_RX_BUF_SIZE];


static void uartInitPins(uint8_t ch);
static bool uartInitRxDma(uint8_t ch);
static void uartUpdateRxIn(uint8_t ch);


static bool is_init = false;

static uart_tbl_t uart_tbl[UART_MAX_CH];

#if CLI_USE(HW_UART)
static void cliUart(cli_args_t *args);
#endif




bool uartInit(void)
{
  for (int i=0; i<UART_MAX_CH; i++)
  {
    uart_tbl[i].is_open  = false;
    uart_tbl[i].baud     = 115200;
    uart_tbl[i].rx_cnt   = 0;
    uart_tbl[i].tx_cnt   = 0;
    uart_tbl[i].p_driver = NULL;

    qbufferCreate(&uart_tbl[i].qbuffer, uart_rx_buf[i], UART_RX_BUF_SIZE);
  }

  /* ch1 = USB CDC : 하드웨어 핸들이 없는 가상 채널이다 */
  uart_tbl[HW_UART_CH_DEBUG].p_uart  = HPM_UART0;
  uart_tbl[HW_UART_CH_DEBUG].clock   = clock_uart0;
  uart_tbl[HW_UART_CH_DEBUG].p_dma   = HPM_HDMA;
  uart_tbl[HW_UART_CH_DEBUG].dma_ch  = HW_DMA_CH_UART0_RX;
  uart_tbl[HW_UART_CH_DEBUG].dma_req = HPM_DMA_SRC_UART0_RX;

  is_init = true;

#if CLI_USE(HW_UART)
  cliAdd("uart", cliUart);
#endif

  return true;
}

bool uartDeInit(void)
{
  return true;
}

bool uartIsInit(void)
{
  return is_init;
}

bool uartOpen(uint8_t ch, uint32_t baud)
{
  uart_config_t config = {0};


  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    uart_tbl[ch].baud    = baud;
    uart_tbl[ch].is_open = uart_tbl[ch].p_driver->open(baud);
    return uart_tbl[ch].is_open;
  }

#if HW_USE_CDC == 1
  if (ch == HW_UART_CH_USB)
  {
    /* USB 스택은 usbInit() 이 따로 올린다. 여기서는 열림 표시만 한다.
       (스택 초기화 전에 호출돼도 안전해야 한다) */
    uart_tbl[ch].baud    = baud;
    uart_tbl[ch].is_open = true;
    return true;
  }
#endif

  /*
   * 순서 주의 : pinmux -> clock -> uart_init.
   * 클럭을 먼저 켜면 RX 핀의 레벨 변화가 스퓨리어스 바이트로 잡힌다.
   */
  uartInitPins(ch);
  clock_add_to_group(uart_tbl[ch].clock, 0);

  uart_default_config(uart_tbl[ch].p_uart, &config);
  config.src_freq_in_hz = clock_get_frequency(uart_tbl[ch].clock);
  config.baudrate       = baud;
  config.fifo_enable    = true;
  config.dma_enable     = true;
  config.tx_fifo_level  = uart_tx_fifo_trg_not_full;
  config.rx_fifo_level  = uart_rx_fifo_trg_not_empty;

  if (uart_init(uart_tbl[ch].p_uart, &config) != status_success)
  {
    return false;
  }

  if (uartInitRxDma(ch) == false)
  {
    return false;
  }

  uart_tbl[ch].baud    = baud;
  uart_tbl[ch].is_open = true;

  return true;
}

void uartInitPins(uint8_t ch)
{
  switch (ch)
  {
    case HW_UART_CH_DEBUG:
      /* UART0 : PA00 = TXD, PA01 = RXD  (온보드 FT2232 VCP) */
      HPM_IOC->PAD[IOC_PAD_PA00].FUNC_CTL = IOC_PA00_FUNC_CTL_UART0_TXD;
      HPM_IOC->PAD[IOC_PAD_PA01].FUNC_CTL = IOC_PA01_FUNC_CTL_UART0_RXD;
      break;

    default:
      break;
  }
}

/*
 * RX 링버퍼를 DMA 무한루프로 채운다.
 * en_infiniteloop 이면 채널이 끝까지 전송한 뒤 자동으로 처음으로 되돌아간다.
 * 인터럽트는 전부 마스킹하고 폴링으로 잔여량만 읽는다.
 */
bool uartInitRxDma(uint8_t ch)
{
  dma_handshake_config_t cfg = {0};
  uint32_t dmamux_ch;


  clock_add_to_group(clock_hdma, 0);

  dmamux_ch = DMA_SOC_CHN_TO_DMAMUX_CHN(uart_tbl[ch].p_dma, uart_tbl[ch].dma_ch);
  dmamux_config(HPM_DMAMUX, dmamux_ch, uart_tbl[ch].dma_req, true);

  cfg.ch_index        = uart_tbl[ch].dma_ch;
  cfg.src             = (uint32_t)&uart_tbl[ch].p_uart->RBR;
  cfg.src_fixed       = true;
  cfg.dst             = core_local_mem_to_sys_address(BOARD_RUNNING_CORE,
                                                      (uint32_t)uart_tbl[ch].qbuffer.p_buf);
  cfg.dst_fixed       = false;
  cfg.data_width      = DMA_TRANSFER_WIDTH_BYTE;
  cfg.size_in_byte    = UART_RX_BUF_SIZE;
  cfg.en_infiniteloop = true;
  cfg.interrupt_mask  = DMA_INTERRUPT_MASK_ALL;

  if (dma_setup_handshake(uart_tbl[ch].p_dma, &cfg, true) != status_success)
  {
    return false;
  }

  qbufferFlush(&uart_tbl[ch].qbuffer);

  return true;
}

/*
 * DMA 가 다음에 쓸 위치를 qbuffer.in 에 반영한다.
 * 잔여 전송량으로 역산하는 것 말고는 STM32 판(CBR1 카운터)과 동일하다.
 */
void uartUpdateRxIn(uint8_t ch)
{
  qbuffer_t *p_q = &uart_tbl[ch].qbuffer;
  uint32_t   remain;


  remain = dma_get_remaining_transfer_size(uart_tbl[ch].p_dma, uart_tbl[ch].dma_ch);

  if (remain > p_q->len) remain = p_q->len;

  p_q->in = (p_q->len - remain) % p_q->len;
}

bool uartIsOpen(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  return uart_tbl[ch].is_open;
}

bool uartSetDriver(uint8_t ch, uart_driver_t *p_driver)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].p_driver = p_driver;

  return true;
}

bool uartClose(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  uart_tbl[ch].is_open = false;

  return true;
}

uint32_t uartAvailable(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;
  if (uart_tbl[ch].is_open == false) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->available();
  }

#if HW_USE_CDC == 1
  if (ch == HW_UART_CH_USB) return cdcAvailable();
#endif

  uartUpdateRxIn(ch);

  return qbufferAvailable(&uart_tbl[ch].qbuffer);
}

bool uartFlush(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return false;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->flush();
  }

#if HW_USE_CDC == 1
  if (ch == HW_UART_CH_USB)
  {
    uint32_t pre_time = millis();
    while (uartAvailable(ch) > 0)
    {
      if (millis() - pre_time >= 10) break;
      uartRead(ch);
    }
    return true;
  }
#endif

  uartUpdateRxIn(ch);
  uart_tbl[ch].qbuffer.out = uart_tbl[ch].qbuffer.in;

  return true;
}

uint8_t uartRead(uint8_t ch)
{
  uint8_t ret = 0;


  if (ch >= UART_MAX_CH) return 0;
  if (uart_tbl[ch].is_open == false) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->read();
  }

#if HW_USE_CDC == 1
  if (ch == HW_UART_CH_USB)
  {
    ret = cdcRead();
    uart_tbl[ch].rx_cnt++;
    return ret;
  }
#endif

  if (uartAvailable(ch) == 0) return 0;

  qbufferRead(&uart_tbl[ch].qbuffer, &ret, 1);
  uart_tbl[ch].rx_cnt++;

  return ret;
}

uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length)
{
  uint32_t ret = 0;


  if (ch >= UART_MAX_CH) return 0;
  if (uart_tbl[ch].is_open == false) return 0;

  if (uart_tbl[ch].p_driver != NULL)
  {
    return uart_tbl[ch].p_driver->write(p_data, length);
  }

#if HW_USE_CDC == 1
  if (ch == HW_UART_CH_USB)
  {
    ret = cdcWrite(p_data, length);
    uart_tbl[ch].tx_cnt += ret;
    return ret;
  }
#endif

  for (uint32_t i=0; i<length; i++)
  {
    if (uart_send_byte(uart_tbl[ch].p_uart, p_data[i]) != status_success)
    {
      break;
    }
    ret++;
  }

  uart_tbl[ch].tx_cnt += ret;

  return ret;
}

uint32_t uartPrintf(uint8_t ch, const char *fmt, ...)
{
  char buf[256];
  va_list args;
  int len;
  uint32_t ret;


  va_start(args, fmt);
  len = vsnprintf(buf, 256, fmt, args);
  va_end(args);

  if (len < 0) return 0;

  ret = uartWrite(ch, (uint8_t *)buf, (uint32_t)len);

  return ret;
}

uint32_t uartGetBaud(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

#if HW_USE_CDC == 1
  /* USB 는 호스트가 SET_LINE_CODING 으로 지정한 값이 실제 보레이트다 */
  if (ch == HW_UART_CH_USB) return cdcGetBaud();
#endif

  return uart_tbl[ch].baud;
}

uint32_t uartGetRxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].rx_cnt;
}

uint32_t uartGetTxCnt(uint8_t ch)
{
  if (ch >= UART_MAX_CH) return 0;

  return uart_tbl[ch].tx_cnt;
}


#if CLI_USE(HW_UART)
void cliUart(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    const char *ch_name[UART_MAX_CH] =
    {
      "UART0(FT2232)",
#if HW_UART_MAX_CH >= 2
      "USB (CDC)    ",
#endif
    };

    for (int i=0; i<UART_MAX_CH; i++)
    {
      cliPrintf("ch%d %s : %d bps, open %d, rx %d, tx %d\n",
                i,
                ch_name[i],
                (int)uartGetBaud(i),
                uart_tbl[i].is_open,
                (int)uart_tbl[i].rx_cnt,
                (int)uart_tbl[i].tx_cnt);
    }
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "test"))
  {
    uint8_t ch;

    ch = (uint8_t)args->getData(1);

    if (ch >= UART_MAX_CH)
    {
      cliPrintf("ch %d is over max %d\n", ch, UART_MAX_CH);
      return;
    }

    while (cliKeepLoop())
    {
      if (uartAvailable(ch) > 0)
      {
        uint8_t rx_data;

        rx_data = uartRead(ch);
        cliPrintf("%c", rx_data);
      }
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uart info\n");
    cliPrintf("uart test ch[0~%d]\n", UART_MAX_CH-1);
  }
}
#endif

#endif
