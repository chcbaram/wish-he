#ifndef WIZ_SPI_H_
#define WIZ_SPI_H_

#ifdef __cplusplus
 extern "C" {
#endif


#include "hw_def.h"

#ifdef _USE_HW_WIZSPI


bool wizspiInit(void);
bool wizspiRead(uint8_t block_sel, uint16_t addr, void *p_data, uint32_t length, uint32_t timeout_ms);
bool wizspiWrite(uint8_t block_sel, uint16_t addr, void *p_data, uint32_t length, uint32_t timeout_ms);


#endif

#ifdef __cplusplus
}
#endif

#endif 