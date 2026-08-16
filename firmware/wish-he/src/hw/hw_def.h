#ifndef HW_DEF_H_
#define HW_DEF_H_



#include "bsp.h"


#define _DEF_FIRMWATRE_VERSION    "V260816R10"
#define _DEF_BOARD_NAME           "WISH60-HE"



/*
 * 성능 계측 코드를 넣을지 (keysUpdate · qmkUpdate 의 시간 측정과 초과 카운터).
 *
 * 이 계측은 공짜가 아니다. `micros()` 가 `mchtmr_get_count() / tick_us` 인데
 * 32비트 코어에서 64비트 나눗셈이라 `__udivdi3` 호출이 된다. 그걸 35kHz 스캔
 * 경로에서 한 바퀴에 두 번, QMK 루프에서 또 두 번 부른다.
 *
 * 얼마인지는 재봤다.
 *
 *   켬   29.06 us / 스캔   34,412 회/초
 *   끔   28.16 us / 스캔   35,510 회/초      차이 0.90 us (3.1 %)
 *
 * 0.9us 는 micros() 두 번 값이다 (400MHz 에서 360 사이클). qmkUpdate 까지 더하면
 * 루프당 약 1.8us.
 *
 * ★ 기본은 켬이다. 8kHz 예산 125us 에 지금 32us 를 쓰므로 1.8us 는 의미가 없고,
 *   반대로 이 카운터들은 값을 한 적이 있다 — keyboard_task 초과가 "153만 번 중
 *   3회" 로 드러나지 않았으면 구조를 계속 의심했을 것이다. 최대치 하나로는
 *   "3번" 과 "5000번" 이 구분되지 않는다.
 *
 * 끄면 `keys time` 의 주기·초과 횟수와 `qmk info` 의 keyboard_task 통계가 0 이 된다.
 */
#define _USE_HW_PERF_STAT       1

#define _USE_HW_LED
#define      HW_LED_MAX_CH          1

/* IAP 부트로더 진입 / 소프트 리셋. docs/README.md 6.3 참조 */
#define _USE_HW_RESET

/*
 * HDMA 채널 배분 — 전역 자원이므로 여기서 한 곳에 모아 관리한다.
 * 드라이버가 각자 임의 번호를 잡으면 조용히 서로 덮어쓴다(실제로 겪었다).
 */
#define      HW_DMA_CH_UART0_RX     0
#define      HW_DMA_CH_WS2812       2
/*           HW_DMA_CH_ADC          3~  (예정) */

/*
 * 우선순위는 채널 번호와 무관하다 — CHCTRL[n].CTRL bit29 로 채널마다 따로 준다.
 * 2단계뿐이고 dma_default_channel_config() 기본값은 LOW 다.
 *
 *   ADC    : HIGH  — 스캔 타이밍이 밀리면 키 입력이 튄다
 *   WS2812 : LOW   — 2ms 정도 늦어도 눈에 안 보인다
 *   UART   : LOW
 */

/* WS2812 (PA29 = SPI1.MOSI). 이 보드의 유일한 시각 표시 수단이다. */
#define _USE_HW_WS2812
#define      HW_WS2812_MAX_CH       83

/* 키 스캔 (ADC 시퀀스 + 아날로그 MUX). 8채널 x 8스텝 = 64키 */
#define _USE_HW_KEYS
#define      HW_KEYS_STEP_MAX       8       /* MUX 스텝 = 논리 행 */
#define      HW_KEYS_CH_MAX         8       /* ADC 채널 = 논리 열 */
#define      KEYS_CH_MAX_PER_ADC    (HW_KEYS_CH_MAX / 2)   /* ADC 하나가 맡는 채널 */

#define _USE_HW_UART
#define      HW_UART_MAX_CH         2
#define      HW_UART_CH_DEBUG       _DEF_UART1     /* ch0 = UART0 (PA00/PA01), FT2232 VCP */
#define      HW_UART_CH_USB         _DEF_UART2     /* ch1 = USB CDC (가상 채널) */
#define      HW_UART_CH_CLI         HW_UART_CH_DEBUG


/*
 * 내장 플래시 (XPI NOR 1MB).
 *
 *   0x00000 ~ 0x20000   부트로더·예약      건드리면 안 됨
 *   0x20000 ~ 0x80000   본 펌웨어
 *   0x80000 ~ 0xC0000   벤더 EEPROM (e2p)  ★ 우리 것이 아니다. 건드리면 안 됨
 *   0xC0000 ~ 0x100000  우리 몫 (256KB)
 */
#define _USE_HW_FLASH
#define      HW_FLASH_SECTOR_SIZE   4096         /* 소거 단위 */
#define      HW_FLASH_PAGE_SIZE     256          /* 기록 단위 */
#define      HW_FLASH_USER_BEGIN    0x0C0000UL   /* 이 아래로는 쓰지 않는다 */
#define      HW_FLASH_CAL_A         0x0C0000UL   /* 보정 핑퐁 A */
#define      HW_FLASH_CAL_B         0x0C1000UL   /* 보정 핑퐁 B */
#define      HW_FLASH_E2P_BEGIN     0x0C4000UL   /* QMK/VIA EEPROM 이미지 */
#define      HW_FLASH_E2P_SIZE      0x004000UL   /* 논리 16KB = 4섹터 */

/*
 * 설정(프로파일 네 벌) 핑퐁 — E2P 뒤에 둔다.
 *
 * ★ 보정과 저장 자리를 나눈다.
 *
 *   보정은 보드를 잰 값이고 설정은 취향이다. 한 덩어리로 두면 입력지점을 한 번
 *   만질 때마다 보정값까지 통째로 다시 쓴다 — 잃으면 아픈 쪽을 쓸데없이 자주
 *   위험에 놓는 셈이다.
 *
 *   네 벌이라 4KB 를 넘으므로 슬롯마다 8KB(2섹터)를 준다. 뒤로 0x100000 까지
 *   208KB 가 비어 있어 더 늘려도 된다.
 */
#define      HW_FLASH_SET_A         0x0C8000UL   /* 설정 핑퐁 A (8KB) */
#define      HW_FLASH_SET_B         0x0CA000UL   /* 설정 핑퐁 B (8KB) */

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
#define _USE_CLI_HW_KEYS            1
#define _USE_CLI_HW_FLASH           1
#define _USE_CLI_HW_UART            1
#define _USE_CLI_HW_USB             1


#endif
