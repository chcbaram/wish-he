/*
 * hid_trk_if.c  —  HE 라이브 트래킹 (IF3, IN 전용)
 *
 * 키마다 눌린 깊이(mm)와 원시값을 장치가 계속 밀어낸다. 13편 래피드 트리거의 튜닝
 * 도구가 목적이다 — 0.1mm 단위 방향 반전은 CLI 로 볼 수 없다.
 *
 * ★ 왜 raw HID 에서 뗐나.
 *
 *   처음에는 설정 채널(IF2)과 엔드포인트를 나눠 썼다. 그런데 VIA 는 "명령을 보내면
 *   다음 IN 리포트가 그 응답"이라고 가정한다. 트래킹 프레임이 그 사이에 끼면 짝이
 *   어긋나 표준 VIA 도구와 공존이 안 된다. 그래서 IN 전용 인터페이스를 따로 뒀다.
 *
 * ★ 왜 요청-응답이 아니라 밀어내나.
 *
 *   페이지 번호를 받아 몇 키씩 돌려주는 요청-응답으로 짜면 스냅샷 하나에 왕복이
 *   여러 번 든다. 켜두고 계속 내보내면 왕복이 없어 폴링 주기가 곧 프레임 주기다.
 *
 *     64키 x 4B(원시값+깊이) = 256B,  프레임당 7키  ->  스냅샷 10프레임
 *     125us 폴링  ->  스냅샷 1.25ms = 초당 800장
 */

#include "usb/cherryusb/hid_trk_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"
#include "keys.h"


#define TRK_BUSID           0


/*
 * 벤더 정의 페이지. VIA(0xFF60)와 겹치지 않게 0xFF61 을 쓴다 — 호스트 도구가
 * 설정 채널과 스트리밍 채널을 usage page 로 갈라 잡을 수 있다.
 */
static const uint8_t trk_report_desc[] = {
  0x06, 0x61, 0xFF,         /* Usage Page (Vendor Defined 0xFF61)  */
  0x09, 0x61,               /* Usage (0x61)                        */
  0xA1, 0x01,               /* Collection (Application)            */
  0x09, 0x62,               /*   Usage (0x62)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, TRK_EP_MPS,         /*   Report Count (32)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x81, 0x02,               /*   Input (Data, Var, Abs)            */
  0xC0                      /* End Collection                      */
};

_Static_assert(sizeof(trk_report_desc) == TRK_REPORT_DESC_SIZE,
               "TRK_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");


USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[TRK_EP_MPS];

static volatile bool     is_configured = false;
static volatile bool     is_tx_busy    = false;
static volatile bool     track_on      = false;
static          uint32_t track_idx     = 0;   /* 다음 프레임의 첫 키 인덱스 */
static volatile uint32_t frame_cnt     = 0;

static struct usbd_interface trk_intf;




static void trkInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;

  is_tx_busy = false;
}

static struct usbd_endpoint trk_in_ep = { .ep_addr = TRK_IN_EP, .ep_cb = trkInCallback };




/*
 * 프레임 한 장.
 *
 *   [0] 태그   [1] 첫 키 인덱스   [2] 이 프레임 키 수   [3] 전체 키 수
 *   [4..] 키당 4바이트   원시값(LE16) + 깊이(LE16, bit15 = 눌림)
 *
 * 깊이는 0.01mm 라 400 을 넘지 않으므로 상위 비트가 남는다. 거기에 눌림 판정을
 * 실으면 호스트가 임계값을 몰라도 눌림 표시를 그릴 수 있다.
 *
 * 첫 키 인덱스를 달고 다니므로 프레임이 몇 개 빠져도 나머지가 제자리에 들어간다.
 * 순서 맞추기용 상태를 호스트가 들 필요가 없다.
 */
void hidTrkUpdate(void)
{
  uint32_t n = 0;
  uint32_t mask;

  if (track_on == false || is_configured == false) return;
  if (is_tx_busy)                                  return;

  memset(tx_report, 0, TRK_EP_MPS);

  while (n < TRK_PER_FRAME && (track_idx + n) < KEYS_MAX)
  {
    uint32_t i   = track_idx + n;
    uint32_t row = i / KEYS_CH_MAX;
    uint32_t col = i % KEYS_CH_MAX;
    uint32_t o   = TRK_HDR + n * 4;
    uint16_t raw = keysGetRaw(row, col);
    uint16_t um  = keysGetDepthUm(row, col);

    if (keysGetPressed(row, col)) um |= 0x8000;

    tx_report[o + 0] = (uint8_t)(raw & 0xFF);
    tx_report[o + 1] = (uint8_t)(raw >> 8);
    tx_report[o + 2] = (uint8_t)(um & 0xFF);
    tx_report[o + 3] = (uint8_t)(um >> 8);
    n++;
  }

  tx_report[0] = TRK_TAG;
  tx_report[1] = (uint8_t)track_idx;
  tx_report[2] = (uint8_t)n;
  tx_report[3] = KEYS_MAX;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  if (is_tx_busy == false)
  {
    is_tx_busy = true;

    /*
     * ★ 못 실었으면 되돌린다. 이 통로에는 되살리기가 없다 — is_tx_busy 가 굳으면
     *   재열거 전까지 트래킹이 통째로 멎는다. 같은 자리를 놓고 KBD·EXK 와 규칙을
     *   맞춘다.
     *
     *   프레임은 다시 안 만든다. 어차피 다음 호출이 지금 값으로 새로 만든다 —
     *   트래킹은 관찰용이라 묵은 프레임보다 최신 것이 맞다.
     */
    if (usbd_ep_start_write(TRK_BUSID, TRK_IN_EP, tx_report, TRK_EP_MPS) != 0)
    {
      is_tx_busy = false;
    }
    else
    {
      track_idx += n;
      if (track_idx >= KEYS_MAX) track_idx = 0;
      frame_cnt++;
    }
  }
  restore_global_irq(mask);
}

void hidTrkSetEnable(bool on)
{
  track_on  = on;
  track_idx = 0;
}

void hidTrkEventHandler(uint8_t busid, uint8_t event)
{
  (void)busid;

  switch (event)
  {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
    case USBD_EVENT_CONFIGURED:
      is_configured = (event == USBD_EVENT_CONFIGURED);
      is_tx_busy    = false;
      track_on      = false;   /* 도구가 끄지 않고 사라져도 계속 쏘지 않게 */
      track_idx     = 0;
      frame_cnt     = 0;
      break;

    default:
      break;
  }
}

bool hidTrkInit(void)
{
  is_configured = false;
  is_tx_busy    = false;
  track_on      = false;
  track_idx     = 0;
  frame_cnt     = 0;

  usbd_add_interface(TRK_BUSID,
                     usbd_hid_init_intf(TRK_BUSID, &trk_intf,
                                        trk_report_desc, sizeof(trk_report_desc)));
  usbd_add_endpoint(TRK_BUSID, &trk_in_ep);

  return true;
}

bool     hidTrkIsEnabled(void)    { return track_on; }
uint32_t hidTrkGetFrameCount(void) { return frame_cnt; }

#endif
#endif
