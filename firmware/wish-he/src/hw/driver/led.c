#include "led.h"


#ifdef _USE_HW_LED

#include "hpm_gpio_drv.h"


typedef struct
{
  GPIO_Type *ctrl;
  uint32_t   port;
  uint8_t    pin;
  uint32_t   pad;         /* IOC_PAD_xxx   */
  uint32_t   pad_func;    /* IOC_xxx_FUNC_CTL_GPIO_x_xx */
  uint8_t    on_state;
  uint8_t    off_state;
} led_tbl_t;


static const led_tbl_t led_tbl[LED_MAX_CH] =
{
#if defined(HW_BOARD_WISH40_HE)
  /* wish40-he : PA31 -> LED -> 1k -> GND. 액티브 하이 (회로도 LED2 / R12) */
  {
    HPM_GPIO0, GPIO_DI_GPIOA, 31,
    IOC_PAD_PA31, IOC_PA31_FUNC_CTL_GPIO_A_31,
    1, 0
  },
#else
  /* hpm5300evk : User LED = PA23, 470ohm 로 +3.3V 풀업 -> 액티브 로우 */
  {
    HPM_GPIO0, GPIO_DI_GPIOA, 23,
    IOC_PAD_PA23, IOC_PA23_FUNC_CTL_GPIO_A_23,
    BOARD_LED_ON_LEVEL, BOARD_LED_OFF_LEVEL
  },
#endif
};



bool ledInit(void)
{
  /* GPIO 클럭은 board_init_clock() 에서 그룹0에 등록된다 */

  for (int i=0; i<LED_MAX_CH; i++)
  {
    HPM_IOC->PAD[led_tbl[i].pad].FUNC_CTL = led_tbl[i].pad_func;

    gpio_set_pin_output_with_initial(led_tbl[i].ctrl,
                                     led_tbl[i].port,
                                     led_tbl[i].pin,
                                     led_tbl[i].off_state);
  }

  return true;
}

void ledOn(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  gpio_write_pin(led_tbl[ch].ctrl, led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].on_state);
}

void ledOff(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  gpio_write_pin(led_tbl[ch].ctrl, led_tbl[ch].port, led_tbl[ch].pin, led_tbl[ch].off_state);
}

void ledToggle(uint8_t ch)
{
  if (ch >= LED_MAX_CH) return;

  gpio_toggle_pin(led_tbl[ch].ctrl, led_tbl[ch].port, led_tbl[ch].pin);
}

#endif
