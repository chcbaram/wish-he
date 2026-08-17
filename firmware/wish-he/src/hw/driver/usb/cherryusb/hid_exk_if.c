/*
 * hid_exk_if.c  —  NKRO · 확장키 인터페이스 (IF1)
 *
 * 부트 키보드(IF0)는 6키 롤오버가 한계다. 그 위에 리포트 ID 로 나눠 쓰는 인터페이스를
 * 하나 더 둔다.
 *
 *   ID 6  NKRO      240비트 비트맵. 동시에 몇 개를 눌러도 다 나간다
 *   ID 2  MOUSE     마우스키 — 버튼 8개 + X/Y/휠/가로휠
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
 *
 * ★ 섀도는 리포트 종류마다 따로 둔다.
 *
 *   엔드포인트는 하나인데 종류는 여럿이다. 섀도를 하나만 두면 전송이 진행 중인 사이에
 *   두 종류가 겹쳐 들어올 때 뒤엣것이 앞엣것을 덮어 없앤다. 같은 종류끼리라면 그게
 *   맞는 병합이다 — NKRO 도 컨슈머도 "지금 눌린 것 전부" 라는 절대 상태라 최신 것
 *   하나면 충분하다. 하지만 종류가 다르면 서로를 복구해주지 못한다. 컨슈머 리포트가
 *   NKRO 릴리즈를 덮으면 그 키는 다음 키 이벤트가 올 때까지 눌린 채로 남는다.
 *
 *   그래서 종류마다 칸을 하나씩 주고, 보낼 것이 있다는 표시(dirty)만 세워둔다.
 *   전송 중이면 표시를 그대로 남겨두고 완료 콜백에서 다시 훑는다.
 *
 *   훑는 것은 도착 순서가 아니라 고정 우선순위다. 키보드가 늘 먼저 나가야 다른
 *   종류(뒤에 붙을 마우스가 특히 그렇다) 뒤에 줄서지 않는다. FIFO 로 만들면 이
 *   성질을 잃는다.
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

  /* ── 마우스 (ID 2) ─────────────────────────────────────────
   *
   * 참고 보드가 쓰던 블록을 그대로 옮겼다. 버튼을 8개로 잡아 패딩이 없다 —
   * QMK 는 KC_MS_BTN1~8 을 다 갖고 있고 report_mouse_t 의 buttons 도 8비트다.
   * X/Y/Wheel/AC Pan 이 전부 int8 이라 리포트가 6바이트로 report_mouse_t 와
   * 정확히 맞는다 (MOUSE_EXTENDED_REPORT 를 켜지 않는 이유).
   */
  0x05, 0x01,                   /* Usage Page (Generic Desktop)  */
  0x09, 0x02,                   /* Usage (Mouse)                 */
  0xA1, 0x01,                   /* Collection (Application)      */
  0x85, EXK_REPORT_ID_MOUSE,    /*   Report ID (2)               */
  0x09, 0x01,                   /*   Usage (Pointer)             */
  0xA1, 0x00,                   /*   Collection (Physical)       */
  0x05, 0x09,                   /*     Usage Page (Button)       */
  0x19, 0x01,                   /*     Usage Minimum (1)         */
  0x29, 0x08,                   /*     Usage Maximum (8)         */
  0x15, 0x00,                   /*     Logical Minimum (0)       */
  0x25, 0x01,                   /*     Logical Maximum (1)       */
  0x95, 0x08,                   /*     Report Count (8)          */
  0x75, 0x01,                   /*     Report Size (1)           */
  0x81, 0x02,                   /*     Input (Data,Var,Abs)      */
  0x05, 0x01,                   /*     Usage Page (Generic Desktop) */
  0x09, 0x30,                   /*     Usage (X)                 */
  0x09, 0x31,                   /*     Usage (Y)                 */
  0x15, 0x81,                   /*     Logical Minimum (-127)    */
  0x25, 0x7F,                   /*     Logical Maximum (127)     */
  0x95, 0x02,                   /*     Report Count (2)          */
  0x75, 0x08,                   /*     Report Size (8)           */
  0x81, 0x06,                   /*     Input (Data,Var,Rel)      */
  0x09, 0x38,                   /*     Usage (Wheel)             */
  0x15, 0x81,                   /*     Logical Minimum (-127)    */
  0x25, 0x7F,                   /*     Logical Maximum (127)     */
  0x95, 0x01,                   /*     Report Count (1)          */
  0x75, 0x08,                   /*     Report Size (8)           */
  0x81, 0x06,                   /*     Input (Data,Var,Rel)      */
  0x05, 0x0C,                   /*     Usage Page (Consumer)     */
  0x0A, 0x38, 0x02,             /*     Usage (AC Pan)            */
  0x15, 0x81,                   /*     Logical Minimum (-127)    */
  0x25, 0x7F,                   /*     Logical Maximum (127)     */
  0x95, 0x01,                   /*     Report Count (1)          */
  0x75, 0x08,                   /*     Report Size (8)           */
  0x81, 0x06,                   /*     Input (Data,Var,Rel)      */
  0xC0,                         /*   End Collection (Physical)   */
  0xC0,                         /* End Collection                */
};

