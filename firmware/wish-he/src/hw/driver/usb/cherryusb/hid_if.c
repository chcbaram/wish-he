/*
 * hid_if.c  —  raw HID 설정 채널 (CherryUSB, HPM5361)
 *
 * 웹페이지에서 HID 명령으로 부트로더에 진입시킨다. IAP 가 기대하는 방식이다.
 * CDC 콘솔에 사람이 `reset boot` 를 치지 않아도 호스트 도구가 알아서 넘길 수 있다.
 *
 *   호스트 -> 장치 (OUT, 32 B)      장치 -> 호스트 (IN, 32 B)
 *     [0]    명령                     [0]    명령 에코
 *     [1..]  인자                     [1..]  인자 에코
 *                                     [..]   그 뒤부터 응답
 *
 * ★ 인자를 그대로 되돌려줘야 한다.
 *
 *   VIA 클라이언트는 응답이 `[명령, ...인자]` 로 시작하는지 검사하고, 아니면
 *   "Receiving incorrect response for command" 로 버린다. 처음에는 [1] 에 상태
 *   바이트를 넣었다가 그 검사에 걸렸다. 상태는 뺐다 — 실패는 무응답으로 드러난다.
 *
 * ★ 리셋은 콜백(ISR) 안에서 하지 않는다. 응답 IN 전송이 호스트에게 실제로 나가기 전에
 *   리셋해버리면 도구 쪽에서는 그냥 장치가 사라진 것으로 보인다. 그리고 부트 플래그
 *   기록은 ROM API 로 XIP 플래시를 건드리는 일이라 인터럽트 컨텍스트에서 할 일이 아니다.
 *   그래서 콜백은 예약만 하고 hidIfUpdate() 가 메인 루프에서 실행한다.
 *
 * ★ 라이브 트래킹은 여기서 내보내지 않는다.
 *
 *   전용 인터페이스(hid_trk_if.c, IF3)가 맡는다. 이 채널로 같이 내보내면 VIA 의
 *   요청-응답 짝이 어긋나기 때문이다 — VIA 는 명령 다음 IN 리포트를 응답이라고
 *   가정한다. 여기서는 0xC3 으로 켜고 끄기만 한다.
 */

#include "usb/cherryusb/hid_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"
#include "reset.h"
#include "keys.h"
#include "usb/cherryusb/hid_trk_if.h"


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

/*
 * 우리가 모르는 명령은 QMK/VIA 로 넘긴다.
 *
 * OUT 콜백(ISR)에서 바로 부르면 안 된다 — VIA 명령은 EEPROM 을 건드린다.
 * 그래서 복사만 해두고 hidIfUpdate() 가 메인 루프에서 실행한다.
 */
static hid_raw_recv_t    raw_recv    = NULL;
static uint8_t           raw_pending_buf[HID_EP_MPS];

/*
 * 보정 저장 요청.
 *
 * ★ ISR 에서 플래시를 쓰면 안 된다. keysCalSave() 는 2~3ms 걸린다. 요청만 세워
 *   두고 메인 루프(hidUpdate)가 처리한다 — 03편에서 부팅 경로에 플래시 작업을
 *   넣었다가 벽돌을 만든 뒤로 이 경계를 지킨다.
 */
static volatile bool     cal_save_req  = false;
static volatile uint8_t  cal_save_ok   = 0;
static volatile uint8_t  cal_save_skip = 0;
static volatile bool     raw_pending = false;

static struct usbd_interface hid_intf;




/*---------------------------------------------------------------------------
 *  명령 처리
 *---------------------------------------------------------------------------*/

/*
 * 명령별 인자 길이 (명령 바이트 제외).
 *
 * 응답에서 인자 뒤가 곧 페이로드 자리다. 실패할 수 있는 명령이 생기면 그 첫
 * 바이트를 상태로 쓰면 된다 — 지금 명령들은 실패할 여지가 없어 안 쓴다.
 */
/*
 * 되돌려 줄 인자 길이.
 *
 * ★ 보낸 만큼 그대로 돌려줘야 한다.
 *
 *   VIA 클라이언트는 응답이 [명령, 보낸 인자...] 로 시작하는지 확인하고, 안 맞으면
 *   오류로 버린다. 키별 설정 쓰기는 인자가 16바이트인데 2바이트만 돌려주다가
 *   전부 오류가 났다. 읽기는 인자가 2바이트뿐이라 통과해서, 읽기만 되고 쓰기가
 *   안 되는 모양으로 나타났다.
 *
 *   그래서 명령만 보고는 정할 수 없다 — 같은 명령이라도 하위 명령에 따라 다르다.
 */
static uint32_t hidCmdArgLen(const uint8_t *p_rx)
{
  switch (p_rx[0])
  {
    case HID_CMD_LAYOUT: return 1;   /* 시작 인덱스 */
    case HID_CMD_TRACK:  return 1;   /* on/off */
    case HID_CMD_SWITCH: return 1;   /* 인덱스 */
    case HID_CMD_CAL:    return 1;   /* 하위 명령 */

    case HID_CMD_KEYCFG:
      /* get: [하위, idx]   set: [하위, idx] + 값 14바이트 */
      return (p_rx[1] == HID_KEYCFG_SET) ? (2 + HID_KEYCFG_LEN) : 2;

    default:             return 0;
  }
}

