#ifndef HID_EXK_IF_H_
#define HID_EXK_IF_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB


/*
 * NKRO · 확장키 인터페이스.
 *
 * 부트 키보드(IF0)와 같이 존재한다. BIOS 는 부트 프로토콜만 알아서 그쪽을 없앨 수
 * 없고, 호스트가 report protocol 을 쓸 때만 이쪽으로 보낸다.
 */

/* 리포트 기술자 길이. hid_exk_if.c 에서 _Static_assert 로 실제 크기와 맞춘다. */
#define EXK_REPORT_DESC_SIZE      164

/* QMK 의 REPORT_ID_* 와 같은 값이어야 한다 */
#define EXK_REPORT_ID_NKRO        0x06
#define EXK_REPORT_ID_SYSTEM      0x03
#define EXK_REPORT_ID_CONSUMER    0x04
#define EXK_REPORT_ID_MOUSE       0x02


bool     hidExkInit(void);      /* 스택 등록. usbBegin() 에서만 */
void     hidExkEventHandler(uint8_t busid, uint8_t event);
void     hidExkSendReport(const uint8_t *p_report, uint32_t length);
bool     hidExkIsConfigured(void);
uint32_t hidExkGetSentCount(void);
uint32_t hidExkGetSentCountOf(uint8_t report_id);   /* EXK_REPORT_ID_* 별 전송 횟수 */
uint32_t hidExkGetLostCount(void);
void     hidExkUpdate(void);        /* 메인 루프에서 — 놓친 전송 완료를 되살린다 */


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
