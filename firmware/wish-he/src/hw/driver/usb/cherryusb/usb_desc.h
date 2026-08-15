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
 *   IF1  NKRO · 확장키     리포트 ID 로 NKRO / 시스템 / 컨슈머를 나눈다
 *   IF2  raw HID           VIA 설정 채널 (usage page 0xFF60)
 *   IF3  HE 스트리밍       라이브 트래킹 전용. IN 만 있다
 *   IF4  CDC 제어         ┐ IAD 로 묶인다
 *   IF5  CDC 데이터       ┘
 *
 * ★ 한 번에 늘렸다.
 *
 *   인터페이스를 더할 때마다 CDC 번호가 밀린다. 7편에서 등록 순서 때문에 CDC 가
 *   IF0 을 가져가 기술자와 어긋난 적이 있어, 그 함정을 여러 번 밟지 않으려고
 *   NKRO 와 스트리밍을 같이 넣었다.
 *
 * ★ 스트리밍을 raw HID 에서 뗀 이유.
 *
 *   VIA 는 "명령을 보내면 다음 IN 리포트가 그 응답"이라고 가정한다. 트래킹 프레임이
 *   같은 엔드포인트로 나가면 그 사이에 끼어 짝이 어긋난다. 우리 포크는 태그로 거를
 *   수 있지만 표준 VIA 도구와는 공존이 안 됐다. 그래서 IN 전용 인터페이스를
 *   따로 두고 있다.
 *
 * 호스트 도구는 인터페이스 번호가 아니라 usage page 로 찾으므로, 번호가 밀려도
 * 고칠 것이 없다.
 */
#define USB_IF_KBD          0x00
#define USB_IF_EXK          0x01
#define USB_IF_HID          0x02
#define USB_IF_TRK          0x03
#define USB_IF_CDC          0x04

#define CDC_IN_EP           0x81
#define CDC_OUT_EP          0x01
#define CDC_INT_EP          0x83

#define HID_IN_EP           0x84
#define HID_OUT_EP          0x04
#define HID_EP_MPS          32

/*
 * NKRO · 확장키. 리포트 ID 로 나눠 쓴다 (QMK 의 REPORT_ID_* 와 같은 값).
 *
 *   1 (NKRO)     [0]=ID [1]=모디파이어 [2..31]=비트맵 240비트  -> 32바이트 딱 맞다
 *   3 (SYSTEM)   [0]=ID [1..2]=usage
 *   4 (CONSUMER) [0]=ID [1..2]=usage
 */
#define EXK_IN_EP           0x85
#define EXK_EP_MPS          32
#define EXK_NKRO_BYTES      30                       /* 240키. QMK 의 NKRO_REPORT_BITS 와 같아야 한다 */
#define EXK_NKRO_LEN        (2 + EXK_NKRO_BYTES)     /* ID + 모디파이어 + 비트맵 */

/* HE 라이브 트래킹. 장치가 밀어내기만 하므로 IN 뿐이다. */
#define TRK_IN_EP           0x86
#define TRK_EP_MPS          32

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