/*
 * ISR 컨텍스트. 플래시나 긴 작업은 여기서 하지 않는다.
 *
 * 우리가 아는 명령이면 응답을 채우고 true 를 준다. 모르면 false — 부른 쪽이
 * QMK/VIA 로 넘긴다.
 */
static bool hidCmdHandler(const uint8_t *p_rx, uint8_t *p_tx)
{
  uint8_t cmd = p_rx[0];

  /*
   * 인자를 되돌리고 그 뒤는 지운다.
   *
   * 통째로 memcpy 하면 호스트가 보낸 쓰레기가 응답 꼬리에 섞여 나와 디버깅할 때
   * 헷갈린다. 명령마다 인자 길이가 정해져 있으므로 딱 그만큼만 에코한다.
   */
  memset(p_tx, 0, HID_EP_MPS);
  memcpy(p_tx, p_rx, hidCmdArgLen(p_rx) + 1);

  switch (cmd)
  {
    case HID_CMD_INFO:
    {
      const char *p_name = _DEF_BOARD_NAME;
      uint32_t    i;

      for (i = 0; i < (HID_EP_MPS - 2) && p_name[i] != 0; i++)
      {
        p_tx[1 + i] = (uint8_t)p_name[i];
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
     *   IN  [1] = 시작 인덱스(에코)   [2] = 이 응답의 개수
     *       [3..] = {x, y, w, h, row, col} x 개수
     *
     * 응답에 전체 개수를 안 싣는 대신, 끝을 넘겨 물으면 개수 0 이 온다. 호스트는
     * 0 이 올 때까지 인덱스를 늘리면 된다.
     */
    case HID_CMD_KEYCFG:
    {
      uint32_t idx = p_rx[2];

      if (p_rx[1] == HID_KEYCFG_SET)
      {
        /*
         * ISR 이라 플래시는 건드리지 않는다. RAM 설정만 바꾸고 즉시 판정에 반영된다 —
         * 저장은 VIA 의 save 명령이 따로 한다.
         */
        keysSetKeyCfg(idx, &p_rx[HID_KEYCFG_OFF], HID_EP_MPS - HID_KEYCFG_OFF);
      }
      else
      {
        keysGetKeyCfg(idx, &p_tx[HID_KEYCFG_OFF], HID_EP_MPS - HID_KEYCFG_OFF);
      }
      return true;
    }

    case HID_CMD_LAYOUT:
    {
      uint32_t start = p_rx[1];
      uint32_t total = keysGetLayoutCount();
      uint32_t n     = 0;

      while (n < HID_LAYOUT_PER_FRAME && (start + n) < total)
      {
        const uint8_t *p_geo = keysGetLayoutEntry(start + n);
        uint32_t       o     = HID_LAYOUT_OFF + n * HID_LAYOUT_ENTRY;

        for (uint32_t k = 0; k < HID_LAYOUT_ENTRY; k++) p_tx[o + k] = p_geo[k];
        n++;
      }

      p_tx[2] = (uint8_t)n;
      break;
    }

    /*
     * 보정 — 시작·상태·저장·취소.
     *
     * 표본 수집은 keysUpdate() 안에서 돈다. 여기서는 켜고 끄고 진행 상황만 준다 —
     * ISR 컨텍스트라 오래 걸리는 일을 하면 안 된다.
     *
     * ★ 저장은 플래시를 쓴다. keysCalSave() 가 2~3ms 걸리므로 ISR 에서 부르면
     *   안 된다. 그래서 요청만 세워 두고 메인 루프가 처리한다.
     */
    case HID_CMD_CAL:
    {
      switch (p_rx[1])
      {
        case HID_CAL_START:  keysCalStart();  break;
        case HID_CAL_CANCEL: keysCalCancel(); break;
        case HID_CAL_SAVE:   cal_save_req = true; break;
        default: break;                       /* 상태만 */
      }

      p_tx[2] = keysCalIsActive() ? 1 : 0;
      p_tx[3] = (uint8_t)keysCalDone();
      p_tx[4] = (uint8_t)keysCalTotal();
      p_tx[5] = cal_save_ok;
      p_tx[6] = cal_save_skip;
      keysCalBitmap(&p_tx[HID_CAL_MAP_OFF], HID_EP_MPS - HID_CAL_MAP_OFF);
      break;
    }

    /*
     * 스위치 종류표 — 한 번에 하나씩 준다.
     *
     * 항목이 열 개도 안 되므로 페이지를 나눌 것 없이 인덱스로 하나씩 묻는다.
     * 이름이 문자열이라 한 프레임에 여러 개를 넣으면 자리 계산이 지저분해진다.
     */
    case HID_CMD_SWITCH:
    {
      uint32_t    idx   = p_rx[1];
      uint32_t    total = keysGetSwitchCount();
      const char *p_nm;
      uint16_t    um;

      p_tx[2] = (uint8_t)total;
      p_tx[3] = (uint8_t)keysGetSwitchGenericCount();

      if (idx >= total) break;          /* 범위 밖 — 개수만 알려주고 끝 */

      um      = keysGetSwitchTravelUm(idx);
      p_tx[4] = (uint8_t)(um & 0xFF);
      p_tx[5] = (uint8_t)(um >> 8);

      p_nm = keysGetSwitchName(idx);
      for (uint32_t i = 0; i < (HID_EP_MPS - HID_SWITCH_NAME_OFF - 1) && p_nm[i]; i++)
      {
        p_tx[HID_SWITCH_NAME_OFF + i] = (uint8_t)p_nm[i];
      }
      break;
    }

    /*
     * 라이브 트래킹 on/off.
     *
     *   OUT [1] = 1 시작 / 0 정지
     *   IN  [1] = 에코   [2] = 전체 키 수   [3] = 프레임당 키 수
     *       [4..5] = 전 행정 (LE16, 0.01mm)
     *
     * 전 행정을 같이 주는 것은 호스트가 막대를 몇 mm 짜리로 그릴지 정하기 위해서다.
     * 스위치 종류가 바뀌면 따라가야 하므로 도구에 상수로 박으면 안 된다.
     */
    case HID_CMD_TRACK:
    {
      uint16_t travel = keysGetTravelUm(0, 0);

      hidTrkSetEnable(p_rx[1] != 0);
      p_tx[2] = KEYS_MAX;
      p_tx[3] = TRK_PER_FRAME;
      p_tx[4] = (uint8_t)(travel & 0xFF);
      p_tx[5] = (uint8_t)(travel >> 8);
      break;
    }

    /* 우리 명령이 아니다 — VIA 일 수 있으니 넘긴다 */
    default:
      return false;
  }

  return true;
}

/*
 * VIA 응답을 보낸다. raw_hid_send() 가 부른다 — 메인 루프 컨텍스트다.
 *
 * 이미 전송이 걸려 있으면 빌 때까지 기다린다. 명령 응답은 트래킹 프레임과 달리
 * 버리면 안 된다 — 호스트가 그 응답을 기다리고 있다.
 */
void hidIfSendRaw(const uint8_t *p_data, uint8_t length)
{
  uint32_t mask;
  uint32_t timeout = millis();

  if (is_configured == false || p_data == NULL) return;
  if (length > HID_EP_MPS) length = HID_EP_MPS;

  while (is_tx_busy)
  {
    if (millis() - timeout > 100) return;      /* 호스트가 안 가져간다 */
  }

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  memset(tx_report, 0, HID_EP_MPS);
  memcpy(tx_report, p_data, length);
  is_tx_busy = true;
  usbd_ep_start_write(HID_BUSID, HID_IN_EP, tx_report, HID_EP_MPS);
  restore_global_irq(mask);
}

void hidIfSetRawReceiveFunc(hid_raw_recv_t func)
{
  raw_recv = func;
}

/* 메인 루프에서 부른다. 미뤄둔 VIA 명령과 예약된 리셋을 실제로 수행한다. */
void hidIfUpdate(void)
{
  /*
   * 보정 저장 — ISR 이 아니라 여기서 한다.
   *
   * keysCalSave() 는 플래시를 쓰고 2~3ms 걸린다. ISR 에서 부르면 USB 가 밀린다.
   * 03편에서 부팅 경로에 플래시 작업을 넣었다가 벽돌을 만든 뒤로 이 경계를 지킨다.
   */
  if (cal_save_req)
  {
    uint32_t done = 0, skip = 0;

    cal_save_req  = false;
    cal_save_ok   = keysCalSave(&done, &skip) ? 1 : 0;
    cal_save_skip = (uint8_t)skip;
    logPrintf("[  ] 보정 저장 %d 키, %d 건너뜀, %s\n",
              (int)done, (int)skip, cal_save_ok ? "OK" : "실패");
  }

  if (raw_pending)
  {
    raw_pending = false;
    if (raw_recv != NULL) raw_recv(raw_pending_buf, HID_EP_MPS);
  }

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

    if (hidCmdHandler(rx_report, tx_report))
    {
      if (is_tx_busy == false)
      {
        is_tx_busy = true;
        usbd_ep_start_write(busid, HID_IN_EP, tx_report, HID_EP_MPS);
      }
    }
    else if (raw_recv != NULL && raw_pending == false)
    {
      /* 모르는 명령 — 메인 루프로 미룬다. VIA 는 EEPROM 을 건드린다 */
      memcpy(raw_pending_buf, rx_report, HID_EP_MPS);
      raw_pending = true;
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

bool hidIfIsTracking(void)
{
  return hidTrkIsEnabled();
}

uint32_t hidIfGetRxCount(void)
{
  return rx_count;
}

uint32_t hidIfGetTrackCount(void)
{
  return hidTrkGetFrameCount();
}

#endif
#endif
