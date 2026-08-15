#ifndef USB_DESC_H_
#define USB_DESC_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB


/*
 * 인터페이스 배치
 *
 *   IF0  HID 부트 키보드   ★ 반드시 첫 번째 — 일부 BIOS 가 IF0 만 본다
 *   IF1  raw HID           설정 · 부트로더 점프 채널 (usage page 0xFF60)
 *   IF2  CDC 제어         ┐ IAD 로 묶인다
 *   IF3  CDC 데이터       ┘
 *
 * raw HID 가 IF0 -> IF1 로 밀렸지만 호스트 도구는 인터페이스 번호가 아니라
 * usage page 로 찾으므로 고칠 것이 없다.
 */
#define USB_IF_KBD          0x00
#define USB_IF_HID          0x01
#define USB_IF_CDC          0x02

#define CDC_IN_EP           0x81
#define CDC_OUT_EP          0x01
#define CDC_INT_EP          0x83

#define HID_IN_EP           0x84
#define HID_OUT_EP          0x04
#define HID_EP_MPS          32

/*
 * 부트 키보드. 리포트는 8바이트 고정 —
 *   [0] 모디파이어  [1] 예약  [2..7] 눌린 키 6개 (6키 롤오버)
 */
#define KBD_IN_EP           0x82
#define KBD_EP_MPS          8
#define KBD_REPORT_LEN      8
#define KBD_ROLLOVER        6


void usbDescRegister(uint8_t busid);
void usbDescEventHandler(uint8_t busid, uint8_t event);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
