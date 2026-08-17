#ifndef UART_H_
#define UART_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "hw_def.h"


#define UART_MAX_CH         HW_UART_MAX_CH


typedef struct uart_driver_t_ uart_driver_t;

typedef struct uart_driver_t_
{
  bool (*open)(uint32_t baud);
  bool (*close)(void);
  uint32_t (*available)(void);
  bool (*flush)(void);
  uint8_t (*read)(void);
  uint32_t (*write)(uint8_t *p_data, uint32_t length);
} uart_driver_t;

bool     uartInit(void);
bool     uartDeInit(void);
bool     uartIsInit(void);
bool     uartOpen(uint8_t ch, uint32_t baud);
bool     uartIsOpen(uint8_t ch);
bool     uartSetDriver(uint8_t ch, uart_driver_t *p_driver);
bool     uartClose(uint8_t ch);
uint32_t uartAvailable(uint8_t ch);
bool     uartFlush(uint8_t ch);
uint8_t  uartRead(uint8_t ch);
uint32_t uartWrite(uint8_t ch, uint8_t *p_data, uint32_t length);
uint32_t uartPrintf(uint8_t ch, const char *fmt, ...);
uint32_t uartGetBaud(uint8_t ch);
uint32_t uartGetRxCnt(uint8_t ch);
uint32_t uartGetTxCnt(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif