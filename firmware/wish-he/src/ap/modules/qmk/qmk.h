#ifndef QMK_H_
#define QMK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ap_def.h"

/* 루프 통계 — 진단 화면이 함께 본다 */
typedef struct
{
  uint32_t task_us;
  uint32_t task_us_max;
  uint32_t task_us_avg;
  uint32_t task_over;      /* 125us 를 넘긴 횟수 */
  uint32_t task_cnt;
  uint32_t rgb_us;
  uint32_t rgb_us_max;
  uint32_t rgb_us_avg;
} qmk_stat_t;

void qmkGetStat(qmk_stat_t *p_stat);
void qmkClearStat(void);




#include "quantum.h"


bool qmkCliInit(void);
bool qmkStart(void);
bool qmkIsOn(void);
void qmkUpdate(void);
void qmkRgbStat(uint32_t us);


#ifdef __cplusplus
}
#endif

#endif