/*
 * 업데이트 프로토콜 — wish60-he 부트로더와 같다 (09편). 호스트 쪽은 고치지 않는다.
 *
 *   요청 64B : [0]=명령 [1]=길이 [2..3]=미사용 [4..]=데이터
 *   응답 64B : [0]=0x85 [1]=1(성공)/0(실패)
 *
 *   0x81 시작   [4..7]=총 길이(LE32). 기록 주소를 0x80020000 으로 되돌린다
 *   0x80 데이터 4KB 페이지 버퍼에 붙인다
 *   0x82 기록   지금 버퍼를 0xFF 로 채워 쓰고 나서, 이 리포트의 데이터를 붙인다
 *   0x83 종료   (호스트는 상태를 안 본다 — wish60 부트로더가 0 을 줬다)
 *
 * ★ 굽다가 끊겨도 앱으로 넘어가지 않게 한다 — 그게 이 파일에서 가장 중요한 일이다.
 *
 *   조립한 보드는 택트 스위치에 손이 안 닿는다. 반쯤 쓴 앱으로 넘어가 죽으면 남는
 *   길이 Esc 하나뿐이다. 그래서 매직이 든 첫 페이지를 **맨 마지막에** 쓴다.
 *
 *     시작   첫 섹터부터 지운다 — 매직이 사라진다
 *     기록   첫 페이지는 RAM 에 들고만 있고, 나머지는 받는 대로 쓴다
 *     종료   첫 페이지를 쓴다 — 이때 비로소 매직이 생긴다
 *
 *   어디서 끊기든 매직이 없으니 다음 부팅은 부트로더에 남는다.
 *
 * ★ 쓰는 범위를 0x20000 ~ 0x80000 으로 못 박는다. 호스트가 주소를 줄 수 없으니
 *   부트로더 자신(0x0~)도, 앱의 저장 데이터(0xC0000~)도 덮을 길이 없다.
 */
#include <string.h>
#include "board.h"
#include "hpm_romapi.h"
#include "hpm_l1c_drv.h"
#include "boot.h"


#define CMD_DATA          0x80
#define CMD_START         0x81
#define CMD_FLUSH         0x82
#define CMD_END           0x83
#define RSP_TAG           0x85

#define PAYLOAD_OFF       4
#define PAYLOAD_MAX       60

/* board.c 의 nor_cfg_option 과 같아야 한다. 헤더 하위 4비트 = 뒤따르는 워드 수 (03편) */
#define NOR_CFG_HEADER    0xFCF90002U
#define NOR_CFG_OPTION0   0x00000006U
#define NOR_CFG_OPTION1   0x00001000U


static xpi_nor_config_t nor_cfg;
static bool     is_init     = false;
static bool     is_started  = false;
static bool     is_done     = false;
static uint32_t wr_off;                /* 다음에 쓸 플래시 오프셋 */
static uint32_t page_len;
static uint8_t  page[BOOT_SECTOR_SIZE]  __attribute__((aligned(4)));
static uint8_t  page0[BOOT_SECTOR_SIZE] __attribute__((aligned(4)));
static bool     has_page0   = false;

volatile uint32_t iap_err;             /* JTAG 으로 본다 — 마지막 실패 자리 */


static bool iapFlushPage(void);
static bool iapWriteSector(uint32_t off, const uint8_t *data);
static bool iapEraseSector(uint32_t off);
static bool iapFinish(void);




bool iapInit(void)
{
  xpi_nor_config_option_t opt;
  uint32_t                mask;

  opt.header.U  = NOR_CFG_HEADER;
  opt.option0.U = NOR_CFG_OPTION0;
  opt.option1.U = NOR_CFG_OPTION1;

  mask    = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  is_init = (rom_xpi_nor_get_config(HPM_XPI0, &nor_cfg, &opt) == status_success);
  restore_global_irq(mask);

  return is_init;
}

