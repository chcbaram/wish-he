/*
 * 부트로더 USB — HID 인터페이스 하나 (usage page 0xFF53, 64B IN/OUT, 리포트 ID 없음).
 *
 * wish60 부트로더는 키보드 · VIA · 마우스까지 넷을 띄웠지만 호스트 도구는 셋 다 안 본다.
 * iap_update.py 는 usage page 로, 웹앱은 VID/PID + usage page 로 찾는다 (09편).
 * 쓰지 않는 인터페이스는 틀릴 자리만 늘린다.
 *
 * ★ 처리는 메인 루프에서 한다. 플래시 소거는 인터럽트를 막고 수십 ms 를 쓴다 —
 *   ISR 안에서 하면 그동안 USB 가 통째로 멈춘다. OUT 을 받으면 재무장하지 않고
 *   들고만 있는다. 컨트롤러가 다음 OUT 을 NAK 하므로 호스트는 그냥 기다린다
 *   (본 펌웨어 hid_if.c 의 흐름제어와 같다).
 */
#include <string.h>
#include "board.h"
#include "usbd_core.h"
#include "usbd_hid.h"
#include "hpm_usb_drv.h"
#include "boot.h"


#define HID_BUSID         0
#define HID_IN_EP         0x81
#define HID_OUT_EP        0x01
#define HID_EP_MPS        64
#define HID_INTERVAL_HS   4           /* 1ms — 굽기에는 충분하다 */
#define HID_INTERVAL_FS   1

#define HID_REPORT_DESC_SIZE  34
#define USB_CONFIG_SIZE       (9 + HID_CUSTOM_INOUT_DESCRIPTOR_LEN)


static const uint8_t hid_report_desc[] = {
  0x06, 0x53, 0xFF,         /* Usage Page (Vendor Defined 0xFF53) — 호스트가 이걸로 찾는다 */
  0x09, 0x01,               /* Usage (0x01)                        */
  0xA1, 0x01,               /* Collection (Application)            */
  0x09, 0x02,               /*   Usage (0x02)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, HID_EP_MPS,         /*   Report Count (64)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x81, 0x02,               /*   Input (Data, Var, Abs)            */
  0x09, 0x03,               /*   Usage (0x03)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, HID_EP_MPS,         /*   Report Count (64)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x91, 0x02,               /*   Output (Data, Var, Abs)           */
  0xC0                      /* End Collection                      */
};

_Static_assert(sizeof(hid_report_desc) == HID_REPORT_DESC_SIZE,
               "HID_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");

static const uint8_t device_descriptor[] = {
  USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
  USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(0x00, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_HS),
};

static const uint8_t config_descriptor_fs[] = {
  USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(0x00, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_FS),
};

static const uint8_t device_quality_descriptor[] = {
  USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
  USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(0x00, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_FS),
};

static const uint8_t other_speed_config_descriptor_fs[] = {
  USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x01, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
  HID_CUSTOM_INOUT_DESCRIPTOR_INIT(0x00, 0x00, HID_REPORT_DESC_SIZE,
                                   HID_OUT_EP, HID_IN_EP, HID_EP_MPS, HID_INTERVAL_HS),
};

static const char *string_descriptors[] = {
  (const char[]){ 0x09, 0x04 },
  "BARAM",
  "WISH40-HE Boot",
  "0001",
};


USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t rx_report[HID_EP_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[HID_EP_MPS];

static struct usbd_interface hid_intf;
static volatile bool is_configured = false;
static volatile bool is_rx_pending = false;


static const uint8_t *deviceDescCallback(uint8_t speed);
static const uint8_t *configDescCallback(uint8_t speed);
static const uint8_t *qualifierDescCallback(uint8_t speed);
static const uint8_t *otherSpeedDescCallback(uint8_t speed);
static const char    *stringDescCallback(uint8_t speed, uint8_t index);
static void           usbEventHandler(uint8_t busid, uint8_t event);
static void           hidOutCallback(uint8_t busid, uint8_t ep, uint32_t nbytes);
static void           hidInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes);

