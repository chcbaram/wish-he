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
 *
 * ★ 라이브 트래킹은 요청-응답이 아니라 장치가 밀어낸다.
 *
 *   상용 보드는 페이지 번호를 받아 16키씩 돌려주는 요청-응답이다. 그러면 스냅샷
 *   하나에 왕복이 여러 번이라, 폴링 주기가 아무리 짧아도 왕복 수만큼 곱해진다.
 *   우리는 켜두면 장치가 계속 내보낸다 — 왕복이 없어 폴링 주기가 곧 프레임 주기다.
 *
 *     64키 x 4B(원시값+깊이) = 256B,  프레임당 7키  ->  스냅샷 10프레임
 *     125us 폴링  ->  1.25ms 스냅샷 = 초당 800장
 *
 *   래피드 트리거를 보려면 이 정도는 되어야 한다. 4mm 를 10ms 에 지나가는 타건에서
 *   스트로크 전체에 표본이 8장 실린다.
 */

#include "usb/cherryusb/hid_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"
#include "reset.h"
#include "keys.h"


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


/*
 * DMA 가 접근하는 스테이징 버퍼.
 *
 * 트래킹 프레임은 버퍼를 따로 둔다. 명령 응답과 같은 버퍼를 쓰면 한쪽을 채우는 도중
 * 다른 쪽이 전송을 걸 수 있다. 엔드포인트는 하나뿐이라 전송 권한은 is_tx_busy 로
 * 나눠 갖되, 버퍼는 겹치지 않게 한다.
 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t rx_report[HID_EP_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[HID_EP_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tk_report[HID_EP_MPS];

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

/* 라이브 트래킹 */
static volatile bool     track_on    = false;
static          uint32_t track_idx   = 0;   /* 다음 프레임의 첫 키 인덱스 */
static volatile uint32_t track_count = 0;   /* 내보낸 프레임 수 — usb info 진단용 */

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

    /*
     * 물리 배치 읽기 — 페이지 방식.
     *
     *   OUT [1] = 시작 인덱스
     *   IN  [2] = 시작 인덱스   [3] = 이 응답의 개수
     *       [4..] = {x, y, w, h, row, col} x 개수
     *
     * 응답에 전체 개수를 안 싣는 대신, 끝을 넘겨 물으면 개수 0 이 온다. 호스트는
     * 0 이 올 때까지 인덱스를 늘리면 된다.
     */
    case HID_CMD_LAYOUT:
    {
      uint32_t start = p_rx[1];
      uint32_t total = keysGetLayoutCount();
      uint32_t n     = 0;

      while (n < HID_LAYOUT_PER_FRAME && (start + n) < total)
      {
        const uint8_t *p_geo = keysGetLayoutEntry(start + n);
        uint32_t       o     = HID_TRACK_HDR + n * HID_LAYOUT_ENTRY;

        for (uint32_t k = 0; k < HID_LAYOUT_ENTRY; k++) p_tx[o + k] = p_geo[k];
        n++;
      }

      p_tx[2] = (uint8_t)start;
      p_tx[3] = (uint8_t)n;
      break;
    }

    /*
     * 라이브 트래킹 on/off.
     *
     *   OUT [1] = 1 시작 / 0 정지
     *   IN  [2] = 전체 키 수   [3] = 프레임당 키 수   [4..5] = 전 행정(LE16, 0.01mm)
     *
     * 전 행정을 같이 주는 것은 호스트가 막대를 몇 mm 짜리로 그릴지 정하기 위해서다.
     * 스위치 종류가 바뀌면 따라가야 하므로 도구에 상수로 박으면 안 된다.
     */
    case HID_CMD_TRACK:
    {
      uint16_t travel = keysGetTravelUm(0, 0);

      track_on  = (p_rx[1] != 0);
      track_idx = 0;
      p_tx[2]   = KEYS_MAX;
      p_tx[3]   = HID_TRACK_PER_FRAME;
      p_tx[4]   = (uint8_t)(travel & 0xFF);
      p_tx[5]   = (uint8_t)(travel >> 8);
      break;
    }

    default:
      p_tx[1] = HID_RESP_UNKNOWN_CMD;
      break;
  }
}

/*
 * 트래킹 프레임 한 장.
 *
 *   [0] 태그 0xC4   [1] 첫 키 인덱스   [2] 이 프레임 키 수   [3] 전체 키 수
 *   [4..] 키당 4바이트   원시값(LE16) + 깊이(LE16, bit15 = 눌림)
 *
 * 깊이는 0.01mm 라 400 을 넘지 않으므로 상위 비트가 남는다. 거기에 눌림 판정을
 * 실어 보내면 호스트가 임계값을 몰라도 상용 화면처럼 체크 표시를 그릴 수 있다.
 *
 * ★ 버퍼를 채운 뒤에 전송 권한을 잡는다. 그 사이 명령 응답이 끼어들면 이번 프레임은
 *   그냥 버린다 — 다음 폴링에 다시 만들면 되고, 트래킹은 최신값이 중요하지 빠짐없이
 *   가는 게 중요한 게 아니다.
 */
static void hidTrackUpdate(void)
{
  uint32_t n = 0;
  uint32_t mask;

  if (track_on == false || is_configured == false) return;
  if (is_tx_busy)                                  return;   /* 값싼 선검사 */

  memset(tk_report, 0, HID_EP_MPS);

  while (n < HID_TRACK_PER_FRAME && (track_idx + n) < KEYS_MAX)
  {
    uint32_t i   = track_idx + n;
    uint32_t row = i / KEYS_CH_MAX;
    uint32_t col = i % KEYS_CH_MAX;
    uint32_t o   = HID_TRACK_HDR + n * 4;
    uint16_t raw = keysGetRaw(row, col);
    uint16_t um  = keysGetDepthUm(row, col);

    if (keysGetPressed(row, col)) um |= 0x8000;

    tk_report[o + 0] = (uint8_t)(raw & 0xFF);
    tk_report[o + 1] = (uint8_t)(raw >> 8);
    tk_report[o + 2] = (uint8_t)(um & 0xFF);
    tk_report[o + 3] = (uint8_t)(um >> 8);
    n++;
  }

  tk_report[0] = HID_EVT_TRACK;
  tk_report[1] = (uint8_t)track_idx;
  tk_report[2] = (uint8_t)n;
  tk_report[3] = KEYS_MAX;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  if (is_tx_busy == false)
  {
    is_tx_busy = true;
    usbd_ep_start_write(HID_BUSID, HID_IN_EP, tk_report, HID_EP_MPS);

    track_idx += n;
    if (track_idx >= KEYS_MAX) track_idx = 0;
    track_count++;
  }
  restore_global_irq(mask);
}

/* 메인 루프에서 부른다. 트래킹 프레임을 내보내고, 예약된 리셋을 실제로 수행한다. */
void hidIfUpdate(void)
{
  hidTrackUpdate();

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
      track_on      = false;   /* 도구가 끄지 않고 사라져도 계속 쏘지 않게 */
      break;

    case USBD_EVENT_CONFIGURED:
      is_configured = true;
      is_tx_busy    = false;
      track_on      = false;
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
  track_on       = false;
  track_idx      = 0;
  track_count    = 0;

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

bool hidIfIsTracking(void)
{
  return track_on;
}

uint32_t hidIfGetRxCount(void)
{
  return rx_count;
}

uint32_t hidIfGetTrackCount(void)
{
  return track_count;
}

#endif
#endif
