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
  (void)report;
}

static void usb_send_mouse(report_mouse_t *report)
{
  (void)report;
}

static void usb_send_extra(report_extra_t *report)
{
  (void)report;
}

host_driver_t usb_driver = {
  usb_keyboard_leds,
  usb_send_keyboard,
  usb_send_nkro,
  usb_send_mouse,
  usb_send_extra,
};


/* 부트 프로토콜만 쓴다. NKRO 인터페이스가 생기면 여기가 바뀐다. */
uint8_t keyboard_protocol_get(void)
{
  return 0;
}

bool host_can_send_nkro(void)
{
  return false;
}