_Static_assert(sizeof(exk_report_desc) == EXK_REPORT_DESC_SIZE,
               "EXK_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");


USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[EXK_EP_MPS];

/*
 * 리포트 종류별 칸. 배열 순서가 그대로 전송 우선순위다 — 키보드가 맨 앞이라
 * 마우스 리포트 뒤에 줄서지 않는다.
 */
typedef enum
{
  EXK_SLOT_NKRO = 0,
  EXK_SLOT_MOUSE,
  EXK_SLOT_CONSUMER,
  EXK_SLOT_SYSTEM,
  EXK_SLOT_MAX
} exk_slot_t;

/* 마우스 리포트 = { id, buttons, x, y, v, h } — report_mouse_t 와 같은 배치 */
#define EXK_MOUSE_LEN       6
#define EXK_MOUSE_OFS_BTN   1
#define EXK_MOUSE_OFS_DELTA 2   /* x, y, v, h 네 개가 이 자리부터 int8 로 붙는다 */
#define EXK_MOUSE_DELTA_CNT 4

typedef struct
{
  uint8_t          buf[EXK_EP_MPS];   /* 마지막으로 실은(또는 실을) 내용 */
  uint8_t          len;
  volatile bool    dirty;             /* 아직 안 나간 새 내용이 있다 */
  volatile uint32_t cnt;              /* 이 칸에서 실어 보낸 횟수 */
} exk_slot_info_t;

static exk_slot_info_t   slot[EXK_SLOT_MAX];
static volatile bool     is_configured = false;
static volatile bool     is_tx_busy    = false;
static volatile uint32_t tx_busy_ms    = 0;    /* 실은 시각 — 놓친 완료를 재는 자 */
static volatile int32_t  tx_slot       = -1;   /* 지금 나가 있는 칸 */
static volatile uint32_t sent_cnt      = 0;
static volatile uint32_t lost_cnt      = 0;

static struct usbd_interface exk_intf;


static void exkSlotClear(void)
{
  for (uint32_t i = 0; i < EXK_SLOT_MAX; i++)
  {
    slot[i].len   = 0;
    slot[i].dirty = false;
    slot[i].cnt   = 0;
    for (uint32_t j = 0; j < EXK_EP_MPS; j++) slot[i].buf[j] = 0;
  }
}


/* 리포트 ID -> 칸. 모르는 ID 는 버린다. */
static bool exkSlotOf(uint8_t report_id, exk_slot_t *p_slot)
{
  switch (report_id)
  {
    case EXK_REPORT_ID_NKRO:     *p_slot = EXK_SLOT_NKRO;     return true;
    case EXK_REPORT_ID_MOUSE:    *p_slot = EXK_SLOT_MOUSE;    return true;
    case EXK_REPORT_ID_CONSUMER: *p_slot = EXK_SLOT_CONSUMER; return true;
    case EXK_REPORT_ID_SYSTEM:   *p_slot = EXK_SLOT_SYSTEM;   return true;
    default:                                                  return false;
  }
}


