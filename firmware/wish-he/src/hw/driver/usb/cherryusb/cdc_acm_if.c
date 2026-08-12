/*
 * cdc_acm_if.c  —  CherryUSB CDC-ACM 디바이스 글루 (HPM5361)
 *
 * CherryUSB 의 usbd_ep_start_read/write 는 one-shot 이고(큐잉 없음) 완료 콜백은 ISR 컨텍스트다.
 * 그 위에 프로젝트 공용 qbuffer 링버퍼를 얹어 uart 채널처럼 쓸 수 있게 한다.
 *
 *   RX : bulk_out 콜백(ISR) -> qbufferWrite(q_rx) -> 여유가 MPS 이상이면 즉시 재무장.
 *        여유가 모자라면 재무장하지 않는다 -> 하드웨어가 NAK -> 호스트가 대기(흐름제어).
 *        앱이 읽어서 여유가 생기면 cdcIfAvailable/Read 안에서 다시 무장한다.
 *
 *   TX : q_tx 에 쌓고 cdcTxKick() 이 한 청크를 usbd_ep_start_write.
 *        완료 콜백(ISR)에서 다음 청크를 이어보낸다. 길이가 MPS 배수면 ZLP 를 먼저 보낸다.
 */

#include "cdc_acm_if.h"


#ifdef _USE_HW_CDC
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "qbuffer.h"
#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "hpm_interrupt.h"


#define CDC_IN_EP           0x81
#define CDC_OUT_EP          0x01
#define CDC_INT_EP          0x83

#define USB_CONFIG_SIZE     (9 + CDC_ACM_DESCRIPTOR_LEN)

#define CDC_RX_BUF_SIZE     4096
#define CDC_TX_BUF_SIZE     4096
#define CDC_TX_CHUNK_SIZE   2048

#define CDC_BUSID           0


