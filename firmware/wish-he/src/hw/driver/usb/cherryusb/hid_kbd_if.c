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
static volatile bool     is_suspended  = false;
static volatile uint32_t tx_busy_ms    = 0;       /* 실은 시각 — 놓친 완료를 재는 자 */
static volatile uint32_t lost_cnt      = 0;

static struct usbd_interface kbd_intf;

/*
 * ── 부트 프로토콜 ───────────────────────────────────────────────────────
 *
 * 호스트가 SET_PROTOCOL 로 정한다. 0 = 부트, 1 = 리포트.
 *
 * ★ 이걸 안 받고 있었다. 스택의 weak 스텁이 요청을 삼키고 keyboard_protocol_get()
 *   은 언제나 1 을 돌려줬다. 그런데 QMK 는 `keyboard_protocol && keymap_config.nkro`
 *   일 때 **6KRO 를 아예 안 보낸다.** 우리 기본이 NKRO 라, 부트 프로토콜만 아는
 *   BIOS·부트로더 화면에서는 **키가 하나도 안 먹었다.**
 *
 *   실측으로도 보였다 — 타건 중에 `usb stat` 이 KBD sent 1 / EXK nkro 6440 이다.
 *   IF0 은 열거 직후 한 번 나가고 그 뒤로 죽어 있었다.
 *
 * ★ 기본값은 1(리포트)이다. 호스트가 안 물으면 평소대로 NKRO 로 간다.
 *
 * ★ 버스 리셋에서 되돌린다. 그러지 않으면 BIOS 를 거쳐 부팅한 뒤 OS 에서도 부트
 *   프로토콜로 남아 NKRO 가 안 나간다.
 */
static volatile uint8_t kbd_protocol   = 1;

/*
 * 폴링 주기 측정용.
 *
 * 평소에는 바뀔 때만 보내므로 호스트가 실제로 몇 us 마다 물어보는지 알 수 없다.
 * 이 모드에서는 일부러 매번 재무장해서 "폴링마다 전송"이 되게 하고, 완료 사이의
 * 간격을 히스토그램으로 모은다.
 *
 *   125us 와 250us 로 갈린다  -> 호스트는 8kHz 로 묻고 우리가 가끔 놓친 것
 *   167us 근처로 뭉친다        -> 호스트가 그 주기로 묻는 것
 */
static volatile bool     poll_test = false;
static volatile uint32_t poll_last = 0;
static volatile uint32_t poll_min  = 0xFFFFFFFF;
static volatile uint32_t poll_max  = 0;





/* 인터럽트가 막힌 상태이거나 ISR 안에서 호출해야 한다. */
static void kbdArm(void)
{
  if (is_configured == false) return;
  if (is_tx_busy)             return;
  if (is_pending == false)    return;   /* 보낼 게 없으면 싣지 않는다 -> NAK */

  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++) tx_report[i] = shadow[i];

  is_pending = false;
  is_tx_busy = true;
  tx_busy_ms = millis();
  usbd_ep_start_write(KBD_BUSID, KBD_IN_EP, tx_report, KBD_REPORT_LEN);
}


/*
 * 놓친 전송 완료를 되살린다. 메인 루프에서 부른다 (usbUpdate).
 *
 * 사연은 hid_exk_if.c 의 같은 함수에 적어 뒀다 — 플래시를 쓰는 6ms 동안 완료를
 * 놓치면 is_tx_busy 가 굳어 이 인터페이스가 조용히 죽는다.
 *
 * 섀도에는 마지막 상태가 그대로 남아 있으므로 is_pending 만 다시 세우면 된다.
 * 부트 리포트도 "지금 눌린 것 전부" 라는 절대 상태라 한 번 더 보내도 탈이 없다.
 */
/*
 * 호스트가 프로토콜을 정한다. **모든 인터페이스가 이리로 온다** — 부트 서브클래스를
 * 가진 것은 IF0 뿐이라 그것만 본다.
 */
void usbd_hid_set_protocol(uint8_t busid, uint8_t intf, uint8_t protocol)
{
  (void)busid;

  if (intf != 0) return;
  if (kbd_protocol == protocol) return;

  /*
   * ★ 여기서는 값만 기록한다.
   *
   *   바뀌는 순간 리포트가 나가는 인터페이스가 통째로 갈리므로(IF1 NKRO <-> IF0
   *   6KRO) 눌린 키를 비워야 하는데, **그것은 QMK 의 일이고 여기는 제어 전송
   *   안이다.** 하드웨어 층이 QMK 함수를 부르면 층이 뒤집힌다 — 값을 노출하고
   *   변화 감지는 그쪽에서 한다 (qmk.c 의 qmkUpdate).
   */
  kbd_protocol = protocol;
}

uint8_t usbd_hid_get_protocol(uint8_t busid, uint8_t intf)
{
  (void)busid;

  return (intf == 0) ? kbd_protocol : 1;
}

uint8_t hidKbdGetProtocol(void)
{
  return kbd_protocol;
}

