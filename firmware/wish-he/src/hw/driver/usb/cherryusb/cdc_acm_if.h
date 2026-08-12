#ifndef CDC_ACM_IF_H_
#define CDC_ACM_IF_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_CDC
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB


bool     cdcIfInit(void);
void     cdcIfEventHandler(uint8_t busid, uint8_t event);   /* usbd_initialize() 에 넘긴다 */
bool     cdcIfIsConfigured(void);   /* 케이블 연결 + 열거 완료 */
bool     cdcIfIsConnected(void);    /* 위 + 호스트가 포트 오픈(DTR) */
uint32_t cdcIfAvailable(void);
uint8_t  cdcIfRead(void);
uint32_t cdcIfReadBuf(uint8_t *p_data, uint32_t length);
uint32_t cdcIfWrite(uint8_t *p_data, uint32_t length);
uint32_t cdcIfGetBaud(void);
uint8_t  cdcIfGetType(void);


#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
