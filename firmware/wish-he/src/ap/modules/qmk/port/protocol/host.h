#pragma once

#include "hw_def.h"
#include "cli.h"

#include QMK_KEYMAP_CONFIG_H

#include "host_driver.h"
#include "qmk/quantum/led.h"


/* host driver */
void           host_set_driver(host_driver_t *driver);
host_driver_t *host_get_driver(void);


/* host driver interface */
uint8_t host_keyboard_leds(void);
led_t   host_keyboard_led_state(void);
void    host_keyboard_send(report_keyboard_t *report);
void    host_nkro_send(report_nkro_t *report);
void    host_mouse_send(report_mouse_t *report);
void    host_system_send(uint16_t usage);
void    host_consumer_send(uint16_t usage);
void    host_programmable_button_send(uint32_t data);

uint16_t host_last_system_usage(void);
uint16_t host_last_consumer_usage(void);

/* NKRO 가능 여부 (report protocol 일 때만). action_util.c 의 send_keyboard_report 가 참조. */
bool host_can_send_nkro(void);

/*
 * 현재 USB 키보드 프로토콜 (1=report/NKRO 가능, 0=boot).
 * stock report.c/action_util.c 가 이 이름을 "변수처럼" 참조하므로(그 파일들은 건드리지
 * 않는다), 가변 전역을 노출하는 대신 접근 함수를 매크로로 연결한다. USB 상태를 실시간으로
 * 읽어 별도 동기화가 필요 없다. 리포트 조립 시에만 호출되어 핫패스가 아니다.
 */
uint8_t keyboard_protocol_get(void);
#define keyboard_protocol (keyboard_protocol_get())