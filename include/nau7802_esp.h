// src/nau7802_esp.h
#pragma once
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- デバイス基本 ---
#define NAU_I2C_ADDR          0x2A

// --- レジスタ（ITRON版を踏襲） ---
#define PU_CTRL_ADDR          0x00
#define CTRL1_ADDR            0x01
#define CTRL2_ADDR            0x02
#define ADCO_B2_ADDR          0x12
#define OTP_B1_ADDR           0x15
#define PGA_PWR_ADDR          0x1B

// PU_CTRL bits
#define CS_START_CONVERSION   (1<<4)
#define PUA_POWER_UP          (1<<2)
#define PUD_POWER_UP          (1<<1)
#define RR_RESET              (1<<0)
#define RR_NORMAL             (0<<0)

// CTRL1 bits
#define CRP_ACTIVE_LOW              (1<<7)
#define DRDY_SEL_DATA_READY         (0<<6)
#define DRDY_SEL_OUTPUT_CONVERSION  (1<<6)
#define VLDO_3V3                    (4<<3)
#define GAINS_64                    (6<<0)

// CTRL2 bits
#define CHS_CH1               (0<<7)
#define CRS_10                (0<<4)
#define CRS_20                (1<<4)
#define CRS_40                (2<<4)
#define CRS_80                (3<<4)
#define CRS_320               (7<<4)
#define CALS_ACTION           (1<<2)
#define CALMOD_OFFSET_INTERNAL (0<<0)

esp_err_t nau_init(void);
esp_err_t nau_default_config(void);
esp_err_t nau_start_conversion(void);
esp_err_t nau_calibrate_offset_internal(void);
esp_err_t nau_set_sample_rate(uint16_t sps); // 10/20/40/80/320
esp_err_t nau_read_adc_24b(uint8_t out_b2b1b0[3]);

#ifdef __cplusplus
}
#endif
