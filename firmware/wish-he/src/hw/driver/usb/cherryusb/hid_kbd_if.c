/*
 * hid_kbd_if.c  —  HID 부트 키보드 (CherryUSB, HPM5361)
 *
 * ★ 지연의 핵심은 "언제 버퍼에 넣느냐" 다.
 *
 *   IN 엔드포인트는 호스트가 주도한다. 호스트가 bInterval 마다 IN 토큰을 보내고,
 *   장치에 전송이 실려 있으면 하드웨어가 그대로 내보낸다(CPU 개입 0). 안 실려
 *   있으면 NAK 이고 그 폴링은 그냥 넘어간다 — 이건 정상 동작이다.
 *
 *   ★ 목표는 "초당 리포트 수"가 아니라 "바뀐 값이 언제 선에 실리느냐" 다.
 *
 *   처음에는 매번 재무장해서 초당 8000개를 채우려 했는데, 그러면 오히려 느려진다.
 *   키가 바뀌는 순간 이미 실려 있는 옛날 리포트가 먼저 나가고, 새 값은 그 전송이
 *   끝난 뒤 다음 폴링에나 나가기 때문이다 (최대 250us).
 *
 *   바뀔 때만 실으면 즉시 프라임되어 다음 폴링(125us)에 나간다. 지연이 절반이고
 *   같은 값을 되풀이해 보내느라 버스와 CPU 를 쓰지도 않는다.
 *
 *   그래서 구조는 이렇다.
 *     스캔  -> hidKbdSetReport() : 바뀌었을 때만 섀도 갱신 + 프라임
 *     완료  -> 그 사이 또 바뀌었으면 곧바로 잇는다
 */

#include "usb/cherryusb/hid_kbd_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"


#define KBD_BUSID           0


/* 표준 부트 키보드 기술자. BIOS 가 알아보는 형태라 임의로 바꾸면 안 된다. */
static const uint8_t kbd_report_desc[] = {
  0x05, 0x01,       /* Usage Page (Generic Desktop)        */
  0x09, 0x06,       /* Usage (Keyboard)                    */
  0xA1, 0x01,       /* Collection (Application)            */
  0x05, 0x07,       /*   Usage Page (Keyboard/Keypad)      */
  0x19, 0xE0,       /*   Usage Minimum (LeftControl)       */
  0x29, 0xE7,       /*   Usage Maximum (Right GUI)         */
  0x15, 0x00,       /*   Logical Minimum (0)               */
  0x25, 0x01,       /*   Logical Maximum (1)               */
  0x75, 0x01,       /*   Report Size (1)                   */
  0x95, 0x08,       /*   Report Count (8)                  */
  0x81, 0x02,       /*   Input (Data,Var,Abs)   모디파이어 */
  0x95, 0x01,       /*   Report Count (1)                  */
  0x75, 0x08,       /*   Report Size (8)                   */
  0x81, 0x03,       /*   Input (Const)          예약        */
  0x95, 0x05,       /*   Report Count (5)                  */
  0x75, 0x01,       /*   Report Size (1)                   */
  0x05, 0x08,       /*   Usage Page (LED)                  */
  0x19, 0x01,       /*   Usage Minimum (Num Lock)          */
  0x29, 0x05,       /*   Usage Maximum (Kana)              */
  0x91, 0x02,       /*   Output (Data,Var,Abs)  LED        */
  0x95, 0x01,       /*   Report Count (1)                  */
  0x75, 0x03,       /*   Report Size (3)                   */
  0x91, 0x03,       /*   Output (Const)         패딩        */
  0x95, 0x06,       /*   Report Count (6)                  */
  0x75, 0x08,       /*   Report Size (8)                   */
  0x15, 0x00,       /*   Logical Minimum (0)               */
  0x25, 0xFF,       /*   Logical Maximum (255)             */
  0x05, 0x07,       /*   Usage Page (Keyboard/Keypad)      */
  0x19, 0x00,       /*   Usage Minimum (0)                 */
  0x29, 0xFF,       /*   Usage Maximum (255)               */
  0x81, 0x00,       /*   Input (Data,Array)     키 6개     */
  0xC0              /* End Collection                      */
};

