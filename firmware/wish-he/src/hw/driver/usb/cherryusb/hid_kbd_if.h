#ifndef HID_KBD_IF_H_
#define HID_KBD_IF_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"
#include "usb/cherryusb/usb_desc.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB


/* 부트 키보드 리포트 기술자 길이. usb_desc.c 가 컴파일 타임에 필요로 한다. */
#define KBD_REPORT_DESC_SIZE      63


bool     hidKbdInit(void);      /* 스택 등록. usbBegin() 에서만 */
void     hidKbdEventHandler(uint8_t busid, uint8_t event);
bool     hidKbdIsConfigured(void);

/*
 * 보낼 리포트를 갱신한다. 실제 전송은 완료 콜백이 알아서 이어간다.
 *
 * 스캔 주기와 리포트 주기를 분리하기 위한 구조다 — 스캔이 빠르든 느리든
 * 호스트가 물어볼 때마다 "그 순간의 최신 상태"가 나간다.
 */
void     hidKbdSetReport(uint8_t modifier, const uint8_t *keys, uint32_t cnt);

uint32_t hidKbdGetSentCount(void);

/*
 * 폴링 주기 측정 모드. 일부러 매번 재무장해 "폴링마다 전송"을 만들고
 * 완료 사이 간격을 히스토그램으로 모은다. 평소에는 꺼둔다.
 */
void     hidKbdPollTest(bool on);
uint32_t hidKbdGetPollMin(void);
uint32_t hidKbdGetPollMax(void);

/* SOF 카운트 — 버스 마이크로프레임. HS 면 8000/s 가 나와야 한다. */
uint32_t hidKbdGetSofCount(void);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
