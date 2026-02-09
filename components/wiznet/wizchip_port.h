#ifndef __WIZCHIP_PORT_H__
#define __WIZCHIP_PORT_H__

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "wizchip_conf.h"
#include "w5500/w5500.h"

#ifdef __cplusplus
extern "C" {
#endif



// SPI操作関数
void wizchip_spi_init(void);
void wizchip_select(void);
void wizchip_deselect(void);

void wizchip_hw_reset(void);
void wizchip_port_register(void);
void wizchip_write(uint32_t addrSel, uint8_t data);

#ifdef __cplusplus
}
#endif

#endif // __WIZCHIP_PORT_H__
