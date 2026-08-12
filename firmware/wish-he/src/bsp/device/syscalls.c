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

void __assert_func(const char *file, int line, const char *func, const char *expr)
{
  logPrintf("[E_] assert : %s:%d %s() : %s\r\n",
            file != NULL ? file : "?",
            line,
            func != NULL ? func : "?",
            expr != NULL ? expr : "?");

  __asm volatile("ebreak");

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