_Static_assert(sizeof(kbd_report_desc) == KBD_REPORT_DESC_SIZE,
               "KBD_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");


/* DMA 가 접근하는 전송 버퍼 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[KBD_REPORT_LEN];

/* 스캔이 갱신하는 섀도. 전송과 분리해 스캔 주기에 얽매이지 않게 한다. */
static volatile uint8_t  shadow[KBD_REPORT_LEN];
static volatile bool     is_configured = false;
static volatile bool     is_tx_busy    = false;
static volatile bool     is_pending    = false;   /* 아직 안 보낸 새 리포트가 있다 */
static volatile uint32_t sent_cnt      = 0;

static struct usbd_interface kbd_intf;




/* 인터럽트가 막힌 상태이거나 ISR 안에서 호출해야 한다. */
static void kbdArm(void)
{
  if (is_configured == false) return;
  if (is_tx_busy)             return;
  if (is_pending == false)    return;   /* 보낼 게 없으면 싣지 않는다 -> NAK */

  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++) tx_report[i] = shadow[i];

  is_pending = false;
  is_tx_busy = true;
  usbd_ep_start_write(KBD_BUSID, KBD_IN_EP, tx_report, KBD_REPORT_LEN);
}

/* 완료 콜백(ISR) — 그 사이에 바뀐 게 있으면 곧바로 잇는다. */
static void kbdInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;

  sent_cnt++;
  is_tx_busy = false;
  kbdArm();
}

static struct usbd_endpoint kbd_in_ep = { .ep_addr = KBD_IN_EP, .ep_cb = kbdInCallback };




/*
 * ★ 바뀔 때만 싣는다.
 *
 *   처음에는 매번 재무장해서 초당 8000개를 채우려 했다. 그런데 그러면 키가 바뀌는
 *   순간 "이미 실려 있는 옛날 리포트"가 먼저 나가고, 새 값은 그 전송이 끝난 뒤
 *   다음 폴링에나 나간다 — 지연이 최대 250us 로 오히려 두 배가 된다.
 *
 *   바뀔 때만 실으면 즉시 프라임되어 다음 폴링(125us)에 나간다. 지연이 절반이고,
 *   같은 값을 되풀이해 보내느라 버스와 CPU 를 쓰지도 않는다. 안 실려 있는 동안
 *   하드웨어가 NAK 하는 것은 정상 동작이다.
 *
 *   그래서 "초당 리포트 수"는 목표가 아니다. 놀 때 0 이고 칠 때만 올라가는 게 맞다.
 */
void hidKbdSetReport(uint8_t modifier, const uint8_t *keys, uint32_t cnt)
{
  uint8_t  next[KBD_REPORT_LEN];
  uint32_t mask;
  bool     same = true;

  if (cnt > KBD_ROLLOVER) cnt = KBD_ROLLOVER;

  next[0] = modifier;
  next[1] = 0;
  for (uint32_t i = 0; i < KBD_ROLLOVER; i++)
  {
    next[2 + i] = (i < cnt) ? keys[i] : 0;
  }

  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++)
  {
    if (next[i] != shadow[i]) { same = false; break; }
  }
  if (same) return;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++) shadow[i] = next[i];
  is_pending = true;
  kbdArm();
  restore_global_irq(mask);
}

void hidKbdEventHandler(uint8_t busid, uint8_t event)
{
  (void)busid;

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
      is_pending    = true;     /* 열거 직후 한 번은 현재 상태를 알린다 */
      sent_cnt      = 0;
      kbdArm();
      break;

    default:
      break;
  }
}

bool hidKbdInit(void)
{
  is_configured = false;
  is_tx_busy    = false;
  is_pending    = false;
  sent_cnt      = 0;
  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++) shadow[i] = 0;

  usbd_add_interface(KBD_BUSID,
                     usbd_hid_init_intf(KBD_BUSID, &kbd_intf,
                                        kbd_report_desc, sizeof(kbd_report_desc)));
  usbd_add_endpoint(KBD_BUSID, &kbd_in_ep);

  return true;
}

bool hidKbdIsConfigured(void)
{
  return is_configured;
}

uint32_t hidKbdGetSentCount(void)
{
  return sent_cnt;
}

#endif
#endif
