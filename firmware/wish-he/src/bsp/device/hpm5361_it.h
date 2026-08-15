#ifndef HPM5361_IT_H_
#define HPM5361_IT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"


#define FAULT_LOG_MAGIC       0x464C5431    /* "FLT1" */


typedef struct
{
  uint32_t magic;
  uint32_t mcause;
  uint32_t mepc;
  uint32_t mtval;
  uint32_t count;
} fault_log_t;


bool     itInit(void);

/*
 * 부팅 실패 카운터 (.noinit — 리셋해도 남는다).
 *
 * itBootMarkStart() 를 부팅 맨 앞에서 부르고, 메인 루프가 일정 시간 살아남으면
 * itBootMarkOk() 로 지운다. 지워지지 않고 쌓이면 "부팅 도중에 죽고 있다" 는 뜻이다.
 * 하드폴트도 exception_handler 가 리셋하므로 같은 그물에 걸린다.
 */
void     itBootMarkStart(void);
void     itBootMarkOk(void);
uint32_t itBootGetFailCount(void);

uint32_t itGetTickMs(void);
uint32_t itGetTickUs(void);

bool               itFaultIsValid(void);
const fault_log_t *itFaultGet(void);
void               itFaultClear(void);


#ifdef __cplusplus
}
#endif

#endif
