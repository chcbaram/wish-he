#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#include "led.h"
#include "reset.h"
#include "ws2812.h"
#include "keys.h"
#include "flash.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "swtimer.h"
#include "qbuffer.h"
#include "cdc.h"
#include "usb/usb.h"

#include "hpm5361_it.h"


bool hwInit(void);


#ifdef __cplusplus
}
#endif

#endif
