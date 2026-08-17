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

uint32_t itGetTickMs(void);
uint32_t itGetTickUs(void);

bool               itFaultIsValid(void);
const fault_log_t *itFaultGet(void);
void               itFaultClear(void);


#ifdef __cplusplus
}
#endif

#endif
