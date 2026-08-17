#include "main.h"



#ifdef _USE_HW_RTOS
static void mainThread(void const *arg);


int main(void)
{
  bspInit();


  osThreadDef(main, mainThread, _HW_DEF_THREAD_MAIN_PRI, 0, _HW_DEF_THREAD_MAIN_STACK/4);
  if (osThreadCreate(osThread(main), NULL) == NULL)
  {
    ledInit();

    while(1)
    {
      ledOn(_DEF_LED1);
      delay(50);
      ledOff(_DEF_LED1);
      delay(50);
      ledOn(_DEF_LED1);
      delay(500);
      ledOff(_DEF_LED1);
      delay(500);      
    }
  }

  osKernelStart();  
  return 0;
}

void mainThread(void const *arg)
{
  UNUSED(arg);

  hwInit();
  apInit();
  apMain();
}
#else
int main(void)
{
  bspInit();
  
  hwInit();
  apInit();
  apMain();

  return 0;
}
#endif
