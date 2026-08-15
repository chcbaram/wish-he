/*
 * hid_if.c  —  raw HID 설정 채널 (CherryUSB, HPM5361)
 *
 * 상용 보드는 웹페이지에서 HID 명령으로 부트로더에 진입시킨다. 그걸 그대로 흉내낸다.
 * CDC 콘솔에 사람이 `reset boot` 를 치지 않아도 호스트 도구가 알아서 넘길 수 있다.
 *
 *   호스트 -> 장치 (OUT, 32 B)      장치 -> 호스트 (IN, 32 B)
 *     [0]    명령                     [0]    명령 에코
 *     [1..]  인자                     [1]    상태 (0 = OK)
 *                                     [2..]  응답
 *
 * ★ 리셋은 콜백(ISR) 안에서 하지 않는다. 응답 IN 전송이 호스트에게 실제로 나가기 전에
 *   리셋해버리면 도구 쪽에서는 그냥 장치가 사라진 것으로 보인다. 그리고 부트 플래그
 *   기록은 ROM API 로 XIP 플래시를 건드리는 일이라 인터럽트 컨텍스트에서 할 일이 아니다.
 *   그래서 콜백은 예약만 하고 hidIfUpdate() 가 메인 루프에서 실행한다.
 */

#include "usb/cherryusb/hid_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"
#include "reset.h"


#define HID_BUSID           0

/* 응답이 호스트로 빠져나갈 시간을 주고 리셋한다. */
#define HID_ACTION_DELAY_MS 50


/* VIA 계열과 같은 벤더 정의 페이지. 호스트는 이 값으로 장치를 찾는다. */
static const uint8_t hid_report_desc[] = {
  0x06, 0x60, 0xFF,         /* Usage Page (Vendor Defined 0xFF60)  */
  0x09, 0x61,               /* Usage (0x61)                        */
  0xA1, 0x01,               /* Collection (Application)            */
  0x09, 0x62,               /*   Usage (0x62)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, HID_EP_MPS,         /*   Report Count (32)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x81, 0x02,               /*   Input (Data, Var, Abs)            */
  0x09, 0x63,               /*   Usage (0x63)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, HID_EP_MPS,         /*   Report Count (32)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x91, 0x02,               /*   Output (Data, Var, Abs)           */
  0xC0                      /* End Collection                      */
};

_Static_assert(sizeof(hid_report_desc) == HID_REPORT_DESC_SIZE,
               "HID_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");


/* DMA 가 접근하는 스테이징 버퍼 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t rx_report[HID_EP_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[HID_EP_MPS];

enum
{
  ACTION_NONE = 0,
  ACTION_BOOT,
  ACTION_RESET,
};

static volatile bool     is_configured  = false;
static volatile bool     is_tx_busy     = false;
static volatile uint8_t  pending_action = ACTION_NONE;
static volatile uint32_t action_time    = 0;
static volatile uint32_t rx_count       = 0;

static struct usbd_interface hid_intf;




/*---------------------------------------------------------------------------
 *  명령 처리
 *---------------------------------------------------------------------------*/

/* ISR 컨텍스트. 플래시나 긴 작업은 여기서 하지 않는다. */
static void hidCmdHandler(const uint8_t *p_rx, uint8_t *p_tx)
{
  uint8_t cmd = p_rx[0];

  memset(p_tx, 0, HID_EP_MPS);
  p_tx[0] = cmd;
  p_tx[1] = HID_RESP_OK;

  switch (cmd)
  {
    case HID_CMD_INFO:
    {
      const char *p_name = _DEF_BOARD_NAME;
      uint32_t    i;

      for (i = 0; i < (HID_EP_MPS - 3) && p_name[i] != 0; i++)
      {
        p_tx[2 + i] = (uint8_t)p_name[i];
      }
      break;
    }

    case HID_CMD_BOOT:
      pending_action = ACTION_BOOT;
      action_time    = millis();
      break;

    case HID_CMD_RESET:
      pending_action = ACTION_RESET;
      action_time    = millis();
      break;

    default:
      p_tx[1] = HID_RESP_UNKNOWN_CMD;
      break;
  }
}

/* 메인 루프에서 부른다. 예약된 리셋을 실제로 수행한다. */
void hidIfUpdate(void)
{
  if (pending_action == ACTION_NONE) return;

  if (millis() - action_time < HID_ACTION_DELAY_MS) return;

  switch (pending_action)
  {
    case ACTION_BOOT:
      logPrintf("[  ] HID 부트로더 진입 요청\n");
      pending_action = ACTION_NONE;
      resetToBoot();              /* 플래그 기록에 실패하면 그냥 돌아온다 */
      logPrintf("[E_] 부트 플래그 기록 실패\n");
      break;

    case ACTION_RESET:
      pending_action = ACTION_NONE;
      resetToReset();             /* 돌아오지 않는다 */
      break;

    default:
      pending_action = ACTION_NONE;
      break;
  }
}




/*---------------------------------------------------------------------------
 *  엔드포인트 콜백 (ISR)
 *---------------------------------------------------------------------------*/
static void hidOutCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)ep;

  if (nbytes > 0)
  {
    rx_count++;
    hidCmdHandler(rx_report, tx_report);

    if (is_tx_busy == false)
    {
      is_tx_busy = true;
      usbd_ep_start_write(busid, HID_IN_EP, tx_report, HID_EP_MPS);
    }
  }

  /* 즉시 재무장 — 이 채널은 흐름제어가 필요 없다 */
  usbd_ep_start_read(busid, HID_OUT_EP, rx_report, HID_EP_MPS);
}

static void hidInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;

  is_tx_busy = false;
}

static struct usbd_endpoint hid_out_ep = { .ep_addr = HID_OUT_EP, .ep_cb = hidOutCallback };
static struct usbd_endpoint hid_in_ep  = { .ep_addr = HID_IN_EP,  .ep_cb = hidInCallback  };




/*---------------------------------------------------------------------------
 *  스택 이벤트 / 공개 API
 *---------------------------------------------------------------------------*/
void hidIfEventHandler(uint8_t busid, uint8_t event)
{
  switch (event)
  {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      is_configured = false;
      is_tx_busy    = false;
      break;

    case USBD_EVENT_CONFIGURED:
      is_configured = true;
      is_tx_busy    = false;
      usbd_ep_start_read(busid, HID_OUT_EP, rx_report, HID_EP_MPS);
      break;

    default:
      break;
  }
}

bool hidIfInit(void)
{
  is_configured  = false;
  is_tx_busy     = false;
  pending_action = ACTION_NONE;
  rx_count       = 0;

  usbd_add_interface(HID_BUSID,
                     usbd_hid_init_intf(HID_BUSID, &hid_intf,
                                        hid_report_desc, sizeof(hid_report_desc)));
  usbd_add_endpoint(HID_BUSID, &hid_out_ep);
  usbd_add_endpoint(HID_BUSID, &hid_in_ep);

  return true;
}

bool hidIfIsConfigured(void)
{
  return is_configured;
}

uint32_t hidIfGetRxCount(void)
{
  return rx_count;
}

#endif
#endif
