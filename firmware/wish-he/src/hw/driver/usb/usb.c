/*
 * usb.c  —  USB 스택 무관 글루 + 진단/성능 CLI
 *
 * 스택별 초기화는 usbBegin() 안에서만 갈린다.
 * CherryUSB 는 순수 인터럽트 구동이라 별도 펌핑 루프가 필요 없다.
 */

#include "usb/usb.h"


#ifdef _USE_HW_USB

#include "cdc.h"
#include "cli.h"
#include "uart.h"

#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB
#include "usbd_core.h"
#include "hpm_interrupt.h"
#include "usb/cherryusb/cdc_acm_if.h"
#include "usb/cherryusb/hid_if.h"
#include "usb/cherryusb/usb_desc.h"
#endif


static bool      is_init     = false;
static UsbMode_t is_usb_mode = USB_NON_MODE;

#if CLI_USE(HW_USB)
static void cliUsb(cli_args_t *args);
#endif




bool usbInit(void)
{
  is_init = usbBegin(USB_CDC_MODE);

#if CLI_USE(HW_USB)
  cliAdd("usb", cliUsb);
#endif

  logPrintf("[%s] usbInit()\n", is_init ? "OK" : "E_");

  return is_init;
}

bool usbBegin(UsbMode_t usb_mode)
{
  if (usb_mode != USB_CDC_MODE) return false;

#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB
  /*
   * 핀/클럭은 board 계층이 담당한다. DP/DM 풀다운 해제는 board_init() 안에서 이미 끝났다.
   * usbd_initialize() 가 usb_dc_init -> usb_device_init -> usb_dcd_init -> usb_phy_init 까지
   * 밟고 IRQ 활성화와 D+ 풀업(connect)도 스스로 한다.
   */
  board_init_usb(HPM_USB0);

  intc_set_irq_priority(CONFIG_HPM_USBD_IRQn, 2);

  /*
   * 등록 순서가 인터페이스 번호다 (usb_desc.h 의 배치와 반드시 일치해야 한다).
   *   hidIfInit()     -> IF0
   *   cdcIfRegister() -> IF1, IF2
   */
  usbDescRegister(0);
  hidIfInit();
  cdcIfRegister();

  if (usbd_initialize(0, CONFIG_HPM_USBD_BASE, usbDescEventHandler) != 0)
  {
    return false;
  }
#endif

  is_usb_mode = usb_mode;

  return true;
}

/* 메인 루프에서 부른다. HID 로 예약된 리셋을 여기서 실행한다. */
void usbUpdate(void)
{
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB
  hidIfUpdate();
#endif
}

bool usbIsOpen(void)
{
  return cdcIsConnect();
}

bool usbIsConnect(void)
{
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB
  return cdcIfIsConfigured();      /* 케이블 연결 + 열거 완료 */
#else
  return cdcIsConnect();
#endif
}

UsbMode_t usbGetMode(void)
{
  return is_usb_mode;
}

UsbType_t usbGetType(void)
{
  return (UsbType_t)cdcGetType();
}




#if CLI_USE(HW_USB)

/* 무결성 검증용 : 0,1,2,...,255,0,... 증가 시퀀스 */
static void usbFillSeq(uint8_t *p_buf, uint32_t length, uint8_t seq)
{
  for (uint32_t i = 0; i < length; i++)
  {
    p_buf[i] = (uint8_t)(seq + i);
  }
}

void cliUsb(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    while (cliKeepLoop())
    {
      cliPrintf("USB Mode    : %d      \n", usbGetMode());
      cliPrintf("USB Type    : %d      \n", usbGetType());
      cliPrintf("USB Connect : %d      \n", usbIsConnect());
      cliPrintf("USB Open    : %d      \n", usbIsOpen());
      cliPrintf("USB Baud    : %d      \n", (int)cdcGetBaud());
      cliPrintf("HID Ready   : %d      \n", hidIfIsConfigured());
      cliPrintf("HID Rx      : %d      \n", (int)hidIfGetRxCount());
      cliMoveUp(7);
      delay(100);
    }
    cliMoveDown(7);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "tx"))
  {
    static uint8_t buf[512];
    uint32_t pre_time = millis();
    uint32_t sent = 0;
    uint8_t  seq  = 0;

    cliPrintf("usb tx start\n");
    while (cliKeepLoop())
    {
      uint32_t w;

      usbFillSeq(buf, sizeof(buf), seq);
      w = cdcWrite(buf, sizeof(buf));
      seq  += (uint8_t)w;          /* 실제 보낸 만큼만 진행해야 연속성이 유지된다 */
      sent += w;

      if (millis() - pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("tx : %d KB/s\n", (int)(sent / 1024));
        sent = 0;
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "rx"))
  {
    static uint8_t buf[512];
    uint32_t pre_time = millis();
    uint32_t recv = 0;
    uint32_t err_cnt = 0;
    int      exp = -1;

    cliPrintf("usb rx start\n");
    while (cliKeepLoop())
    {
      uint32_t len;

      len = cdcReadBuf(buf, sizeof(buf));
      for (uint32_t i = 0; i < len; i++)
      {
        if (exp >= 0 && buf[i] != (uint8_t)exp) err_cnt++;
        exp = (buf[i] + 1) & 0xFF;    /* 불일치 시 수신값 기준으로 재동기 */
      }
      recv += len;

      if (millis() - pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("rx : %d KB/s, err : %d\n", (int)(recv / 1024), (int)err_cnt);
        recv = 0;
      }
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "duplex"))
  {
    static uint8_t rx_buf[512];
    static uint8_t tx_buf[512];
    uint32_t pre_time = millis();
    uint32_t sent = 0;
    uint32_t recv = 0;
    uint8_t  seq  = 0;

    cliPrintf("usb duplex start\n");
    while (cliKeepLoop())
    {
      uint32_t w;

      recv += cdcReadBuf(rx_buf, sizeof(rx_buf));

      usbFillSeq(tx_buf, sizeof(tx_buf), seq);
      w = cdcWrite(tx_buf, sizeof(tx_buf));
      seq  += (uint8_t)w;
      sent += w;

      if (millis() - pre_time >= 1000)
      {
        pre_time = millis();
        logPrintf("tx : %d KB/s, rx : %d KB/s\n", (int)(sent / 1024), (int)(recv / 1024));
        sent = 0;
        recv = 0;
      }
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usb info\n");
    cliPrintf("usb tx\n");
    cliPrintf("usb rx\n");
    cliPrintf("usb duplex\n");
  }
}
#endif

#endif
