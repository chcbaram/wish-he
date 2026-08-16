#ifndef QMK_H_
#define QMK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"


#include "quantum.h"


bool qmkCliInit(void);
bool qmkInit(void);
void qmkUpdate(void);
void qmkRgbStat(uint32_t us);


#ifdef __cplusplus
}
#endif

#endif