/*
 * hid_exk_if.c  —  NKRO · 확장키 인터페이스 (IF1)
 *
 * 부트 키보드(IF0)는 6키 롤오버가 한계다. 그 위에 리포트 ID 로 나눠 쓰는 인터페이스를
 * 하나 더 둔다.
 *
 *   ID 1  NKRO      240비트 비트맵. 동시에 몇 개를 눌러도 다 나간다
 *   ID 3  SYSTEM    전원·절전 같은 시스템 제어
 *   ID 4  CONSUMER  볼륨·재생 같은 미디어키
 *
 * ★ 부트 키보드를 없애지 않는다.
 *
 *   BIOS 와 부트로더는 부트 프로토콜만 안다. NKRO 로 갈아타면 BIOS 에서 키가 안 먹는다.
 *   그래서 둘을 같이 두고, 호스트가 report protocol 을 쓸 때만 NKRO 로 보낸다.
 *   QMK 의 host_can_send_nkro() 가 그 판단을 한다.
 *
 * ★ 전송 정책은 7편 그대로 — 바뀔 때만 싣는다.
 *
 *   같은 리포트를 되풀이해 보내면 이미 실려 있던 옛 값이 먼저 나가느라 지연이 오히려
 *   늘어난다. 섀도와 비교해 다를 때만 프라임한다.
 */

#include "usb/cherryusb/hid_exk_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"


#define EXK_BUSID           0


/*
 * 리포트 기술자.
 *
 * NKRO 비트맵은 usage 0x00~0xEF(240개)를 비트로 편다. 모디파이어(0xE0~0xE7)는 그
 * 범위 안에 이미 들어 있지만, 부트 리포트와 같은 자리에서 다루려고 앞에 8비트를
 * 따로 둔다 — QMK 의 report_nkro_t 배치와 맞춘다.
 */
static const uint8_t exk_report_desc[] = {
  /* ── NKRO (ID 1) ───────────────────────────────────────── */
  0x05, 0x01,                   /* Usage Page (Generic Desktop)  */
  0x09, 0x06,                   /* Usage (Keyboard)              */
  0xA1, 0x01,                   /* Collection (Application)      */
  0x85, EXK_REPORT_ID_NKRO,     /*   Report ID (1)               */
  0x05, 0x07,                   /*   Usage Page (Keyboard)       */
  0x19, 0xE0,                   /*   Usage Minimum (LeftControl) */
  0x29, 0xE7,                   /*   Usage Maximum (Right GUI)   */
  0x15, 0x00,                   /*   Logical Minimum (0)         */
  0x25, 0x01,                   /*   Logical Maximum (1)         */
  0x75, 0x01,                   /*   Report Size (1)             */
  0x95, 0x08,                   /*   Report Count (8)            */
  0x81, 0x02,                   /*   Input (Data,Var,Abs)        */
  0x05, 0x07,                   /*   Usage Page (Keyboard)       */
  0x19, 0x00,                   /*   Usage Minimum (0)           */
  0x29, 0xEF,                   /*   Usage Maximum (239)         */
  0x15, 0x00,                   /*   Logical Minimum (0)         */
  0x25, 0x01,                   /*   Logical Maximum (1)         */
  0x75, 0x01,                   /*   Report Size (1)             */
  0x95, 0xF0,                   /*   Report Count (240)          */
  0x81, 0x02,                   /*   Input (Data,Var,Abs)        */
  0xC0,                         /* End Collection                */

  /* ── 시스템 제어 (ID 3) ────────────────────────────────── */
  0x05, 0x01,                   /* Usage Page (Generic Desktop)  */
  0x09, 0x80,                   /* Usage (System Control)        */
  0xA1, 0x01,                   /* Collection (Application)      */
  0x85, EXK_REPORT_ID_SYSTEM,   /*   Report ID (3)               */
  0x19, 0x01,                   /*   Usage Minimum (1)           */
  0x2A, 0xB7, 0x00,             /*   Usage Maximum (0xB7)        */
  0x15, 0x01,                   /*   Logical Minimum (1)         */
  0x26, 0xB7, 0x00,             /*   Logical Maximum (0xB7)      */
  0x75, 0x10,                   /*   Report Size (16)            */
  0x95, 0x01,                   /*   Report Count (1)            */
  0x81, 0x00,                   /*   Input (Data,Array,Abs)      */
  0xC0,                         /* End Collection                */

  /* ── 컨슈머 (ID 4) ─────────────────────────────────────── */
  0x05, 0x0C,                   /* Usage Page (Consumer)         */
  0x09, 0x01,                   /* Usage (Consumer Control)      */
  0xA1, 0x01,                   /* Collection (Application)      */
  0x85, EXK_REPORT_ID_CONSUMER, /*   Report ID (4)               */
  0x19, 0x01,                   /*   Usage Minimum (1)           */
  0x2A, 0xA0, 0x02,             /*   Usage Maximum (0x2A0)       */
  0x15, 0x01,                   /*   Logical Minimum (1)         */
  0x26, 0xA0, 0x02,             /*   Logical Maximum (0x2A0)     */
  0x75, 0x10,                   /*   Report Size (16)            */
  0x95, 0x01,                   /*   Report Count (1)            */
  0x81, 0x00,                   /*   Input (Data,Array,Abs)      */
  0xC0,                         /* End Collection                */
};