static const struct usb_descriptor usb_descriptor_set = {
  .device_descriptor_callback         = deviceDescCallback,
  .config_descriptor_callback         = configDescCallback,
  .device_quality_descriptor_callback = qualifierDescCallback,
  .other_speed_descriptor_callback    = otherSpeedDescCallback,
  .string_descriptor_callback         = stringDescCallback,
};

static struct usbd_endpoint hid_out_ep = { .ep_addr = HID_OUT_EP, .ep_cb = hidOutCallback };
static struct usbd_endpoint hid_in_ep  = { .ep_addr = HID_IN_EP,  .ep_cb = hidInCallback  };




bool usbBootInit(void)
{
  board_init_usb(HPM_USB0);

  /*
   * ★ LQFP64 에는 VBUS 감지 핀이 없다 — PHY 에 "VBUS 유효" 를 직접 세운다.
   *
   *   안 하면 컨트롤러가 세션이 없다고 보고(OTGSC.BSV=0, BSE=1) D+ 풀업을 안 건다.
   *   호스트에는 아무것도 안 보이고, 펌웨어는 멀쩡히 돈다. wish60 · wish61 은
   *   LQFP100 이라 VBUS 핀이 있어 이 줄 없이 됐다. SDK EVK 보드 코드에도 주석으로만
   *   남아 있다. 버스 전원 장치라 케이블을 뽑으면 전원이 같이 꺼지므로 잃는 것이 없다.
   */
  usb_phy_using_internal_vbus(HPM_USB0);

  intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);

  usbd_desc_register(HID_BUSID, &usb_descriptor_set);
  usbd_add_interface(HID_BUSID,
                     usbd_hid_init_intf(HID_BUSID, &hid_intf,
                                        hid_report_desc, sizeof(hid_report_desc)));
  usbd_add_endpoint(HID_BUSID, &hid_out_ep);
  usbd_add_endpoint(HID_BUSID, &hid_in_ep);

  return usbd_initialize(HID_BUSID, CONFIG_HPM_USBD_BASE, usbEventHandler) == 0;
}

bool usbBootRecv(uint8_t *buf)
{
  if (is_rx_pending == false) return false;

  memcpy(buf, rx_report, HID_EP_MPS);
  return true;
}

void usbBootSend(const uint8_t *buf)
{
  /* 요청 하나에 응답 하나다 — 응답을 내보내고 나서 다음 요청을 받는다 */
  memcpy(tx_report, buf, HID_EP_MPS);
  is_rx_pending = false;
  if (is_configured)
  {
    usbd_ep_start_write(HID_BUSID, HID_IN_EP, tx_report, HID_EP_MPS);
    usbd_ep_start_read(HID_BUSID, HID_OUT_EP, rx_report, HID_EP_MPS);
  }
}

static void hidOutCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;
  is_rx_pending = true;               /* 재무장은 usbBootSend() 가 한다 */
}

static void hidInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)busid;
  (void)ep;
  (void)nbytes;
}

static void usbEventHandler(uint8_t busid, uint8_t event)
{
  switch (event)
  {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      is_configured = false;
      is_rx_pending = false;
      break;

    case USBD_EVENT_CONFIGURED:
      is_configured = true;
      is_rx_pending = false;
      usbd_ep_start_read(busid, HID_OUT_EP, rx_report, HID_EP_MPS);
      break;

    default:
      break;
  }
}

static const uint8_t *deviceDescCallback(uint8_t speed)
{
  (void)speed;
  return device_descriptor;
}

static const uint8_t *configDescCallback(uint8_t speed)
{
  if (speed == USB_SPEED_HIGH) return config_descriptor_hs;
  if (speed == USB_SPEED_FULL) return config_descriptor_fs;
  return NULL;
}

static const uint8_t *qualifierDescCallback(uint8_t speed)
{
  (void)speed;
  return device_quality_descriptor;
}

static const uint8_t *otherSpeedDescCallback(uint8_t speed)
{
  if (speed == USB_SPEED_HIGH) return other_speed_config_descriptor_hs;
  if (speed == USB_SPEED_FULL) return other_speed_config_descriptor_fs;
  return NULL;
}

static const char *stringDescCallback(uint8_t speed, uint8_t index)
{
  (void)speed;
  if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) return NULL;
  return string_descriptors[index];
}
