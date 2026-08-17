// Copyright 2018-2022 QMK
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once


#include "hw_def.h"



void     eeprom_init(void);
void     eeprom_update(void);   /* 논블로킹 1바이트 드레인 (준비됐을 때만) */
void     eeprom_flush(void);    /* 블로킹: 큐 전체 동기 기록 (리셋/DFU 직전) */
void     eeprom_task(void);
void     eeprom_req_clean(void);
uint8_t  eeprom_read_byte(const uint8_t *addr);
uint16_t eeprom_read_word(const uint16_t *addr);
uint32_t eeprom_read_dword(const uint32_t *addr);
void     eeprom_read_block(void *buf, const void *addr, uint32_t len);
void     eeprom_write_byte(uint8_t *addr, uint8_t value);
void     eeprom_write_word(uint16_t *addr, uint16_t value);
void     eeprom_write_dword(uint32_t *addr, uint32_t value);
void     eeprom_write_block(const void *buf, void *addr, size_t len);
void     eeprom_update_byte(uint8_t *addr, uint8_t value);
void     eeprom_update_word(uint16_t *addr, uint16_t value);
void     eeprom_update_dword(uint32_t *addr, uint32_t value);
void     eeprom_update_block(const void *buf, void *addr, size_t len);

/* 진단 — qmk info 가 쓴다. 소거 횟수는 플래시 수명을 보는 눈이다. */
uint32_t eepromGetFlushCount(void);
uint32_t eepromGetDirtyMask(void);
