/*
 * usb_desc.c  —  복합 장치 기술자 (raw HID + CDC-ACM)
 *
 * 기술자는 인터페이스 번호와 엔드포인트를 한 곳에서 봐야 어긋나지 않는다.
 * 클래스별 글루(cdc_acm_if.c / hid_if.c)는 자기 동작만 맡고 기술자는 여기 모은다.
 *
 * 등록 순서가 곧 인터페이스 번호다 — usbd_add_interface() 가 부른 순서대로
 * 0, 1, 2 를 매긴다. 그래서 usbInit() 에서 HID 를 먼저 등록해야 아래 기술자와 맞는다.
 */

#include "usb/cherryusb/usb_desc.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_cdc_acm.h"
#include "usb_hid.h"
#include "usb/cherryusb/cdc_acm_if.h"
#include "usb/cherryusb/hid_if.h"
#include "usb/cherryusb/hid_kbd_if.h"


#define USB_CONFIG_SIZE   (9 + HID_KEYBOARD_DESCRIPTOR_LEN \
                         + HID_CUSTOM_INOUT_DESCRIPTOR_LEN + CDC_ACM_DESCRIPTOR_LEN)

/*
 * 인터럽트 엔드포인트 폴링 주기. HS 는 2^(n-1) 마이크로프레임, FS 는 ms 단위다.
 *
 * 키보드는 1 = 125us = 8kHz — 상용 보드와 같다.
 *
 * ★ 설정 채널도 1 이다. 처음에는 "설정이 빠를 이유가 없다"고 4(=1ms)로 뒀는데,
 *   라이브 트래킹을 얹으면서 바꿨다. 64키를 32바이트 리포트에 나눠 실으면 스냅샷
 *   하나가 10프레임이라, 1ms 폴링에서는 스냅샷 주기가 10ms 가 된다. 빠른 타건은
 *   4mm 를 10ms 안에 지나가므로 스트로크 전체에서 표본이 한두 개뿐이다.
 *   125us 폴링이면 스냅샷 1.25ms — 래피드 트리거 파형을 볼 수 있다.
 *
 *   MPS 는 32 를 유지한다. VIA 규약이라 늘리면 호환이 깨진다.
 */
#define KBD_INTERVAL_HS   1       /* 125us = 8kHz */
#define KBD_INTERVAL_FS   1       /* 1ms — FS 의 하한 */
#define HID_INTERVAL_HS   1       /* 125us — 라이브 트래킹 때문에 (위 설명) */
#define HID_INTERVAL_FS   1       /* 1ms */


static const uint8_t device_descriptor[] = {
  USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
  USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_KEYBOARD_DESCRIPTOR_INIT(USB_IF_KBD, 0x01, KBD_REPORT_DESC_SIZE,
                               KBD_IN_EP, KBD_EP_MPS, KBD_INTERVAL_HS),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(USB_IF_HID, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_HS),
  CDC_ACM_DESCRIPTOR_INIT(USB_IF_CDC, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

static const uint8_t config_descriptor_fs[] = {
  USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_KEYBOARD_DESCRIPTOR_INIT(USB_IF_KBD, 0x01, KBD_REPORT_DESC_SIZE,
                               KBD_IN_EP, KBD_EP_MPS, KBD_INTERVAL_FS),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(USB_IF_HID, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_FS),
  CDC_ACM_DESCRIPTOR_INIT(USB_IF_CDC, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

static const uint8_t device_quality_descriptor[] = {
  USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

/* other-speed 는 "지금 속도의 반대쪽"을 기술한다. HS 로 동작 중이면 FS 규격을 담는 게 맞다. */
static const uint8_t other_speed_config_descriptor_hs[] = {
  USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_KEYBOARD_DESCRIPTOR_INIT(USB_IF_KBD, 0x01, KBD_REPORT_DESC_SIZE,
                               KBD_IN_EP, KBD_EP_MPS, KBD_INTERVAL_FS),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(USB_IF_HID, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_FS),
  CDC_ACM_DESCRIPTOR_INIT(USB_IF_CDC, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

static const uint8_t other_speed_config_descriptor_fs[] = {
  USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x04, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_KEYBOARD_DESCRIPTOR_INIT(USB_IF_KBD, 0x01, KBD_REPORT_DESC_SIZE,
                               KBD_IN_EP, KBD_EP_MPS, KBD_INTERVAL_HS),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(USB_IF_HID, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_HS),
  CDC_ACM_DESCRIPTOR_INIT(USB_IF_CDC, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

static const char *string_descriptors[] = {
  (const char[]){ 0x09, 0x04 },   /* LangID : en-US */
  "HPMicro",
  _DEF_BOARD_NAME,
  "0001",
};


static const uint8_t *device_descriptor_callback(uint8_t speed)
{
  (void)speed;
  return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
  if (speed == USB_SPEED_HIGH) return config_descriptor_hs;
  if (speed == USB_SPEED_FULL) return config_descriptor_fs;
  return NULL;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
  (void)speed;
  return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
  if (speed == USB_SPEED_HIGH) return other_speed_config_descriptor_hs;
  if (speed == USB_SPEED_FULL) return other_speed_config_descriptor_fs;
  return NULL;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
  (void)speed;
  if (index >= (sizeof(string_descriptors)/sizeof(string_descriptors[0]))) return NULL;
  return string_descriptors[index];
}

static const struct usb_descriptor usb_descriptor_set =
{
  .device_descriptor_callback         = device_descriptor_callback,
  .config_descriptor_callback         = config_descriptor_callback,
  .device_quality_descriptor_callback = device_quality_descriptor_callback,
  .other_speed_descriptor_callback    = other_speed_config_descriptor_callback,
  .string_descriptor_callback         = string_descriptor_callback,
};


void usbDescRegister(uint8_t busid)
{
  usbd_desc_register(busid, &usb_descriptor_set);
}

/* 스택 이벤트는 클래스 글루 전부에게 뿌린다. */
void usbDescEventHandler(uint8_t busid, uint8_t event)
{
  hidKbdEventHandler(busid, event);
  hidIfEventHandler(busid, event);
  cdcIfEventHandler(busid, event);
}

#endif
#endif
