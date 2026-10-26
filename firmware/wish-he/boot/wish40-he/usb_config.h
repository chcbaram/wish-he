/*
 * usb_config.h — CherryUSB 설정 (부트로더). 본 펌웨어 것에서 HID 하나에 필요한 만큼만.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include "hpm_soc.h"
#include "hpm_misc.h"
#include "hpm_soc_feature.h"


/* 부트로더에는 로그 출력 길이 없다. 문자열을 끌고 오지 않게 비운다 */
#define CONFIG_USB_PRINTF(...)  do { } while (0)
#define CONFIG_USB_DBG_LEVEL    USB_DBG_ERROR

#define CONFIG_USB_HS
#define CONFIG_USB_ALIGN_SIZE   4
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".noncacheable.non_init")))

/* 0483 아래 계열 번호 — 5307 앱 / 5308 부트로더 (docs/wish40-he.md) */
#define USBD_VID                0x0483
#define USBD_PID                0x5308
#define USBD_MAX_POWER          100
#define USBD_LANGID_STRING      1033

#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 256
#define CONFIG_USBDEV_MAX_BUS   1

#define CONFIG_HPM_USBD_BASE    HPM_USB0_BASE
#define CONFIG_HPM_USBD_IRQn    IRQn_USB0

#define usb_phyaddr2ramaddr(addr) core_local_mem_to_sys_address(0, (addr))
#define usb_ramaddr2phyaddr(addr) sys_address_to_core_local_mem(0, (addr))

#endif
