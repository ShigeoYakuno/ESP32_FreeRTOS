// src/nau7802_esp.c
#include "nau7802_esp.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>

#ifndef SENS_I2C_PORT
#define SENS_I2C_PORT I2C_NUM_0
#endif
static const char *TAG = "nau7802";

// ---- I2C helpers ----
static esp_err_t wr_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NAU_I2C_ADDR<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t rd_reg(uint8_t reg, uint8_t *val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NAU_I2C_ADDR<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NAU_I2C_ADDR<<1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, val, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t rd_multi(uint8_t reg, uint8_t *buf, size_t len)
{
    // “アドレス書き+連続読み” の一般形
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NAU_I2C_ADDR<<1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (NAU_I2C_ADDR<<1) | I2C_MASTER_READ, true);
    for (size_t i=0;i<len;i++) {
        i2c_master_read_byte(cmd, &buf[i], (i==len-1)? I2C_MASTER_NACK : I2C_MASTER_ACK);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// ---- Public API ----
esp_err_t nau_init(void)
{
    // レジスタリセット
    ESP_ERROR_CHECK(wr_reg(PU_CTRL_ADDR, RR_RESET));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(wr_reg(PU_CTRL_ADDR, RR_NORMAL));
    vTaskDelay(pdMS_TO_TICKS(10));

    // アナログ/デジタル Power-Up
    uint8_t pu=0;
    rd_reg(PU_CTRL_ADDR, &pu);
    pu |= PUA_POWER_UP; ESP_ERROR_CHECK(wr_reg(PU_CTRL_ADDR, pu));
    vTaskDelay(pdMS_TO_TICKS(10));
    pu |= PUD_POWER_UP; ESP_ERROR_CHECK(wr_reg(PU_CTRL_ADDR, pu));
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t nau_default_config(void)
{
    // CTRL1: DRDY=LOW Active / DRDY=転送完了 / LDO=3.3V / GAIN=64
    //uint8_t c1 = (CRP_ACTIVE_LOW | DRDY_SEL_OUTPUT_CONVERSION | VLDO_3V3 | GAINS_64);
    uint8_t c1 = (CRP_ACTIVE_LOW | DRDY_SEL_DATA_READY | VLDO_3V3 | GAINS_64);
    ESP_ERROR_CHECK(wr_reg(CTRL1_ADDR, c1));
    vTaskDelay(pdMS_TO_TICKS(10));

    // CTRL2: CH1 / 80SPS 既定
    uint8_t c2 = (CHS_CH1 | CRS_80);
    ESP_ERROR_CHECK(wr_reg(CTRL2_ADDR, c2));
    vTaskDelay(pdMS_TO_TICKS(10));

    // データシート推奨: OTP_B1=0x30, PGA_PWR=出力バッファ有効（Pico実装準拠）
    ESP_ERROR_CHECK(wr_reg(OTP_B1_ADDR, 0x30));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(wr_reg(PGA_PWR_ADDR, (1<<5))); // PGA_OUTPUT_BUFFER_ENABLE
    vTaskDelay(pdMS_TO_TICKS(10));

    return ESP_OK;
}

esp_err_t nau_start_conversion(void)
{
    uint8_t pu = 0;
    ESP_ERROR_CHECK(rd_reg(PU_CTRL_ADDR, &pu));

    // 念のため一旦クリア
    pu &= (uint8_t)~CS_START_CONVERSION;
    ESP_ERROR_CHECK(wr_reg(PU_CTRL_ADDR, pu));
    vTaskDelay(pdMS_TO_TICKS(1));

    // 立ち上げてスタート
    pu |= CS_START_CONVERSION;
    ESP_ERROR_CHECK(wr_reg(PU_CTRL_ADDR, pu));
    vTaskDelay(pdMS_TO_TICKS(1));

    // トリガ型なら戻す（保持型なら戻しても動くことが多い）
    pu &= (uint8_t)~CS_START_CONVERSION;
    return wr_reg(PU_CTRL_ADDR, pu);
}


esp_err_t nau_calibrate_offset_internal(void)
{
    // CTRL2: CALMOD=内部オフセット / CALS=1 で開始（終了で自動クリア）
    uint8_t c2=0;
    ESP_ERROR_CHECK(rd_reg(CTRL2_ADDR, &c2));
    c2 &= ~((uint8_t)0x0F); // 下位4bitを一旦クリア
    c2 |= (CALMOD_OFFSET_INTERNAL | (c2 & 0xF0)); // SPSは保持
    ESP_ERROR_CHECK(wr_reg(CTRL2_ADDR, c2 | CALS_ACTION));
    vTaskDelay(pdMS_TO_TICKS(100)); // Pico実装では100ms待ち
    // エラービット確認は必要に応じて（簡略）
    return ESP_OK;
}

esp_err_t nau_set_sample_rate(uint16_t sps)
{
    uint8_t rate_bits = CRS_80;
    switch (sps) {
        case 10: rate_bits = CRS_10; break;
        case 20: rate_bits = CRS_20; break;
        case 40: rate_bits = CRS_40; break;
        case 80: rate_bits = CRS_80; break;
        case 320: rate_bits = CRS_320; break;
        default: return ESP_ERR_INVALID_ARG;
    }
    uint8_t c2=0; ESP_ERROR_CHECK(rd_reg(CTRL2_ADDR, &c2));
    c2 &= ~((uint8_t)0x70);
    c2 |= rate_bits;
    return wr_reg(CTRL2_ADDR, c2);
}

esp_err_t nau_read_adc_24b(uint8_t out_b2b1b0[3])
{
    return rd_multi(ADCO_B2_ADDR, out_b2b1b0, 3);
}
