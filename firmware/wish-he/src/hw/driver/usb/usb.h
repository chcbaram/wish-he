#ifndef USB_H_
#define USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_USB


typedef enum UsbMode
{
  USB_NON_MODE,
  USB_CDC_MODE,
} UsbMode_t;

typedef enum UsbType
{
  USB_CON_CDC = 0,
  USB_CON_CLI = 1,
} UsbType_t;


bool usbInit(void);
bool usbBegin(UsbMode_t usb_mode);
void usbUpdate(void);
bool usbIsOpen(void);
bool usbIsConnect(void);

UsbMode_t usbGetMode(void);
UsbType_t usbGetType(void);


#endif

#ifdef __cplusplus
}
#endif

#endif
