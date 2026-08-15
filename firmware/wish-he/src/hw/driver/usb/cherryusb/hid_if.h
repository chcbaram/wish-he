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


/*
 * ★ 명령 ID 는 VIA 를 피해서 잡는다.
 *
 *   처음에는 0x01 INFO / 0x02 BOOT / 0x03 RESET 이었는데, VIA 규약과 정면으로
 *   겹친다 — 0x01 은 id_get_protocol_version, 0x02 는 id_get_keyboard_value,
 *   0x03 은 id_set_keyboard_value 다. 11편에서 QMK 의 via.c 를 얹는 순간 같은
 *   채널을 두 주인이 쓰게 되므로 미리 옮겼다.
 *
 *   부트로더 점프는 VIA 에 이미 자리가 있다(id_bootloader_jump = 0x0B). 그걸 쓰면
 *   나중에 표준 VIA 도구로도 진입시킬 수 있다. 우리만의 확장은 VIA 가 쓰지 않는
 *   0xC0 대에 모았다.
 */
#define HID_CMD_BOOT              0x0B    /* VIA id_bootloader_jump 와 같은 자리 */

#define HID_CMD_INFO              0xC0    /* 보드 정보 */
#define HID_CMD_RESET             0xC1    /* 그냥 리셋 */
#define HID_CMD_LAYOUT            0xC2    /* 물리 배치 읽기 — 페이지 방식 */
#define HID_CMD_TRACK             0xC3    /* 라이브 트래킹 on/off */

/* 장치가 스스로 내보내는 프레임의 태그 (IN 리포트 [0]) */
#define HID_EVT_TRACK             0xC4    /* 트래킹 스냅샷 조각 */

/* 응답 상태 (IN 리포트 [1]) */
#define HID_RESP_OK               0x00
#define HID_RESP_UNKNOWN_CMD      0x01
#define HID_RESP_FAIL             0x02

/*
 * 한 프레임에 담는 키 수. 32바이트 리포트에서 헤더 4바이트를 빼면 28바이트고,
 * 키당 4바이트(원시값 + 깊이)라 7개다. 64키면 10프레임이 한 스냅샷이 된다.
 */
#define HID_TRACK_HDR             4
#define HID_TRACK_PER_FRAME       ((HID_EP_MPS - HID_TRACK_HDR) / 4)

/* 레이아웃 한 항목은 {x, y, w, h, row, col} 6바이트 */
#define HID_LAYOUT_ENTRY          6
#define HID_LAYOUT_PER_FRAME      ((HID_EP_MPS - HID_TRACK_HDR) / HID_LAYOUT_ENTRY)


/*
 * 우리가 모르는 명령을 넘겨줄 곳 (QMK/VIA).
 *
 * ★ ISR 이 아니라 메인 루프에서 불린다. VIA 명령은 EEPROM 을 건드리므로 인터럽트
 *   컨텍스트에서 돌리면 안 된다. 그래서 OUT 콜백은 복사만 하고 hidIfUpdate() 가
 *   실행한다.
 */
typedef void (*hid_raw_recv_t)(uint8_t *p_data, uint8_t length);

void     hidIfSetRawReceiveFunc(hid_raw_recv_t func);
void     hidIfSendRaw(const uint8_t *p_data, uint8_t length);

bool     hidIfInit(void);      /* 스택 등록. usbBegin() 에서만 */
void     hidIfUpdate(void);
void     hidIfEventHandler(uint8_t busid, uint8_t event);
bool     hidIfIsConfigured(void);
bool     hidIfIsTracking(void);
uint32_t hidIfGetRxCount(void);
uint32_t hidIfGetTrackCount(void);


#endif
#endif


#ifdef __cplusplus
}
#endif

#endif
