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

/*
 * 키별 설정 — VIA 커스텀 채널로는 못 하는 것.
 *
 *   VIA 는 [채널, 값 ID] 두 바이트뿐이라 키 인덱스를 실을 자리가 없다. 그래서
 *   그쪽은 전역(= 모두 선택)만 다루고, 키를 골라 설정하는 것은 이 명령으로 한다.
 *
 *     읽기  [0] C5  [1] 00  [2] idx           -> 같은 머리 + 값 19바이트
 *     쓰기  [0] C5  [1] 01  [2] idx  [3..] 값 -> 머리만 에코
 *
 *   idx 가 키 수를 넘으면 전 키에 적용한다 — "모두 선택"이 왕복 한 번이 된다.
 */
#define HID_CMD_KEYCFG            0xC5
#define HID_CMD_SWITCH            0xC6    /* 스위치 종류표 — 한 번에 하나 */

/*
 * 스위치 표 응답 배치.
 *
 *   OUT [1] = 인덱스
 *   IN  [1] = 에코   [2] = 전체 개수   [3] = 일반형 개수
 *       [4..5] = 전 행정 (LE16, 0.01mm)   [6..] = 이름 (NUL 종료)
 *
 * ★ 이 표를 도구에 박으면 안 된다. 스위치를 하나 추가하면 번호가 밀리는데
 *   설정은 번호로 저장되므로, 어긋나면 사용자가 고른 것과 다른 스위치가 걸린다.
 *   일반형 개수를 같이 주는 것은 도구가 목록에 구분선을 그으려고다.
 */
#define HID_SWITCH_NAME_OFF       6

#define HID_CMD_CAL               0xC7    /* 보정 — 시작·상태·저장·취소 */

/*
 * 보정.
 *
 *   OUT [1] = 0 상태만  1 시작  2 저장  3 취소
 *   IN  [1] = 에코   [2] = 진행 중(1/0)   [3] = 끝난 키   [4] = 대상 키
 *       [5] = 저장 결과 (2 일 때만: 1 성공)   [6] = 저장 때 건너뛴 키
 *       [8..15] = 키별 완료 비트맵 (64키 = 8바이트, 비트 i = 키 i)
 *
 * ★ 무압 기준값은 여기서 다루지 않는다. 러닝 최대값이 늘 추적하므로 손댈 일이
 *   없고, 여기서 모으는 것은 **바닥값**뿐이다 — 끝까지 눌러야만 알 수 있는 값.
 */
#define HID_CAL_STATUS            0
#define HID_CAL_START             1
#define HID_CAL_SAVE              2
#define HID_CAL_CANCEL            3
#define HID_CAL_MAP_OFF           8
#define HID_KEYCFG_GET            0x00
#define HID_KEYCFG_SET            0x01
#define HID_KEYCFG_OFF            3       /* 값이 시작하는 자리 */
#define HID_KEYCFG_LEN            14      /* 쓰기가 싣는 값 바이트 수 */

#define HID_CMD_INFO              0xC0    /* 보드 정보 */
#define HID_CMD_RESET             0xC1    /* 그냥 리셋 */
#define HID_CMD_LAYOUT            0xC2    /* 물리 배치 읽기 — 페이지 방식 */
#define HID_CMD_TRACK             0xC3    /* 라이브 트래킹 on/off */

/*
 * 레이아웃 응답 배치.
 *   [0] 명령 에코  [1] 시작 인덱스 에코  [2] 개수  [3..] {x,y,w,h,row,col} x 개수
 */
#define HID_LAYOUT_ENTRY          6
#define HID_LAYOUT_OFF            3
#define HID_LAYOUT_PER_FRAME      ((HID_EP_MPS - HID_LAYOUT_OFF) / HID_LAYOUT_ENTRY)


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
