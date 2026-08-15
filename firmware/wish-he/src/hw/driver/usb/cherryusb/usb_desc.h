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
 *   IF0  raw HID    설정 · 부트로더 점프 채널
 *   IF1  CDC 제어  ┐ IAD 로 묶인다
 *   IF2  CDC 데이터┘
 *
 * 7편에서 부트 키보드를 넣으면 그것이 IF0 을 가져가고 나머지가 한 칸씩 밀린다.
 * 호스트 도구는 인터페이스 번호가 아니라 usage page 로 찾으므로 밀려도 무방하다.
 */
#define USB_IF_HID          0x00
#define USB_IF_CDC          0x01

#define CDC_IN_EP           0x81
#define CDC_OUT_EP          0x01
#define CDC_INT_EP          0x83

#define HID_IN_EP           0x84
#define HID_OUT_EP          0x04
#define HID_EP_MPS          32


void usbDescRegister(uint8_t busid);
void usbDescEventHandler(uint8_t busid, uint8_t event);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
