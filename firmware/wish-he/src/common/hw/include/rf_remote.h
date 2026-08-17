#ifndef RF_REMOTE_H_
#define RF_REMOTE_H_


#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#ifdef _USE_HW_RF_REMOTE



typedef struct
{
  uint32_t id;
  uint8_t  data;
} rfremote_info_t;


bool rfRemoteInit(void);


#endif

#ifdef __cplusplus
}
#endif



#endif 