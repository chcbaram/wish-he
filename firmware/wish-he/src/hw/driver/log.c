#include "log.h"


#ifdef _USE_HW_LOG
#include "uart.h"
#include "cli.h"

#ifdef _USE_HW_RTOS
#define lock()      xSemaphoreTake(mutex_lock, portMAX_DELAY);
#define unLock()    xSemaphoreGive(mutex_lock);
#else
#define lock()      
#define unLock()    
#endif


typedef struct
{
  uint16_t line_index;
  uint16_t buf_length;
  uint16_t buf_length_max;
  uint16_t buf_index;
  uint8_t *buf;
} log_buf_t;


log_buf_t log_buf_boot;
log_buf_t log_buf_list;

/*
 * ★ 로그 고리는 AHB SRAM 에 둔다. **DLM 이 아깝다.**
 *
 *   32KB 짜리 AHB SRAM 이 통째로 놀고 있는데 DLM 은 87% 였다. 로그는 사람이 읽자고
 *   쌓는 것이라 조금 느린 메모리로 보내도 아무 차이가 없다.
 */
ATTR_PLACE_AT(".ahb_sram")
static uint8_t buf_boot[LOG_BOOT_BUF_MAX];
ATTR_PLACE_AT(".ahb_sram")
static uint8_t buf_list[LOG_LIST_BUF_MAX];

static bool is_init = false;
static bool is_boot_log = true;
static bool is_enable = true;
static bool is_open = false;

static uint8_t  log_ch = LOG_CH;
static uint32_t log_baud = 115200;

static char print_buf[256];

#ifdef _USE_HW_RTOS
static SemaphoreHandle_t mutex_lock;
#endif


/*
 * JTAG 로 읽는 RAM 콘솔.
 *
 * 이 보드에는 UART 헤더가 없어 USB CDC 가 올라오기 전에는 출력 수단이 전혀 없다.
 * 그래서 로그를 .noinit 링버퍼에도 흘려두고 OpenOCD 로 덤프한다.
 * .noinit 은 리셋 후에도 보존되므로 크래시 직전 로그도 남는다.
 *
 * 덤프 방법은 docs/README.md 4.2 참조. 버퍼 주소는 build/wish60-he.map 의
 * log_ram 심볼에서 확인한다.
 */
#define LOG_RAM_BUF_MAX   2048
#define LOG_RAM_MAGIC     0x474F4C52UL    /* "RLOG" */

typedef struct
{
  uint32_t magic;
  uint32_t index;      /* 다음에 쓸 위치 */
  uint32_t wrapped;    /* 한 바퀴 돌았으면 1 */
  uint32_t size;
  uint8_t  buf[LOG_RAM_BUF_MAX];
} log_ram_t;

static __attribute__((section(".noinit"), used)) log_ram_t log_ram;


static void logRamReset(void)
{
  log_ram.magic   = LOG_RAM_MAGIC;
  log_ram.index   = 0;
  log_ram.wrapped = 0;
  log_ram.size    = LOG_RAM_BUF_MAX;
}

static void logRamWrite(const char *p_data, uint32_t length)
{
  /* 콜드 부팅이면 내용이 쓰레기다. 매직으로 판별한다. */
  if (log_ram.magic != LOG_RAM_MAGIC || log_ram.size != LOG_RAM_BUF_MAX)
  {
    logRamReset();
  }

  for (uint32_t i = 0; i < length; i++)
  {
    log_ram.buf[log_ram.index] = (uint8_t)p_data[i];

    log_ram.index++;
    if (log_ram.index >= LOG_RAM_BUF_MAX)
    {
      log_ram.index   = 0;
      log_ram.wrapped = 1;
    }
  }
}



#if CLI_USE(HW_LOG)
static void cliCmd(cli_args_t *args);
#endif





