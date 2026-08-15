/*
 * port/driver_usb.c  —  QMK 의 출력 드라이버를 우리 CherryUSB 로 잇는다.
 *
 * host.c 의 host_keyboard_send() 등이 활성 드라이버의 함수 포인터로 넘어온다.
 *
 * ★ 전송 정책은 7편 그대로다 — "바뀔 때만 싣는다".
 *
 *   hidKbdSetReportRaw() 가 섀도와 비교해 같으면 아무것도 하지 않는다. QMK 는
 *   같은 리포트를 여러 번 보낼 수 있는데(레이어 처리 중간 상태 등) 그때마다
 *   전송을 걸면 옛 리포트가 먼저 나가느라 지연이 오히려 늘어난다.
 *
 * NKRO · 마우스 · 미디어키는 아직 인터페이스가 없다. 기술자에 IF 를 더하면 CDC 의
 * 인터페이스 번호가 밀리므로(7편의 등록 순서 함정) 11편에서 한꺼번에 정리한다.
 */

#include "host.h"
#include "host_driver.h"
#include "report.h"
#include "keycode_config.h"
#include "usb/cherryusb/hid_kbd_if.h"
#include "usb/cherryusb/hid_exk_if.h"


static uint8_t usb_keyboard_leds(void)
{
  return hidKbdGetLeds();
}

static void usb_send_keyboard(report_keyboard_t *report)
{
  /* report_keyboard_t = { mods, reserved, keys[6] } — 부트 리포트와 같은 8바이트 */
  hidKbdSetReportRaw((const uint8_t *)report);
}

static void usb_send_nkro(report_nkro_t *report)
{
#ifdef NKRO_ENABLE
  /* report_nkro_t = { report_id, mods, bits[30] } — IF1 의 ID 1 배치와 같다 */
  hidExkSendReport((const uint8_t *)report, sizeof(report_nkro_t));
#else
  (void)report;
#endif
}

static void usb_send_mouse(report_mouse_t *report)
{
  (void)report;   /* 마우스 인터페이스는 아직 없다 */
}

static void usb_send_extra(report_extra_t *report)
{
#ifdef EXTRAKEY_ENABLE
  /* report_extra_t = { report_id, usage } — 시스템(3) / 컨슈머(4) 둘 다 이 모양 */
  hidExkSendReport((const uint8_t *)report, sizeof(report_extra_t));
#else
  (void)report;
#endif
}

host_driver_t usb_driver = {
  usb_keyboard_leds,
  usb_send_keyboard,
  usb_send_nkro,
  usb_send_mouse,
  usb_send_extra,
};


/*
 * NKRO 는 별도 인터페이스(IF1)라 부트 프로토콜과 무관하게 늘 보낼 수 있다.
 *
 * 보통 QMK 는 "호스트가 report protocol 을 쓸 때만 NKRO" 로 판단하는데, 그건 키보드
 * 인터페이스 하나로 둘을 겸할 때 이야기다. 우리는 IF0(부트) 과 IF1(NKRO) 이 따로
 * 있으므로 BIOS 는 IF0 을 보고 OS 는 IF1 을 본다.
 *
 * 실제로 NKRO 로 나갈지는 keymap_config.nkro (NK_TOGG 로 토글) 가 정한다.
 */
uint8_t keyboard_protocol_get(void)
{
  return 1;
}

bool host_can_send_nkro(void)
{
#ifdef NKRO_ENABLE
  return hidExkIsConfigured();
#else
  return false;
#endif
}