void iapProcess(const uint8_t *rx, uint8_t *tx)
{
  uint8_t cmd = rx[0];
  uint8_t len = rx[1];
  bool    ok  = false;

  memset(tx, 0, 64);
  tx[0] = RSP_TAG;

  if (len > PAYLOAD_MAX) len = 0;

  switch (cmd)
  {
    case CMD_START:
    {
      uint32_t total = rx[4] | (rx[5] << 8) | (rx[6] << 16) | ((uint32_t)rx[7] << 24);

      is_started = false;
      has_page0  = false;
      page_len   = 0;
      wr_off     = BOOT_APP_OFF;

      if (is_init && total > 0 && total <= BOOT_APP_END - BOOT_APP_OFF)
      {
        ok = is_started = iapEraseSector(BOOT_APP_OFF);   /* 매직부터 없앤다 */
      }
      break;
    }

    case CMD_DATA:
      if (is_started && page_len + len <= BOOT_SECTOR_SIZE)
      {
        memcpy(&page[page_len], &rx[PAYLOAD_OFF], len);
        page_len += len;
        ok = true;
      }
      break;

    case CMD_FLUSH:
      /* 먼저 쓰고 나서 붙인다 — 순서가 프로토콜이다 (09편 함정 ①) */
      if (is_started && iapFlushPage())
      {
        memcpy(&page[0], &rx[PAYLOAD_OFF], len);
        page_len = len;
        ok = true;
      }
      break;

    case CMD_END:
      /*
       * wish60 부트로더는 여기서 그냥 앱으로 뛰었다. 우리는 마무리하고 리셋한다.
       * 시작 없이 온 종료(진입만 하고 나가기)도 받는다 — 플래그를 지우고 나간다.
       */
      ok = iapFinish();
      is_done = true;
      break;

    default:
      break;
  }

  tx[1] = ok ? 1 : 0;
}

bool iapIsDone(void)
{
  return is_done;
}

static bool iapFlushPage(void)
{
  if (page_len == 0) return true;
  if (wr_off + BOOT_SECTOR_SIZE > BOOT_APP_END) { iap_err = 1; return false; }

  memset(&page[page_len], 0xFF, BOOT_SECTOR_SIZE - page_len);

  if (wr_off == BOOT_APP_OFF)
  {
    memcpy(page0, page, BOOT_SECTOR_SIZE);     /* 매직 페이지 — 종료 때 쓴다 */
    has_page0 = true;
  }
  else if (iapWriteSector(wr_off, page) == false)
  {
    return false;
  }

  wr_off  += BOOT_SECTOR_SIZE;
  page_len = 0;
  return true;
}

static bool iapFinish(void)
{
  bool ok = true;

  if (is_started)
  {
    is_started = false;
    if (iapFlushPage() == false) return false;           /* 자투리를 안 보낸 호스트 */
    if (has_page0 == false)      { iap_err = 2; return false; }
    if (*(uint32_t *)page0 != BOOT_APP_MAGIC) { iap_err = 3; return false; }
    if (iapWriteSector(BOOT_APP_OFF, page0) == false) return false;
  }

  /* 앱이 남긴 진입 요청을 지운다. 안 지우면 다음 부팅도 여기 남는다 */
  if (*(volatile uint32_t *)(BOOT_XIP_BASE + BOOT_FLAG_OFF) != 0xFFFFFFFFUL)
  {
    ok = iapEraseSector(BOOT_FLAG_OFF);
  }
  return ok;
}

static bool iapWriteSector(uint32_t off, const uint8_t *data)
{
  hpm_stat_t st;
  uint32_t   mask;

  if (iapEraseSector(off) == false) return false;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  st   = rom_xpi_nor_program(HPM_XPI0, xpi_xfer_channel_auto, &nor_cfg,
                             (const uint32_t *)data, off, BOOT_SECTOR_SIZE);
  restore_global_irq(mask);
  if (st != status_success) { iap_err = 0x10 | (off >> 12) << 8; return false; }

  /* ROM API 는 D-cache 를 안 건드린다. 되읽기 전에 무효화한다 (03편 함정 ②) */
  l1c_dc_invalidate(BOOT_XIP_BASE + off, BOOT_SECTOR_SIZE);
  if (memcmp((const void *)(BOOT_XIP_BASE + off), data, BOOT_SECTOR_SIZE) != 0)
  {
    iap_err = 0x20 | (off >> 12) << 8;
    return false;
  }
  return true;
}

static bool iapEraseSector(uint32_t off)
{
  hpm_stat_t st;
  uint32_t   mask;

  /* 부트로더 영역은 지울 수 없다 — 플래그 섹터만 예외 */
  if (off != BOOT_FLAG_OFF && (off < BOOT_APP_OFF || off >= BOOT_APP_END))
  {
    iap_err = 4;
    return false;
  }

  /* 소거 중에 XIP 에서 인출하면 죽는다. ISR 이 플래시에 있으니 인터럽트를 막는다 */
  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  st   = rom_xpi_nor_erase_sector(HPM_XPI0, xpi_xfer_channel_auto, &nor_cfg, off);
  restore_global_irq(mask);

  l1c_dc_invalidate(BOOT_XIP_BASE + off, BOOT_SECTOR_SIZE);
  if (st != status_success) { iap_err = 0x30 | (off >> 12) << 8; return false; }
  return true;
}
