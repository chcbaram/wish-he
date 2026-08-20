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
 * 호스트가 잠들어 있는가 (USBD_EVENT_SUSPEND / RESUME).
 *
 * qmk.c 의 idle 처리가 이 값의 변화를 보고 suspend_power_down() 을 부른다.
 * 상류 QMK 는 프로토콜 계층이 하는 일인데 우리 포트에는 그 계층이 없다.
 */
bool     hidKbdIsSuspended(void);

/*
 * 보낼 리포트를 갱신한다. 실제 전송은 완료 콜백이 알아서 이어간다.
 *
 * 스캔 주기와 리포트 주기를 분리하기 위한 구조다 — 스캔이 빠르든 느리든
 * 호스트가 물어볼 때마다 "그 순간의 최신 상태"가 나간다.
 */
void     hidKbdSetReport(uint8_t modifier, const uint8_t *keys, uint32_t cnt);

/* 8바이트 부트 리포트를 그대로. QMK 의 report_keyboard_t 배치와 같다. */
void     hidKbdSetReportRaw(const uint8_t *p_report);

/*
 * 지금 나가 있는 리포트를 그대로 읽는다 (KBD_REPORT_LEN 바이트).
 *
 * "화면에 찍히는 글자가 이상하다"를 좁힐 때 쓴다. 스캔·매트릭스·키맵이 다 맞는데
 * 글자가 다르면 남는 건 이 바이트뿐이라, 여기서 갈라야 장치 문제인지 호스트
 * 해석 문제인지 정해진다.
 */
void     hidKbdGetReportRaw(uint8_t *p_report);

/* 호스트가 켠 LED 비트 (Caps/Num/Scroll) */
uint8_t  hidKbdGetLeds(void);

uint32_t hidKbdGetSentCount(void);
uint32_t hidKbdGetLostCount(void);
void     hidKbdUpdate(void);        /* 메인 루프에서 — 놓친 전송 완료를 되살린다 */
uint8_t  hidKbdGetProtocol(void);   /* 0 = 부트, 1 = 리포트. 호스트가 정한다 */
void     hidKbdSetProtocol(uint8_t protocol);   /* 시험용 — 호스트 요청을 흉내 낸다 */

/*
 * 폴링 주기 측정 모드. 일부러 매번 재무장해 "폴링마다 전송"을 만들고
 * 완료 사이 간격을 히스토그램으로 모은다. 평소에는 꺼둔다.
 */
void     hidKbdPollTest(bool on);
uint32_t hidKbdGetPollMin(void);
uint32_t hidKbdGetPollMax(void);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
