/*
 * syscalls.c
 *
 * newlib 이 요구하는 최소 시스템 콜 구현.
 * hpm_sdk 의 components/debug_console 와 utils/hpm_sbrk.c 가 하던 역할을 대신한다.
 *
 *   _write / _read   : 콘솔 UART(HW_UART_CH_DEBUG) 로 연결
 *   _sbrk            : 링커 스크립트의 .heap 섹션 경계(__heap_start__ / __heap_end__) 안에서만 확장
 *   __assert_func    : <assert.h> 의 assert() 실패 콜백
 */

#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/times.h>

#include "hw_def.h"
#include "uart.h"
#include "flash.h"
#include "reset.h"


char *__env[1] = { 0 };
char **environ = __env;




void *_sbrk(int incr)
{
  extern char __heap_start__, __heap_end__;
  static char *heap_end;
  char        *prev_heap_end;

  if (heap_end == NULL)
  {
    heap_end = &__heap_start__;
  }

  prev_heap_end = heap_end;

  if (heap_end + incr > &__heap_end__)
  {
    errno = ENOMEM;
    return (void *)-1;
  }

  heap_end += incr;

  return (void *)prev_heap_end;
}

int _write(int file, char *ptr, int len)
{
  (void)file;

  if (uartIsInit() == false) return len;

  uartWrite(HW_UART_CH_DEBUG, (uint8_t *)ptr, (uint32_t)len);

  return len;
}

int _read(int file, char *ptr, int len)
{
  (void)file;
  int i;

  if (uartIsInit() == false) return 0;

  for (i = 0; i < len; i++)
  {
    while (uartAvailable(HW_UART_CH_DEBUG) == 0)
    {
      //
    }
    *ptr++ = uartRead(HW_UART_CH_DEBUG);
  }

  return len;
}

/*
 * assert 실패 — 부트로더로 넘긴다.
 *
 * ★ 그냥 멈추면 벽돌이다.
 *
 *   assert 는 대개 초기화 도중에 터진다. 그 자리에서 무한루프에 들어가면 USB 가
 *   아예 안 올라와서 콘솔도 업데이트 채널도 없다. 부트로더 핀을 눌러야만 살아난다.
 *   실제로 QMK 를 얹다가 WS2812 프레임버퍼 정렬 assert 로 그렇게 됐고, 원인을 찾는 데
 *   JTAG 을 다시 물려야 했다.
 *
 *   부트로더로 가면 USB 업데이트 채널이 뜨므로 그냥 다시 구우면 된다.
 *
 * ★ 조건 없이 넘기지는 않는다.
 *
 *   8편에서 "부팅 N회 실패하면 부트로더로" 라는 안전망을 넣었다가 브릭을 만들었다.
 *   그때 문제는 **아주 이른 시점에 플래시를 건드린 것**이었다 — 부트 플래그 기록이
 *   실패하자 조용히 리턴해 그대로 리셋 루프에 남았다.
 *
 *   그래서 여기서는 flashIsReady() 로 두 가지를 확인한다.
 *     - 플래시가 초기화됐는가        (아니면 기록 자체가 안 된다)
 *     - 지금 소거·기록 중이 아닌가   (그 도중에 죽었다면 또 건드리면 안 된다)
 *
 *   못 넘어가면 멈춘다. 넘어가기를 재시도하거나 리셋을 걸지 않는다 — 리셋 루프가
 *   가장 나쁜 결말이다.
 */
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
  logPrintf("[E_] assert : %s:%d %s() : %s\r\n",
            file != NULL ? file : "?",
            line,
            func != NULL ? func : "?",
            expr != NULL ? expr : "?");

  /* 디버거가 붙어 있으면 여기서 멈춘다. 원인을 그 자리에서 볼 수 있다. */
  __asm volatile("ebreak");

  if (flashIsReady())
  {
    logPrintf("[  ] 부트로더로 넘어간다\n");
    resetToBoot();                 /* 성공하면 돌아오지 않는다 */
    logPrintf("[E_] 부트 플래그 기록 실패 — 멈춘다\n");
  }
  else
  {
    logPrintf("[E_] 플래시를 쓸 수 없다 — 멈춘다\n");
  }

  while (1)
  {
  }
}




int _getpid(void)
{
  return 1;
}

int _kill(int pid, int sig)
{
  (void)pid;
  (void)sig;
  errno = EINVAL;
  return -1;
}

void _exit(int status)
{
  _kill(status, -1);
  while (1) {}
}

int _close(int file)
{
  (void)file;
  return -1;
}

int _fstat(int file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _isatty(int file)
{
  (void)file;
  return 1;
}

int _lseek(int file, int ptr, int dir)
{
  (void)file;
  (void)ptr;
  (void)dir;
  return 0;
}

int _open(char *path, int flags, ...)
{
  (void)path;
  (void)flags;
  return -1;
}

int _wait(int *status)
{
  (void)status;
  errno = ECHILD;
  return -1;
}

int _unlink(char *name)
{
  (void)name;
  errno = ENOENT;
  return -1;
}

int _times(struct tms *buf)
{
  (void)buf;
  return -1;
}

int _stat(char *file, struct stat *st)
{
  (void)file;
  st->st_mode = S_IFCHR;
  return 0;
}

int _link(char *old, char *new)
{
  (void)old;
  (void)new;
  errno = EMLINK;
  return -1;
}

int _fork(void)
{
  errno = EAGAIN;
  return -1;
}

int _execve(char *name, char **argv, char **env)
{
  (void)name;
  (void)argv;
  (void)env;
  errno = ENOMEM;
  return -1;
}
