/*
 * board.h
 *
 * hpm_sdk boards/hpm5300evk/board.h 에서 본 프로젝트에 필요한 부분만 추린 것이다.
 * hpm_sdk 에 빌드 의존하지 않으므로 보드 계층은 bsp 가 직접 소유한다.
 *
 * Copyright (c) 2023-2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef BOARD_H
#define BOARD_H

#include <stdio.h>
#include <stdarg.h>

#include "hpm_common.h"
#include "hpm_clock_drv.h"
#include "hpm_soc.h"
#include "hpm_soc_feature.h"
#include "hpm_iomux.h"
#include "hpm_pmic_iomux.h"


#define BOARD_NAME          "hpm5300evk"

#ifndef BOARD_RUNNING_CORE
#define BOARD_RUNNING_CORE  HPM_CORE0
#endif


/* on-board QSPI NOR flash (XIP) */
#define BOARD_FLASH_BASE_ADDRESS (0x80000000UL)
#define BOARD_FLASH_SIZE         (SIZE_1MB)


/*
 * 콘솔 UART : UART0  PA00=TXD / PA01=RXD
 * 온보드 FT2232 디버거의 VCP 로 연결된다.
 */
#define BOARD_CONSOLE_UART_BASE     HPM_UART0
#define BOARD_CONSOLE_UART_CLK_NAME clock_uart0
#define BOARD_CONSOLE_UART_IRQ      IRQn_UART0
#define BOARD_CONSOLE_UART_BAUDRATE (115200UL)


/*
 * User LED : PA23
 * 470ohm 으로 +3.3V 에 풀업되어 있어 액티브 로우다. (회로도 LED2 / R18)
 */
#define BOARD_LED_GPIO_NAME  "PA23"
#define BOARD_LED_GPIO_CTRL  HPM_GPIO0
#define BOARD_LED_GPIO_INDEX GPIO_DI_GPIOA
#define BOARD_LED_GPIO_PIN   23
#define BOARD_LED_OFF_LEVEL  1
#define BOARD_LED_ON_LEVEL   0


#if defined(__cplusplus)
extern "C" {
#endif

void board_init(void);
void board_init_clock(void);
void board_init_pmp(void);

void board_init_usb_dp_dm_pins(void);
void board_init_usb(USB_Type *ptr);

void board_ungate_mchtmr_at_lp_mode(void);

void board_delay_us(uint32_t us);
void board_delay_ms(uint32_t ms);

#if defined(__cplusplus)
}
#endif

#endif /* BOARD_H */
