/*
 * QMK host layer — 순정(upstream) 드라이버 디스패치 방식.
 *
 * baram 포크는 이 파일에 usbHidSendReport() 직접호출을 박아 QMK 드라이버 추상화를
 * 우회했으나, 이 프로젝트는 원본 방식으로 되돌린다: 전송은 전적으로
 * host_driver_t(현재 활성 드라이버)의 함수 포인터로만 이뤄진다.
 * (qmk-zephyr 의 port/protocol/host.c 를 M483 용으로 이식)
 *
 * USB 출력 연결은 port/driver_usb.c 의 usb_driver 가, 전환은 host_set_driver() 가 한다.
 */

#include <stdint.h>
#include "host.h"
#include "report.h"

#ifdef NKRO_ENABLE
#include "keycode_config.h"
extern keymap_config_t keymap_config;
#endif

static host_driver_t *driver;
static uint16_t       last_system_usage   = 0;
static uint16_t       last_consumer_usage = 0;

void host_set_driver(host_driver_t *d)
{
  driver = d;
}

host_driver_t *host_get_driver(void)
{
  return driver;
}

uint8_t host_keyboard_leds(void)
{
  if (!driver) return 0;
  return (*driver->keyboard_leds)();
}

led_t host_keyboard_led_state(void)
{
  return (led_t)host_keyboard_leds();
}

void host_keyboard_send(report_keyboard_t *report)
{
  if (!driver) return;
#ifdef KEYBOARD_SHARED_EP
  report->report_id = REPORT_ID_KEYBOARD;
#endif
  (*driver->send_keyboard)(report);
}

void host_nkro_send(report_nkro_t *report)
{
  if (!driver) return;
  report->report_id = REPORT_ID_NKRO;
  (*driver->send_nkro)(report);
}

void host_mouse_send(report_mouse_t *report)
{
  if (!driver) return;
#ifdef MOUSE_SHARED_EP
  report->report_id = REPORT_ID_MOUSE;
#endif
  (*driver->send_mouse)(report);
}

void host_system_send(uint16_t usage)
{
  if (usage == last_system_usage) return;
  last_system_usage = usage;

  if (!driver) return;
  report_extra_t report = {
    .report_id = REPORT_ID_SYSTEM,
    .usage     = usage,
  };
  (*driver->send_extra)(&report);
}

void host_consumer_send(uint16_t usage)
{
  if (usage == last_consumer_usage) return;
  last_consumer_usage = usage;

  if (!driver) return;
  report_extra_t report = {
    .report_id = REPORT_ID_CONSUMER,
    .usage     = usage,
  };
  (*driver->send_extra)(&report);
}

/* PROGRAMMABLE_BUTTON 미사용 — host.h 선언 충족용 스텁 */
void host_programmable_button_send(uint32_t data)
{
  (void)data;
}

uint16_t host_last_system_usage(void)
{
  return last_system_usage;
}

uint16_t host_last_consumer_usage(void)
{
  return last_consumer_usage;
}