/*
 * ★ 시험용이다. 평소에는 호스트가 정한다.
 *
 *   부트 프로토콜을 요구하는 것은 BIOS·부트로더뿐이라 책상에서는 재현할 방법이
 *   없다. 같은 자리에 값을 넣어 흉내 낸다 — 그러지 않으면 "BIOS 에서 키가 먹나"
 *   를 영영 시험할 수 없다.
 */
void hidKbdSetProtocol(uint8_t protocol)
{
  usbd_hid_set_protocol(0, 0, protocol);
}

void hidKbdUpdate(void)
{

  uint32_t mask;

  if (is_tx_busy == false) return;
  if ((millis() - tx_busy_ms) <= 50) return;

  logPrintf("[  ] KBD 전송 완료를 놓쳤다 — 되살린다\n");

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  lost_cnt++;
  is_tx_busy = false;
  is_pending = true;
  kbdArm();
  restore_global_irq(mask);
}

/* 완료 콜백(ISR) — 그 사이에 바뀐 게 있으면 곧바로 잇는다. */
static void kbdInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;

  sent_cnt++;
  is_tx_busy = false;

  if (poll_test)
  {
    uint32_t now = micros();

    if (poll_last)
    {
      uint32_t dt = now - poll_last;

      if (dt < poll_min) poll_min = dt;
      if (dt > poll_max) poll_max = dt;
    }
    poll_last  = now;
    is_pending = true;            /* 매번 다시 싣는다 */
  }

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
void hidKbdSetReportRaw(const uint8_t *p_report)
{
  uint32_t mask;
  bool     same = true;

  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++)
  {
    if (p_report[i] != shadow[i]) { same = false; break; }
  }
  if (same) return;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++) shadow[i] = p_report[i];
  is_pending = true;
  kbdArm();
  restore_global_irq(mask);
}

void hidKbdGetReportRaw(uint8_t *p_report)
{
  for (uint32_t i = 0; i < KBD_REPORT_LEN; i++) p_report[i] = shadow[i];
}

void hidKbdSetReport(uint8_t modifier, const uint8_t *keys, uint32_t cnt)
{
  uint8_t next[KBD_REPORT_LEN];

  if (cnt > KBD_ROLLOVER) cnt = KBD_ROLLOVER;

  next[0] = modifier;
  next[1] = 0;
  for (uint32_t i = 0; i < KBD_ROLLOVER; i++)
  {
    next[2 + i] = (i < cnt) ? keys[i] : 0;
  }

  hidKbdSetReportRaw(next);
}

/*
 * 호스트가 보낸 LED 상태 (Caps/Num/Scroll).
 *
 * SET_REPORT(Output) 로 온다. CherryUSB 의 HID 클래스가 usbd_hid_set_report()
 * 를 부르는데 기본 구현이 weak 라 여기서 받는다.
 */
static volatile uint8_t kbd_leds = 0;

uint8_t hidKbdGetLeds(void)
{
  return kbd_leds;
}

void usbd_hid_set_report(uint8_t busid, uint8_t intf, uint8_t report_id,
                         uint8_t report_type, uint8_t *report, uint32_t report_len)
{
  (void)busid;
  (void)report_id;
  (void)report_type;

  if (intf == 0 && report_len > 0) kbd_leds = report[0];
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
      is_suspended  = false;
      kbdArm();
      break;

    /*
     * 호스트가 잠들었다.
     *
     * ★ 이 신호를 아무도 안 받고 있었다. 상류 QMK 는 프로토콜 계층(ChibiOS/LUFA)이
     *   서스펜드를 보고 suspend_power_down() 을 돌리는데, 우리 포트에는 그 계층이
     *   없다. 그래서 호스트가 자도 LED 가 그대로 켜져 있었다.
     */
    case USBD_EVENT_SUSPEND:
      is_suspended = true;
      break;

    case USBD_EVENT_RESUME:
      is_suspended = false;
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

/* 호스트가 잠들어 있는가. USB 규격상 서스펜드에서는 소비를 줄여야 한다. */
bool hidKbdIsSuspended(void)
{
  return is_suspended;
}

uint32_t hidKbdGetSentCount(void)
{
  return sent_cnt;
}

uint32_t hidKbdGetLostCount(void)
{
  return lost_cnt;
}

void hidKbdPollTest(bool on)
{
  uint32_t mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);

  poll_test = false;

  /* ★ 끌 때는 통계를 지우지 않는다. 지우면 읽기도 전에 사라진다. */
  if (on)
  {
    poll_last  = 0;
    poll_min   = 0xFFFFFFFF;
    poll_max   = 0;
    poll_test  = true;
    is_pending = true;
    kbdArm();                     /* 체인 시작 */
  }
  restore_global_irq(mask);
}

uint32_t hidKbdGetPollMin(void) { return (poll_min == 0xFFFFFFFF) ? 0 : poll_min; }
uint32_t hidKbdGetPollMax(void) { return poll_max; }

#endif
#endif
