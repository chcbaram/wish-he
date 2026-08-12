#ifndef CLI_MGR_H_
#define CLI_MGR_H_


#include "ap_def.h"

#ifdef __cplusplus
extern "C"
{
#endif

  bool cliMgrInit(void);
  void cliMgrEnable(bool enable);

#ifdef __cplusplus
}
#endif

#endif