_Static_assert(sizeof(exk_report_desc) == EXK_REPORT_DESC_SIZE,
               "EXK_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");


USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[EXK_EP_MPS];

static volatile uint8_t  shadow[EXK_EP_MPS];
static volatile uint8_t  pending_len   = 0;   /* 0 = 보낼 것 없음 */
static volatile bool     is_configured = false;
static volatile bool     is_tx_busy    = false;
static volatile uint32_t sent_cnt      = 0;

static struct usbd_interface exk_intf;




/* 인터럽트가 막힌 상태이거나 ISR 안에서 호출해야 한다. */
static void exkArm(void)
{
  if (is_configured == false) return;
  if (is_tx_busy)             return;
  if (pending_len == 0)       return;

  for (uint32_t i = 0; i < pending_len; i++) tx_report[i] = shadow[i];

  is_tx_busy  = true;
  usbd_ep_start_write(EXK_BUSID, EXK_IN_EP, tx_report, pending_len);
  pending_len = 0;
}

static void exkInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;

  sent_cnt++;
  is_tx_busy = false;
  exkArm();
}

static struct usbd_endpoint exk_in_ep = { .ep_addr = EXK_IN_EP, .ep_cb = exkInCallback };




void hidExkSendReport(const uint8_t *p_report, uint32_t length)
{
  uint32_t mask;
  bool     same = true;

  if (p_report == NULL || length == 0 || length > EXK_EP_MPS) return;

  /*
   * 리포트 ID 가 다르면 다른 종류다. 길이까지 같이 봐야 "같은 리포트"인지 알 수 있다.
   * 섀도는 하나뿐이라 종류가 번갈아 오면 매번 다르다고 판단되는데, 그게 맞다.
   */
  if (shadow[0] == p_report[0])
  {
    for (uint32_t i = 0; i < length; i++)
    {
      if (p_report[i] != shadow[i]) { same = false; break; }
    }
    if (same) return;
  }

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  for (uint32_t i = 0; i < length; i++) shadow[i] = p_report[i];
  pending_len = (uint8_t)length;
  exkArm();
  restore_global_irq(mask);
}

void hidExkEventHandler(uint8_t busid, uint8_t event)
{
  (void)busid;

  switch (event)
  {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      is_configured = false;
      is_tx_busy    = false;
      pending_len   = 0;
      break;

    case USBD_EVENT_CONFIGURED:
      is_configured = true;
      is_tx_busy    = false;
      pending_len   = 0;
      sent_cnt      = 0;
      break;

    default:
      break;
  }
}

bool hidExkInit(void)
{
  is_configured = false;
  is_tx_busy    = false;
  pending_len   = 0;
  sent_cnt      = 0;
  for (uint32_t i = 0; i < EXK_EP_MPS; i++) shadow[i] = 0;

  usbd_add_interface(EXK_BUSID,
                     usbd_hid_init_intf(EXK_BUSID, &exk_intf,
                                        exk_report_desc, sizeof(exk_report_desc)));
  usbd_add_endpoint(EXK_BUSID, &exk_in_ep);

  return true;
}

bool     hidExkIsConfigured(void) { return is_configured; }
uint32_t hidExkGetSentCount(void) { return sent_cnt; }

#endif
#endif
