#ifndef HID_TRK_IF_H_
#define HID_TRK_IF_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usb/cherryusb/usb_desc.h"


/*
 * HE 라이브 트래킹 — IN 전용 채널 (usage page 0xFF61).
 *
 * 켜고 끄는 것은 설정 채널(raw HID)의 0xC3 명령으로 한다. 여기는 내보내기만 한다.
 */

/* 리포트 기술자 길이. hid_trk_if.c 에서 _Static_assert 로 실제 크기와 맞춘다. */
#define TRK_REPORT_DESC_SIZE      21

#define TRK_TAG                   0xC4   /* 프레임 [0] */

/*
 * 한 프레임에 담는 키 수. 32바이트 리포트에서 헤더 4바이트를 빼면 28바이트고,
 * 키당 4바이트(원시값 + 깊이)라 7개다. 64키면 10프레임이 한 스냅샷이 된다.
 */
#define TRK_HDR                   4
#define TRK_PER_FRAME             ((TRK_EP_MPS - TRK_HDR) / 4)


bool     hidTrkInit(void);      /* 스택 등록. usbBegin() 에서만 */
void     hidTrkUpdate(void);    /* 메인 루프에서. 켜져 있으면 프레임을 내보낸다 */
void     hidTrkEventHandler(uint8_t busid, uint8_t event);
void     hidTrkSetEnable(bool on);
bool     hidTrkIsEnabled(void);
uint32_t hidTrkGetFrameCount(void);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