/* DMA 가 접근하는 스테이징 버퍼. .noncacheable.non_init 은 NOLOAD 라 0 초기화되지 않는다. */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t rx_stage[USB_BULK_EP_MPS_HS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_stage[CDC_TX_CHUNK_SIZE];

static qbuffer_t q_rx;
static qbuffer_t q_tx;
static uint8_t   q_rx_buf[CDC_RX_BUF_SIZE];
static uint8_t   q_tx_buf[CDC_TX_BUF_SIZE];

static volatile bool     is_configured = false;
static volatile bool     is_dtr        = false;
static volatile bool     is_rx_full    = false;
static volatile bool     is_tx_busy    = false;
static volatile uint32_t cdc_baud      = 115200;
static volatile uint8_t  cdc_type      = 0;
static uint16_t          cdc_mps       = USB_BULK_EP_MPS_HS;


static void cdcRxArm(void);
static void cdcRxRearm(void);
static void cdcTxKick(void);




/*---------------------------------------------------------------------------
 *  기술자
 *---------------------------------------------------------------------------*/
static const uint8_t device_descriptor[] = {
  USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
  USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

static const uint8_t config_descriptor_fs[] = {
  USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

static const uint8_t device_quality_descriptor[] = {
  USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

/* other-speed 는 "지금 속도의 반대쪽"을 기술한다. HS 로 동작 중이면 FS 규격을 담는 게 맞다. */
static const uint8_t other_speed_config_descriptor_hs[] = {
  USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

static const uint8_t other_speed_config_descriptor_fs[] = {
  USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

static const char *string_descriptors[] = {
  (const char[]){ 0x09, 0x04 },   /* LangID : en-US */
  "HPMicro",
  _DEF_BOARD_NAME,
  "0001",
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
  (void)speed;
  return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
  if (speed == USB_SPEED_HIGH) return config_descriptor_hs;
  if (speed == USB_SPEED_FULL) return config_descriptor_fs;
  return NULL;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
  (void)speed;
  return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
  if (speed == USB_SPEED_HIGH) return other_speed_config_descriptor_hs;
  if (speed == USB_SPEED_FULL) return other_speed_config_descriptor_fs;
  return NULL;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
  (void)speed;
  if (index >= (sizeof(string_descriptors)/sizeof(string_descriptors[0]))) return NULL;
  return string_descriptors[index];
}

static const struct usb_descriptor cdc_descriptor =
{
  .device_descriptor_callback         = device_descriptor_callback,
  .config_descriptor_callback         = config_descriptor_callback,
  .device_quality_descriptor_callback = device_quality_descriptor_callback,
  .other_speed_descriptor_callback    = other_speed_config_descriptor_callback,
  .string_descriptor_callback         = string_descriptor_callback,
};




/*---------------------------------------------------------------------------
 *  RX
 *---------------------------------------------------------------------------*/

/* 인터럽트가 막힌 상태에서 호출해야 한다. */
void cdcRxArm(void)
{
  uint32_t room;

  room = (q_rx.len - qbufferAvailable(&q_rx)) - 1;

  if (room >= cdc_mps)
  {
    is_rx_full = false;
    usbd_ep_start_read(CDC_BUSID, CDC_OUT_EP, rx_stage, cdc_mps);
  }
  else
  {
    /* 재무장하지 않는다 -> 하드웨어가 NAK -> 호스트 대기 */
    is_rx_full = true;
  }
}

void cdcRxRearm(void)
{
  uint32_t mask;

  if (is_rx_full == false)    return;
  if (is_configured == false) return;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  if (is_rx_full)
  {
    cdcRxArm();
  }
  restore_global_irq(mask);
}

void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;

  qbufferWrite(&q_rx, rx_stage, nbytes);

  cdcRxArm();
}




/*---------------------------------------------------------------------------
 *  TX
 *---------------------------------------------------------------------------*/

/* 인터럽트가 막힌 상태에서 호출해야 한다. */
void cdcTxKick(void)
{
  uint32_t len;

  if (is_tx_busy)             return;
  if (is_configured == false) return;

  len = qbufferAvailable(&q_tx);
  if (len == 0) return;

  if (len > CDC_TX_CHUNK_SIZE) len = CDC_TX_CHUNK_SIZE;

  qbufferRead(&q_tx, tx_stage, len);

  is_tx_busy = true;
  usbd_ep_start_write(CDC_BUSID, CDC_IN_EP, tx_stage, len);
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  if (nbytes > 0 && (nbytes % usbd_get_ep_mps(busid, ep)) == 0)
  {
    /* 길이가 MPS 배수면 ZLP 를 보내야 호스트가 전송 종료를 인지한다 */
    usbd_ep_start_write(busid, ep, NULL, 0);
  }
  else
  {
    is_tx_busy = false;
    cdcTxKick();
  }
}




/*---------------------------------------------------------------------------
 *  CDC 제어 콜백 (weak 오버라이드)
 *---------------------------------------------------------------------------*/
void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
  (void)busid;
  (void)intf;

  cdc_baud = line_coding->dwDTERate;
  cdc_type = (cdc_baud == 115200) ? 1 : 0;
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
  (void)busid;
  (void)intf;

  line_coding->dwDTERate   = cdc_baud;
  line_coding->bDataBits   = 8;
  line_coding->bParityType = 0;
  line_coding->bCharFormat = 0;
}

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
  (void)busid;
  (void)intf;

  is_dtr = dtr;
}




/*---------------------------------------------------------------------------
 *  스택 이벤트
 *---------------------------------------------------------------------------*/
void cdcIfEventHandler(uint8_t busid, uint8_t event)
{
  switch (event)
  {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      is_configured = false;
      is_tx_busy    = false;
      is_rx_full    = false;
      is_dtr        = false;
      break;

    case USBD_EVENT_CONFIGURED:
      cdc_mps       = usbd_get_ep_mps(busid, CDC_OUT_EP);
      is_configured = true;
      is_tx_busy    = false;
      qbufferFlush(&q_rx);
      qbufferFlush(&q_tx);
      cdcRxArm();                 /* 최초 수신 무장 */
      break;

    default:
      break;
  }
}




/*---------------------------------------------------------------------------
 *  공개 API
 *---------------------------------------------------------------------------*/
static struct usbd_interface intf0;
static struct usbd_interface intf1;

static struct usbd_endpoint cdc_out_ep = { .ep_addr = CDC_OUT_EP, .ep_cb = usbd_cdc_acm_bulk_out };
static struct usbd_endpoint cdc_in_ep  = { .ep_addr = CDC_IN_EP,  .ep_cb = usbd_cdc_acm_bulk_in  };


bool cdcIfInit(void)
{
  qbufferCreate(&q_rx, q_rx_buf, CDC_RX_BUF_SIZE);
  qbufferCreate(&q_tx, q_tx_buf, CDC_TX_BUF_SIZE);

  is_configured = false;
  is_dtr        = false;
  is_rx_full    = false;
  is_tx_busy    = false;

  usbd_desc_register(CDC_BUSID, &cdc_descriptor);
  usbd_add_interface(CDC_BUSID, usbd_cdc_acm_init_intf(CDC_BUSID, &intf0));
  usbd_add_interface(CDC_BUSID, usbd_cdc_acm_init_intf(CDC_BUSID, &intf1));
  usbd_add_endpoint(CDC_BUSID, &cdc_out_ep);
  usbd_add_endpoint(CDC_BUSID, &cdc_in_ep);

  return true;
}

bool cdcIfIsConfigured(void)
{
  return is_configured;
}

bool cdcIfIsConnected(void)
{
  return (is_configured && is_dtr);
}

uint32_t cdcIfAvailable(void)
{
  cdcRxRearm();
  return qbufferAvailable(&q_rx);
}

uint8_t cdcIfRead(void)
{
  uint8_t ret = 0;

  if (qbufferAvailable(&q_rx) > 0)
  {
    qbufferRead(&q_rx, &ret, 1);
  }
  cdcRxRearm();

  return ret;
}

uint32_t cdcIfReadBuf(uint8_t *p_data, uint32_t length)
{
  uint32_t avail;

  avail = qbufferAvailable(&q_rx);
  if (avail > length) avail = length;

  if (avail > 0)
  {
    qbufferRead(&q_rx, p_data, avail);
  }
  cdcRxRearm();

  return avail;
}

uint32_t cdcIfWrite(uint8_t *p_data, uint32_t length)
{
  uint32_t pre_time;
  uint32_t sent_len = 0;
  uint32_t room;
  uint32_t tx_len;
  uint32_t mask;


  if (cdcIfIsConnected() == false) return 0;

  pre_time = millis();

  while (sent_len < length)
  {
    room   = (q_tx.len - qbufferAvailable(&q_tx)) - 1;
    tx_len = length - sent_len;
    if (tx_len > room) tx_len = room;

    if (tx_len > 0)
    {
      qbufferWrite(&q_tx, &p_data[sent_len], tx_len);
      sent_len += tx_len;
    }

    mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    cdcTxKick();
    restore_global_irq(mask);

    if (cdcIfIsConnected() == false) break;

    if (millis() - pre_time >= 100) break;   /* 호스트가 안 빼가면 100ms 에서 포기 */
  }

  return sent_len;
}

uint32_t cdcIfGetBaud(void)
{
  return cdc_baud;
}

uint8_t cdcIfGetType(void)
{
  return cdc_type;
}

#endif
#endif
