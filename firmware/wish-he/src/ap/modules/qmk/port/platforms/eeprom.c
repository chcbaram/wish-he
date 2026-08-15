/*
 * port/platforms/eeprom.c  —  QMK EEPROM 을 내장 플래시로
 *
 * QMK 는 EEPROM 을 바이트 단위로 아무 때나 읽고 쓴다. NOR 플래시는 그렇게 못 쓴다
 * (섹터 단위 소거, 1->0 만 가능). 그래서 **RAM 섬도 + 지연 플러시** 로 간다.
 *
 *   읽기   RAM 섬도에서 바로. 플래시 접근이 없다
 *   쓰기   RAM 섬도를 고치고 그 섹터를 dirty 로 표시만
 *   플러시 조용해진 뒤에 dirty 섹터 하나씩 소거+기록
 *
 * ★ 지연 플러시가 핵심이다.
 *
 *   VIA 로 키맵을 바꾸면 바이트 쓰기가 수백 번 연달아 온다. 매번 플래시에 쓰면
 *   같은 섹터를 수백 번 지우게 되고(수명), 그동안 인터럽트가 막혀 USB 가 멈춘다.
 *   조용해질 때까지 모았다가 쓰면 소거가 섹터당 1회다.
 *
 * ★ 한 번에 한 섹터만 쓴다.
 *
 *   XIP 라 소거·기록 동안 인터럽트를 막아야 한다(flash.c 의 함정 ②). 16KB 를
 *   통째로 쓰면 그 시간이 통째로 정지 구간이 된다. 섹터(4KB)로 끊으면 정지가
 *   짧게 여러 번으로 나뉜다.
 */

#include "quantum.h"
#include "eeprom.h"
#include "flash.h"


#define EE_SECTOR_SIZE   HW_FLASH_SECTOR_SIZE
#define EE_SECTOR_CNT    (TOTAL_EEPROM_BYTE_COUNT / EE_SECTOR_SIZE)

/* 마지막 쓰기 뒤 이만큼 조용하면 플러시한다 */
#define EE_FLUSH_MS      200


static uint8_t        eeprom_buf[TOTAL_EEPROM_BYTE_COUNT];
static uint32_t       dirty_mask   = 0;
static uint32_t       dirty_time   = 0;
static bool           is_req_clean = false;
static uint32_t       flush_cnt    = 0;   /* 진단용 — 실제 소거 횟수 */


void eeprom_init(void)
{
  /*
   * 지운 적 없는 플래시는 전부 0xFF 다. QMK 의 eeconfig 가 매직 워드를 보고 스스로
   * 초기화하므로 여기서 판단하지 않는다 — 읽어만 준다.
   */
  flashRead(HW_FLASH_E2P_BEGIN, eeprom_buf, TOTAL_EEPROM_BYTE_COUNT);
  dirty_mask = 0;
}

/* dirty 섹터 하나를 실제로 기록한다. 여기서만 플래시를 건드린다. */
static bool eepromFlushOne(void)
{
  uint32_t s;
  uint32_t addr;

  if (dirty_mask == 0) return false;

  s    = (uint32_t)__builtin_ctz(dirty_mask);
  addr = HW_FLASH_E2P_BEGIN + s * EE_SECTOR_SIZE;

  /* 실패해도 dirty 를 내린다. 안 그러면 매 루프마다 재시도하며 USB 를 굶긴다. */
  dirty_mask &= ~(1U << s);

  if (flashErase(addr, EE_SECTOR_SIZE) == false)
  {
    logPrintf("[E_] eeprom 소거 실패 0x%06X\n", (unsigned)addr);
    return false;
  }
  if (flashWrite(addr, &eeprom_buf[s * EE_SECTOR_SIZE], EE_SECTOR_SIZE) == false)
  {
    logPrintf("[E_] eeprom 기록 실패 0x%06X\n", (unsigned)addr);
    return false;
  }

  flush_cnt++;
  return true;
}

/* 남은 것을 전부 지금 쓴다. 리셋·부트로더 진입 직전에 부른다. */
void eeprom_flush(void)
{
  while (dirty_mask) eepromFlushOne();
}