/* 인터럽트가 막힌 상태이거나 ISR 안에서 호출해야 한다. */
static void exkArm(void)
{
  if (is_configured == false) return;
  if (is_tx_busy)             return;

  for (uint32_t i = 0; i < EXK_SLOT_MAX; i++)
  {
    if (slot[i].dirty == false) continue;

    for (uint32_t j = 0; j < slot[i].len; j++) tx_report[j] = slot[i].buf[j];

    is_tx_busy = true;
    if (usbd_ep_start_write(EXK_BUSID, EXK_IN_EP, tx_report, slot[i].len) != 0)
    {
      /* 못 실었으면 dirty 를 그대로 남겨 다음 기회에 다시 시도한다. */
      is_tx_busy = false;
      return;
    }
    tx_busy_ms    = millis();
    tx_slot       = (int32_t)i;
    slot[i].dirty = false;
    slot[i].cnt++;
    return;
  }
}


/*
 * 놓친 전송 완료를 되살린다. 메인 루프에서 부른다 (usbUpdate).
 *
 * ★ 플래시를 쓰는 동안 인터럽트가 6ms 꺼진다 (XIP 라 어쩔 수 없다 — 3편). 그 사이
 *   전송 완료를 놓치면 is_tx_busy 가 참인 채로 굳고, 그 뒤로는 한 번도 못 보낸다.
 *   NKRO 가 켜져 있으면 키보드 본류가 이 엔드포인트라 **타이핑이 통째로 멈춘다.**
 *   프로파일 전환·보정 저장이 바로 그 플래시를 쓰는 일이다.
 *
 *   자리가 났는지 여기서는 알 길이 없으니 hid_if.c 와 같게 시간으로 푼다. 한 번의
 *   전송은 1ms 안에 끝난다. 50ms 가 지났으면 놓친 것이다.
 *
 * ★ 나가 있던 칸을 다시 dirty 로 세운다.
 *
 *   그 리포트가 실제로 나갔는지 아닌지를 모른다. 안 나갔는데 넘어가면 키가 눌린 채로
 *   남는다. 세 종류 다 절대 상태라 같은 것을 한 번 더 보내도 호스트가 보는 상태는
 *   그대로다 — 모르면 다시 보내는 쪽이 맞다.
 */
void hidExkUpdate(void)
{
  uint32_t mask;

  if (is_tx_busy == false) return;
  if ((millis() - tx_busy_ms) <= 50) return;

  logPrintf("[  ] EXK 전송 완료를 놓쳤다 — 되살린다\n");

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  lost_cnt++;
  is_tx_busy = false;
  /* 마우스만 빼놓는다 — 상대값이라 다시 보내면 그만큼 더 움직인다. 한 걸음(10~20ms)
   * 이 빠지는 것은 눈에 안 띄지만 두 번 가는 것은 규칙에 어긋난다. */
  if (tx_slot >= 0 && tx_slot < EXK_SLOT_MAX &&
      tx_slot != EXK_SLOT_MOUSE && slot[tx_slot].len > 0)
  {
    slot[tx_slot].dirty = true;
  }
  tx_slot = -1;
  exkArm();
  restore_global_irq(mask);
}

static void exkInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;

  sent_cnt++;
  is_tx_busy = false;
  tx_slot    = -1;
  exkArm();
}

static struct usbd_endpoint exk_in_ep = { .ep_addr = EXK_IN_EP, .ep_cb = exkInCallback };