bool logInit(void)
{
#ifdef _USE_HW_RTOS
  mutex_lock = xSemaphoreCreateMutex();
#endif
  
  log_buf_boot.line_index     = 0;
  log_buf_boot.buf_length     = 0;
  log_buf_boot.buf_length_max = LOG_BOOT_BUF_MAX;
  log_buf_boot.buf_index      = 0;
  log_buf_boot.buf            = buf_boot;


  log_buf_list.line_index     = 0;
  log_buf_list.buf_length     = 0;
  log_buf_list.buf_length_max = LOG_LIST_BUF_MAX;
  log_buf_list.buf_index      = 0;
  log_buf_list.buf            = buf_list;


  is_init = true;

#if CLI_USE(HW_LOG)
  cliAdd("log", cliCmd);
#endif

  return true;
}

void logEnable(void)
{
  is_enable = true;
}

void logDisable(void)
{
  is_enable = false;
}

void logBoot(uint8_t enable)
{
  is_boot_log = enable;
}

bool logOpen(uint8_t ch, uint32_t baud)
{
  log_ch   = ch;
  log_baud = baud;
  is_open  = true;

  is_open = uartOpen(ch, baud);

  return is_open;
}

bool logIsOpen(void)
{
  return is_open;
}

bool logBufPrintf(log_buf_t *p_log, char *p_data, uint32_t length)
{
  uint32_t buf_last;
  uint8_t *p_buf;
  int buf_len;


  buf_last = p_log->buf_index + length + 8;
  if (buf_last > p_log->buf_length_max)
  {
    p_log->buf_index = 0;
    buf_last = p_log->buf_index + length + 8;

    if (buf_last > p_log->buf_length_max)
    {
      return false;
    }
  }

  p_buf = &p_log->buf[p_log->buf_index];

  buf_len = snprintf((char *)p_buf, length + 8, "%04X\t%s", p_log->line_index, p_data);
  p_log->line_index++;
  p_log->buf_index += buf_len;


  if (buf_len + p_log->buf_length <= p_log->buf_length_max)
  {
    p_log->buf_length += buf_len;
  }

  return true;
}

void logPrintf(const char *fmt, ...)
{
  lock();

  va_list args;
  int len;

  if (is_init != true) return;


  va_start(args, fmt);
  len = vsnprintf(print_buf, 256, fmt, args);

  if (is_open == true && is_enable == true)
  {
    uartWrite(log_ch, (uint8_t *)print_buf, len);
  }

  logRamWrite(print_buf, len);

  if (is_boot_log)
  {
    logBufPrintf(&log_buf_boot, print_buf, len);
  }
  logBufPrintf(&log_buf_list, print_buf, len);

  va_end(args);

  unLock();
}


#if CLI_USE(HW_LOG)
void cliCmd(cli_args_t *args)
{
  bool ret = false;



  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("boot.line_index %d\n", log_buf_boot.line_index);
    cliPrintf("boot.buf_length %d\n", log_buf_boot.buf_length);
    cliPrintf("\n");
    cliPrintf("list.line_index %d\n", log_buf_list.line_index);
    cliPrintf("list.buf_length %d\n", log_buf_list.buf_length);

    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "boot"))
  {
    uint32_t index = 0;

    while(cliKeepLoop())
    {
      uint32_t buf_len;

      buf_len = log_buf_boot.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      #ifdef _USE_HW_RTOS
      lock();
      #endif

      cliWrite((uint8_t *)&log_buf_boot.buf[index], buf_len);
      index += buf_len;

      #ifdef _USE_HW_RTOS
      unLock();
      #endif
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "list"))
  {
    uint32_t index = 0;

    while(cliKeepLoop())
    {
      uint32_t buf_len;

      buf_len = log_buf_list.buf_length - index;
      if (buf_len == 0)
      {
        break;
      }
      if (buf_len > 64)
      {
        buf_len = 64;
      }

      lock();
      cliWrite((uint8_t *)&log_buf_list.buf[index], buf_len);
      index += buf_len;
      unLock();
    }
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("log info\n");
    cliPrintf("log boot\n");
    cliPrintf("log list\n");
  }
}
#endif


#endif