void eeprom_update(void)
{
  eepromFlushOne();
}

void eeprom_task(void)
{
  if (dirty_mask && (millis() - dirty_time) >= EE_FLUSH_MS)
  {
    eepromFlushOne();
  }

  if (is_req_clean)
  {
    eeconfig_disable();
    eeprom_flush();
    soft_reset_keyboard();
  }
}

void eeprom_req_clean(void)
{
  is_req_clean = true;
}

uint32_t eepromGetFlushCount(void) { return flush_cnt; }
uint32_t eepromGetDirtyMask(void)  { return dirty_mask; }

uint8_t  eeprom_read_byte(const uint8_t *addr)
{
  uint32_t i = (uint32_t)addr;

  return (i < TOTAL_EEPROM_BYTE_COUNT) ? eeprom_buf[i] : 0;
}

uint16_t eeprom_read_word(const uint16_t *addr)
{
  uint16_t ret = 0;

  ret  = eeprom_buf[((uint32_t)addr) + 0] << 0;
  ret |= eeprom_buf[((uint32_t)addr) + 1] << 8;

  return ret;
}

uint32_t eeprom_read_dword(const uint32_t *addr)
{
  uint32_t ret = 0;
  const uint8_t *p = (const uint8_t *)addr;

  ret  = eeprom_read_byte(p + 0) << 0;
  ret |= eeprom_read_byte(p + 1) << 8;
  ret |= eeprom_read_byte(p + 2) << 16;
  ret |= eeprom_read_byte(p + 3) << 24;

  return ret;
};

void eeprom_read_block(void *buf, const void *addr, uint32_t len)
{
  const uint8_t *p    = (const uint8_t *)addr;
  uint8_t       *dest = (uint8_t *)buf;
  while (len--)
  {
    *dest++ = eeprom_read_byte(p++);
  }
}

void eeprom_write_byte(uint8_t *addr, uint8_t value)
{
  uint32_t i = (uint32_t)addr;

  if (i >= TOTAL_EEPROM_BYTE_COUNT) return;

  eeprom_buf[i] = value;

  /* 플래시에 바로 쓰지 않는다. 섹터를 표시해두고 조용해지면 eeprom_task() 가 쓴다 */
  dirty_mask |= (1U << (i / EE_SECTOR_SIZE));
  dirty_time  = millis();
}

void eeprom_write_word(uint16_t *addr, uint16_t value)
{
	uint8_t *p = (uint8_t *)addr;
	eeprom_write_byte(p++, value);
	eeprom_write_byte(p, value >> 8);
}

void eeprom_write_dword(uint32_t *addr, uint32_t value)
{
	uint8_t *p = (uint8_t *)addr;
	eeprom_write_byte(p++, value);
	eeprom_write_byte(p++, value >> 8);
	eeprom_write_byte(p++, value >> 16);
	eeprom_write_byte(p, value >> 24); 
}

void eeprom_write_block(const void *buf, void *addr, size_t len)
{
  uint8_t       *p   = (uint8_t *)addr;
  const uint8_t *src = (const uint8_t *)buf;
  while (len--)
  {
    eeprom_write_byte(p++, *src++);
  }
}

void eeprom_update_byte(uint8_t *addr, uint8_t value)
{
  uint8_t orig = eeprom_read_byte(addr);
  if (orig != value)
  {
    eeprom_write_byte(addr, value);
  }
}

void eeprom_update_word(uint16_t *addr, uint16_t value)
{
  uint16_t orig = eeprom_read_word(addr);
  if (orig != value)
  {
    eeprom_write_word(addr, value);
  }
}

void eeprom_update_dword(uint32_t *addr, uint32_t value)
{
  uint32_t orig = eeprom_read_dword(addr);
  if (orig != value)
  {
    eeprom_write_dword(addr, value);
  }
}

void eeprom_update_block(const void *buf, void *addr, size_t len)
{
  uint8_t read_buf[len];
  eeprom_read_block(read_buf, addr, len);
  if (memcmp(buf, read_buf, len) != 0)
  {
    eeprom_write_block(buf, addr, len);
  }
}