void hidExkSendReport(const uint8_t *p_report, uint32_t length)
{
  exk_slot_t       id;
  exk_slot_info_t *p_slot;
  uint32_t         mask;
  bool             same;

  if (p_report == NULL || length == 0 || length > EXK_EP_MPS) return;
  if (exkSlotOf(p_report[0], &id) == false) return;

  p_slot = &slot[id];

  /*
   * ★ 마우스만 규칙이 다르다 — 상대값이기 때문이다.
   *
   *   NKRO·컨슈머·시스템은 "지금 눌린 것 전부" 라는 절대 상태다. 같은 값이면 다시
   *   보낼 필요가 없고, 안 나간 것을 새 값으로 덮어도 최신 상태가 그대로 간다.
   *
   *   마우스의 x/y/휠은 "지난번에서 얼마나" 다. 같은 값이 이어지는 것은 같은 속도로
   *   계속 움직이는 중이라는 뜻이라 눌러버리면 커서가 멈춘다. 안 나간 것을 덮으면
   *   그만큼 이동이 사라진다. 그래서 걸러내지 않고, 겹치면 **더한다.**
   */
  if (id == EXK_SLOT_MOUSE)
  {
    mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);

    if (p_slot->dirty && p_slot->len == length)
    {
      /* 버튼은 절대 상태라 새 값으로, 이동량은 못 나간 만큼 얹는다 */
      p_slot->buf[EXK_MOUSE_OFS_BTN] = p_report[EXK_MOUSE_OFS_BTN];

      for (uint32_t i = 0; i < EXK_MOUSE_DELTA_CNT; i++)
      {
        uint32_t k   = EXK_MOUSE_OFS_DELTA + i;
        int32_t  sum = (int32_t)(int8_t)p_slot->buf[k] + (int32_t)(int8_t)p_report[k];

        if (sum >  127) sum =  127;
        if (sum < -127) sum = -127;
        p_slot->buf[k] = (uint8_t)(int8_t)sum;
      }
    }
    else
    {
      for (uint32_t i = 0; i < length; i++) p_slot->buf[i] = p_report[i];
      p_slot->len = (uint8_t)length;
    }

    p_slot->dirty = true;
    exkArm();
    restore_global_irq(mask);
    return;
  }

  /*
   * 이미 그 내용으로 나간 뒤라면 다시 실을 것이 없다.
   *
   * 아직 안 나간(dirty) 칸은 건드리지 않고 그대로 둔다 — 같은 내용이니 덮으나 마나다.
   */
  if (p_slot->len == length)
  {
    same = true;
    for (uint32_t i = 0; i < length; i++)
    {
      if (p_report[i] != p_slot->buf[i]) { same = false; break; }
    }
    if (same) return;
  }

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  for (uint32_t i = 0; i < length; i++) p_slot->buf[i] = p_report[i];
  p_slot->len   = (uint8_t)length;
  p_slot->dirty = true;
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
      tx_slot       = -1;
      exkSlotClear();
      break;

    case USBD_EVENT_CONFIGURED:
      is_configured = true;
      is_tx_busy    = false;
      tx_slot       = -1;
      sent_cnt      = 0;
      lost_cnt      = 0;
      /* 끊기기 전 상태가 남아 있으면 첫 리포트가 같다고 눌러 없어진다 */
      exkSlotClear();
      break;

    default:
      break;
  }
}

bool hidExkInit(void)
{
  is_configured = false;
  is_tx_busy    = false;
  tx_slot       = -1;
  sent_cnt      = 0;
  lost_cnt      = 0;
  exkSlotClear();

  usbd_add_interface(EXK_BUSID,
                     usbd_hid_init_intf(EXK_BUSID, &exk_intf,
                                        exk_report_desc, sizeof(exk_report_desc)));
  usbd_add_endpoint(EXK_BUSID, &exk_in_ep);

  return true;
}

bool     hidExkIsConfigured(void) { return is_configured; }
uint32_t hidExkGetSentCount(void) { return sent_cnt; }
uint32_t hidExkGetLostCount(void) { return lost_cnt; }

/* 종류별로 몇 번 실었는지. 어느 칸이 도는지 눈으로 가르려고 둔다. */
uint32_t hidExkGetSentCountOf(uint8_t report_id)
{
  exk_slot_t id;

  if (exkSlotOf(report_id, &id) == false) return 0;
  return slot[id].cnt;
}

#endif
#endif
