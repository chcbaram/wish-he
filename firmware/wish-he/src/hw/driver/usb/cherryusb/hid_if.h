#ifndef HID_IF_H_
#define HID_IF_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB


/*
 * raw HID 설정 채널.
 *
 * 리포트 기술자 길이는 기술자 배열을 컴파일 타임에 만들 때 필요하므로 매크로로 둔다.
 * hid_if.c 에서 실제 배열 크기와 일치하는지 _Static_assert 로 잡는다.
 */
#define HID_REPORT_DESC_SIZE      34

/* 명령 (OUT 리포트 [0]) */
#define HID_CMD_INFO              0x01    /* 보드 정보 */
#define HID_CMD_BOOT              0x02    /* 부트로더로 점프 */
#define HID_CMD_RESET             0x03    /* 그냥 리셋 */

/* 응답 상태 (IN 리포트 [1]) */
#define HID_RESP_OK               0x00
#define HID_RESP_UNKNOWN_CMD      0x01
#define HID_RESP_FAIL             0x02


bool     hidIfInit(void);      /* 스택 등록. usbBegin() 에서만 */
void     hidIfUpdate(void);
void     hidIfEventHandler(uint8_t busid, uint8_t event);
bool     hidIfIsConfigured(void);
uint32_t hidIfGetRxCount(void);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
