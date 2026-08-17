/*
 * cdc.c  —  USB CDC 스택 디스패처
 *
 * 이 파일이 USB 스택 추상화의 유일한 seam 이다.
 * 위쪽(uart.c / cli / log)은 어떤 스택이 붙어 있는지 전혀 모른다.
 * 스택 추가는 여기에 #elif 블록 하나를 더하는 것으로 끝난다.
 */

#include "cdc.h"


#ifdef _USE_HW_CDC

#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB
#include "usb/cherryusb/cdc_acm_if.h"
#else
#include "usb/tinyusb/cdc_acm_if.h"
#endif


static bool is_init = false;


bool cdcInit(void)
{
  is_init = cdcIfInit();
  return is_init;
}

bool cdcIsInit(void)
{
  return is_init;
}

bool cdcIsConnect(void)
{
  return cdcIfIsConnected();
}

uint32_t cdcAvailable(void)
{
  return cdcIfAvailable();
}

uint8_t cdcRead(void)
{
  return cdcIfRead();
}

uint32_t cdcReadBuf(uint8_t *p_data, uint32_t length)
{
  return cdcIfReadBuf(p_data, length);
}

uint32_t cdcWrite(uint8_t *p_data, uint32_t length)
{
  return cdcIfWrite(p_data, length);
}

uint32_t cdcGetBaud(void)
{
  return cdcIfGetBaud();
}

uint8_t cdcGetType(void)
{
  return cdcIfGetType();
}

#endif
