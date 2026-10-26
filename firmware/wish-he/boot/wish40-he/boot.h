#ifndef BOOT_H_
#define BOOT_H_

#include <stdint.h>
#include <stdbool.h>


/*
 * 플래시 배치 — wish60-he 부트로더 계약 그대로 (docs/00-hardware.md 2·3절).
 * 앱(src/hw/hw_def.h · reset.c)이 이 값을 이미 알고 있다. 바꾸면 앱도 같이 바꿔야 한다.
 */
#define BOOT_XIP_BASE         0x80000000UL
#define BOOT_FLAG_OFF         0x01D000UL    /* 업데이트 플래그 — 앱이 0x0000FFFF 를 쓴다 */
#define BOOT_APP_OFF          0x020000UL
#define BOOT_APP_END          0x080000UL    /* 이 앞까지만 쓴다. 0xC0000~ 는 앱의 데이터 */
#define BOOT_APP_MAGIC        0x0A4D5048UL  /* "HPM\n" */
#define BOOT_SECTOR_SIZE      4096


/* 부트로더에 남은 까닭 — JTAG 으로 boot_why 를 읽으면 보인다 */
#define BOOT_WHY_NO_APP       (1 << 0)      /* 매직이 없다 */
#define BOOT_WHY_FLAG         (1 << 1)      /* 앱이 진입을 요청했다 */
#define BOOT_WHY_BTN          (1 << 2)      /* BOOT_BTN(PA30) */
#define BOOT_WHY_ESC          (1 << 3)      /* Esc 를 누른 채 꽂았다 */


bool iapInit(void);
void iapProcess(const uint8_t *rx, uint8_t *tx);
bool iapIsDone(void);

bool usbBootInit(void);
bool usbBootRecv(uint8_t *buf);
void usbBootSend(const uint8_t *buf);

#endif
