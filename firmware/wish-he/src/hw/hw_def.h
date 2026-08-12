#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260726R1"
#define _DEF_BOARD_NAME           "WISH60-HE"



#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

/* IAP 부트로더 진입 / 소프트 리셋. docs/README.md 6.3 참조 */
#define _USE_HW_RESET

/* WS2812 (PA29 = SPI1.MOSI). 이 보드의 유일한 시각 표시 수단이다. */
#define _USE_HW_WS2812
#define      HW_WS2812_MAX_CH       83

#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_DEBUG       _DEF_UART1     /* ch0 = UART0 (PA00/PA01), FT2232 VCP */
#define      HW_UART_CH_USB         _DEF_UART2     /* ch1 = USB CDC (가상 채널) */
#define      HW_UART_CH_CLI         HW_UART_CH_DEBUG


//-- USB (CDC)
//
#define _USE_HW_USB
#define _USE_HW_CDC
#define      HW_USE_CDC             1

/* USB 스택 선택 : 0 = CherryUSB, 1 = TinyUSB
   빌드에서 -DHW_USB_STACK=1 로 오버라이드 가능 */
#define      HW_USB_STACK_CHERRYUSB 0
#define      HW_USB_STACK_TINYUSB   1
#ifndef      HW_USB_STACK
#define      HW_USB_STACK           HW_USB_STACK_CHERRYUSB
#endif

#define _USE_HW_CLI
#define      HW_CLI_CMD_LIST_MAX    32
#define      HW_CLI_CMD_NAME_MAX    16
#define      HW_CLI_LINE_HIS_MAX    8
#define      HW_CLI_LINE_BUF_MAX    64

#define _USE_HW_CLI_GUI
#define      HW_CLI_GUI_WIDTH       80
#define      HW_CLI_GUI_HEIGHT      24

#define _USE_HW_LOG
#define      HW_LOG_CH              HW_UART_CH_DEBUG
#define      HW_LOG_BOOT_BUF_MAX    2048
#define      HW_LOG_LIST_BUF_MAX    4096

#define _USE_HW_SWTIMER
#define      HW_SWTIMER_MAX_CH      16


//-- CLI
//
#define _USE_CLI_HW_LOG             1
#define _USE_CLI_HW_RESET           1
#define _USE_CLI_HW_WS2812          1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_USB             1


#endif
