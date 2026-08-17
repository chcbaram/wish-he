#include "bootloader.h"
#include "eeprom.h"

void bootloader_jump(void)
{
  eeprom_flush();   /* DFU 진입 전 미저장 EEPROM 동기 기록 */
  resetToBoot();
}

void mcu_reset(void)
{
  eeprom_flush();   /* 리셋 전 미저장 EEPROM 동기 기록 (유실 방지) */
  resetToReset();
}