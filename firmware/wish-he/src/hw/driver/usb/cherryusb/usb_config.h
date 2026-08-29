/*
 * usb_config.h  —  CherryUSB 설정 (HPM5361 디바이스 전용)
 *
 * hpm_sdk samples/cherryusb/config/usb_config.h 에서 host / RNDIS / MSC / MTP / AUDIO 등을
 * 걷어내고 CDC-ACM 디바이스에 필요한 것만 남겼다.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CHERRYUSB_CONFIG_H
#define CHERRYUSB_CONFIG_H

#include "hpm_soc.h"
#include "hpm_misc.h"
#include "hpm_soc_feature.h"

void logPrintf(const char *fmt, ...);


/* ---------------------------------------------------------------- 공통 */

/* printf 를 그대로 쓰면 newlib 의 stdio 가 통째로 딸려온다. 프로젝트 로그로 보낸다. */
#define CONFIG_USB_PRINTF(...) logPrintf(__VA_ARGS__)

/* USB_DBG_ERROR / WARNING / INFO / LOG / DEBUG.
   INFO 이상은 플래시를 꽤 먹는다. 문제 추적할 때만 올린다. */
#define CONFIG_USB_DBG_LEVEL USB_DBG_ERROR

/* HPM5361 USB0 는 온칩 UTMI PHY 를 가진 High Speed 컨트롤러다 (MPS 512). */
#ifndef CONFIG_USB_DEVICE_FORCE_FULL_SPEED
#define CONFIG_USB_HS
#endif

/* DLM 은 TCM 이라 D-Cache 대상이 아니다. 따라서 CONFIG_USB_DCACHE_ENABLE 은 켜지 않고
   정렬도 4바이트면 충분하다. */
#define CONFIG_USB_ALIGN_SIZE 4

/* DMA 가 접근하는 버퍼는 .noncacheable.non_init 에 둔다.
   주의: NOLOAD 라 0 초기화되지 않는다. */
#define USB_NOCACHE_RAM_SECTION __attribute__((section(".noncacheable.non_init")))


/* ------------------------------------------------------------ 디바이스 */

/*
 * VID/PID — 자체 키보드 저장소들과 같은 체계를 쓴다.
 * 이미 쓰고 있는 PID : 0x5201~0x5207, 0x5210, 0x5230, 0x5301~0x5303
 * WISH60-HE 는 겹치지 않는 0x5304 를 쓴다.
 */
#define USBD_VID           0x0483
/*
 * PID 는 보드마다 달라야 한다. 같으면 호스트도, 웹 도구도, 우리 도구(flash.py ·
 * dev.py)도 두 보드를 구분하지 못한다. 계열에서 이미 PID 로 가르고 있다.
 *
 *   0x5209  WISH65-74F9
 *   0x5304  WISH60-HE
 *   0x5305  WISH61-HE
 *
 * ★ 벤더 앱(1ca6:300b)과도 달라지므로, 우리 앱이 뜬 뒤에는 tools/flash.py 의
 *   AppToBoot 가 보드를 못 찾는다. 앱에 부트로더로 돌아가는 길을 반드시 둘 것.
 *   (App1 헤더 페이지를 지우고 0xA9B8C7D6 을 쓴 뒤 PPOR 리셋 — README.md 4절)
 */
#if defined(HW_BOARD_WISH61_HE)
#define USBD_PID           0x5305      /* WISH61-HE */
#else
#define USBD_PID           0x5304      /* WISH60-HE */
#endif
#define USBD_MAX_POWER     500      /* bMaxPower 0xFA. WS2812 83개 구동분 포함 */
#define USBD_LANGID_STRING 1033

#define CONFIG_USBDEV_REQUEST_BUFFER_LEN 512

/* 설정 기술자를 매번 만들지 않고 캐시해 EP0 응답을 빠르게 한다 */
#define CONFIG_USBDEV_DESC_CHECK

#define CONFIG_USBDEV_MAX_BUS 1


/* --------------------------------------------------------------- HPM 포트 */

#define CONFIG_HPM_USBD_BASE HPM_USB0_BASE
#define CONFIG_HPM_USBD_IRQn IRQn_USB0

/* HPM5361 은 단일 코어 + DLM 직접 접근이라 항등 변환이지만, 포트 코드가 호출하므로 정의는 필요하다 */
#define usb_phyaddr2ramaddr(addr) core_local_mem_to_sys_address(0, (addr))
#define usb_ramaddr2phyaddr(addr) sys_address_to_core_local_mem(0, (addr))

#endif /* CHERRYUSB_CONFIG_H */
