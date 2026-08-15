#ifndef FLASH_H_
#define FLASH_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#ifdef _USE_HW_FLASH


#define FLASH_SECTOR_SIZE     4096
#define FLASH_PAGE_SIZE       256

/* 주소는 전부 플래시 오프셋이다 (XIP 주소가 아니다). 0x00000 ~ 0xFFFFF */
#define FLASH_XIP_BASE        0x80000000UL


bool flashInit(void);

/*
 * 읽기는 XIP 로 직접 한다 — 플래시가 메모리 맵에 올라와 있어 포인터로 읽힌다.
 * 방금 쓴 자리를 읽을 때 D-cache 가 옛 값을 들고 있으므로 무효화를 같이 한다.
 */
bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length);

/*
 * ★ 소거·기록 중에는 인터럽트가 막힌다.
 *
 *   XIP 로 실행 중이라 같은 플래시를 건드리는 동안 ISR 본체를 인출할 수 없다.
 *   섹터 소거는 수십 ms 가 걸릴 수 있어 그동안 USB 가 멈춘다. 부팅 경로나
 *   USB 가 바쁜 구간에서는 부르지 말 것.
 */
bool flashErase(uint32_t addr, uint32_t length);
bool flashWrite(uint32_t addr, const uint8_t *p_data, uint32_t length);

/* 직전 실패 진단 — 어느 ROM API 에서 어떤 status 였는지 */
uint32_t flashGetErrStep(void);
uint32_t flashGetErrStatus(void);


#endif


#ifdef __cplusplus
}
#endif

#endif
