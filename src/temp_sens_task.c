// src/temp_sens_task.c
// ESP32 AHT20 + BMP280 センサ統合タスク

#include "temp_sens_task.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "log_task.h"
#include "wifi_task.h"
#include "sd_task.h"  // SDカード書き込み用
#include "user_io.h"  // set_sensor_init_failed()用
#include <string.h>
#include <stdint.h>

// ==== ハードウェア設定 ====
#ifndef TEMP_SENS_I2C_PORT
#define TEMP_SENS_I2C_PORT      I2C_NUM_1  // sens_taskとは別ポートを使用
#endif
#ifndef TEMP_SENS_I2C_SDA
#define TEMP_SENS_I2C_SDA       32         // GPIO32
#endif
#ifndef TEMP_SENS_I2C_SCL
#define TEMP_SENS_I2C_SCL       33         // GPIO33
#endif
#ifndef TEMP_SENS_I2C_FREQ
#define TEMP_SENS_I2C_FREQ      10000      // 10kHz
#endif

// ==== AHT20 定義 ====
#define AHT20_I2C_ADDR          0x38
#define AHT20_CMD_CALIBRATE     0xE1
#define AHT20_CMD_TRIGGER       0xAC
#define AHT20_CMD_SOFTRESET     0xBA
#define AHT20_STATUS_BUSY       0x80
#define AHT20_STATUS_CALIBRATED 0x08

// ==== BMP280 定義 ====
#define BMP280_I2C_ADDR         0x77
#define BMP280_REGISTER_CHIPID  0xD0
#define BMP280_REGISTER_VERSION 0xD1
#define BMP280_REGISTER_SOFTRESET 0xE0
#define BMP280_REGISTER_STATUS  0xF3
#define BMP280_REGISTER_CONTROL 0xF4
#define BMP280_REGISTER_CONFIG  0xF5
#define BMP280_REGISTER_PRESSUREDATA 0xF7
#define BMP280_REGISTER_TEMPDATA 0xFA
#define BMP280_REGISTER_DIG_T1  0x88
#define BMP280_REGISTER_DIG_T2  0x8A
#define BMP280_REGISTER_DIG_T3  0x8C
#define BMP280_REGISTER_DIG_P1  0x8E
#define BMP280_REGISTER_DIG_P2  0x90
#define BMP280_REGISTER_DIG_P3  0x92
#define BMP280_REGISTER_DIG_P4  0x94
#define BMP280_REGISTER_DIG_P5  0x96
#define BMP280_REGISTER_DIG_P6  0x98
#define BMP280_REGISTER_DIG_P7  0x9A
#define BMP280_REGISTER_DIG_P8  0x9C
#define BMP280_REGISTER_DIG_P9  0x9E
#define BMP280_CHIPID           0x58

#define BMP280_MODE_SLEEP       0x00
#define BMP280_MODE_FORCED      0x01
#define BMP280_MODE_NORMAL      0x03

static const char *TAG = "tempSensTask";

static TaskHandle_t s_task = NULL;
static bool s_i2c_initialized = false;

// BMP280補正係数
typedef struct {
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;
    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;
} bmp280_calib_data_t;

static bmp280_calib_data_t s_bmp280_calib;

// データ保持用
static temp_sens_data_t s_last_data = {0};

// ==== 前方宣言 ====
static esp_err_t i2c_bus_init(void);
static esp_err_t aht20_init(void);
static esp_err_t aht20_read(int16_t *temp, uint16_t *humidity);
static esp_err_t bmp280_init(void);
static esp_err_t bmp280_read(int16_t *temp, uint32_t *pressure);
static void temp_sens_task_entry(void *arg);

// ==== I2C初期化 ====
static esp_err_t i2c_bus_init(void)
{
    if (s_i2c_initialized) {
        return ESP_OK;
    }

    i2c_config_t c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TEMP_SENS_I2C_SDA,
        .scl_io_num = TEMP_SENS_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,  // 外部4.7kΩプルアップあり
        .scl_pullup_en = GPIO_PULLUP_DISABLE,  // 外部4.7kΩプルアップあり
        .master.clk_speed = TEMP_SENS_I2C_FREQ,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(TEMP_SENS_I2C_PORT, &c);
    if (err != ESP_OK) {
        return err;
    }

    err = i2c_driver_install(TEMP_SENS_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        // 既にインストール済みの場合はOK
        err = ESP_OK;
    }
    
    if (err == ESP_OK) {
        s_i2c_initialized = true;
    }
    
    return err;
}

// ==== AHT20 I2C helper ====
static esp_err_t aht20_write(const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT20_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    for (size_t i = 0; i < len; i++) {
        i2c_master_write_byte(cmd, data[i], true);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TEMP_SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t aht20_read_status(uint8_t *status)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT20_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, status, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TEMP_SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t aht20_read_data(uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (AHT20_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    for (size_t i = 0; i < len; i++) {
        i2c_master_read_byte(cmd, &data[i], (i == len - 1) ? I2C_MASTER_NACK : I2C_MASTER_ACK);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TEMP_SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// ==== AHT20初期化 ====
static esp_err_t aht20_init(void)
{
    // ソフトリセット
    uint8_t cmd = AHT20_CMD_SOFTRESET;
    if (aht20_write(&cmd, 1) != ESP_OK) {
        return ESP_FAIL;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    // ビジー解除待ち
    uint8_t status;
    int retry = 10;
    while (retry-- > 0) {
        if (aht20_read_status(&status) == ESP_OK) {
            if (!(status & AHT20_STATUS_BUSY)) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // キャリブレーション
    uint8_t cal_cmd[3] = {AHT20_CMD_CALIBRATE, 0x08, 0x00};
    aht20_write(cal_cmd, 3);  // 新しいAHT20では成功しない場合があるが続行

    // キャリブレーション完了待ち
    retry = 10;
    while (retry-- > 0) {
        if (aht20_read_status(&status) == ESP_OK) {
            if (!(status & AHT20_STATUS_BUSY)) {
                if (status & AHT20_STATUS_CALIBRATED) {
                    return ESP_OK;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // キャリブレーションが確認できない場合でも続行（動作する場合がある）
    return ESP_OK;
}

// ==== AHT20測定 ====
static esp_err_t aht20_read(int16_t *temp, uint16_t *humidity)
{
    // トリガーコマンド送信
    uint8_t trigger_cmd[3] = {AHT20_CMD_TRIGGER, 0x33, 0x00};
    if (aht20_write(trigger_cmd, 3) != ESP_OK) {
        return ESP_FAIL;
    }

    // 測定完了待ち（最大80ms）
    uint8_t status;
    int retry = 10;
    while (retry-- > 0) {
        if (aht20_read_status(&status) == ESP_OK) {
            if (!(status & AHT20_STATUS_BUSY)) {
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // データ読み取り（6バイト: status + 5バイトデータ）
    uint8_t data[6];
    if (aht20_read_data(data, 6) != ESP_OK) {
        return ESP_FAIL;
    }

    // データ解析（参考: Adafruit_AHTX0::getEvent()）
    uint32_t h_raw = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | ((uint32_t)data[3] >> 4);
    uint32_t t_raw = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | (uint32_t)data[5];

    // 変換式（固定小数点0.1単位）
    // 参考コード: 湿度 = (h_raw * 100) / (1 << 20) → 0.1%単位にするため10倍
    *humidity = (uint16_t)((h_raw * 1000) >> 20);  // 0.1%単位

    // 参考コード: 温度 = (t_raw * 200) / (1 << 20) - 50 → 0.1℃単位にするため10倍
    int32_t t_calc = (int32_t)((t_raw * 2000) >> 20) - 500;  // 0.1℃単位（500 = 50.0℃ * 10）
    *temp = (int16_t)t_calc;

    return ESP_OK;
}

// ==== BMP280 I2C helper ====
static esp_err_t bmp280_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BMP280_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, data, 2, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TEMP_SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t bmp280_read_reg(uint8_t reg, uint8_t *value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BMP280_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BMP280_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TEMP_SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t bmp280_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BMP280_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (BMP280_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    for (size_t i = 0; i < len; i++) {
        i2c_master_read_byte(cmd, &data[i], (i == len - 1) ? I2C_MASTER_NACK : I2C_MASTER_ACK);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(TEMP_SENS_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static uint16_t read16_LE(const uint8_t *data)
{
    return (uint16_t)data[1] << 8 | (uint16_t)data[0];
}

static int16_t readS16_LE(const uint8_t *data)
{
    return (int16_t)read16_LE(data);
}

// ==== BMP280補正係数読み取り ====
static esp_err_t bmp280_read_calibration(void)
{
    uint8_t cal_data[24];
    if (bmp280_read_regs(BMP280_REGISTER_DIG_T1, cal_data, 24) != ESP_OK) {
        return ESP_FAIL;
    }

    s_bmp280_calib.dig_T1 = read16_LE(&cal_data[0]);
    s_bmp280_calib.dig_T2 = readS16_LE(&cal_data[2]);
    s_bmp280_calib.dig_T3 = readS16_LE(&cal_data[4]);

    s_bmp280_calib.dig_P1 = read16_LE(&cal_data[6]);
    s_bmp280_calib.dig_P2 = readS16_LE(&cal_data[8]);
    s_bmp280_calib.dig_P3 = readS16_LE(&cal_data[10]);
    s_bmp280_calib.dig_P4 = readS16_LE(&cal_data[12]);
    s_bmp280_calib.dig_P5 = readS16_LE(&cal_data[14]);
    s_bmp280_calib.dig_P6 = readS16_LE(&cal_data[16]);
    s_bmp280_calib.dig_P7 = readS16_LE(&cal_data[18]);
    s_bmp280_calib.dig_P8 = readS16_LE(&cal_data[20]);
    s_bmp280_calib.dig_P9 = readS16_LE(&cal_data[22]);

    return ESP_OK;
}

// ==== BMP280初期化 ====
static esp_err_t bmp280_init(void)
{
    // チップID確認
    uint8_t chipid;
    if (bmp280_read_reg(BMP280_REGISTER_CHIPID, &chipid) != ESP_OK) {
        return ESP_FAIL;
    }
    if (chipid != BMP280_CHIPID) {
        ESP_LOGE(TAG, "BMP280 chip ID mismatch: 0x%02X (expected 0x%02X)", chipid, BMP280_CHIPID);
        return ESP_FAIL;
    }

    // 補正係数読み取り
    if (bmp280_read_calibration() != ESP_OK) {
        return ESP_FAIL;
    }

    // 設定: Forced mode, Temp x1, Press x1
    // CONTROL: [osrs_t=001][osrs_p=001][mode=01] = 0x27
    bmp280_write_reg(BMP280_REGISTER_CONTROL, 0x27);

    // CONFIG: [t_sb=000][filter=000][spi3w_en=0] = 0x00
    bmp280_write_reg(BMP280_REGISTER_CONFIG, 0x00);

    vTaskDelay(pdMS_TO_TICKS(100));

    return ESP_OK;
}

// ==== BMP280測定 ====
static esp_err_t bmp280_read(int16_t *temp, uint32_t *pressure)
{
    // Forced modeで測定トリガー
    bmp280_write_reg(BMP280_REGISTER_CONTROL, 0x27);
    vTaskDelay(pdMS_TO_TICKS(10));  // 測定完了待ち

    // データ読み取り（6バイト: Press(3) + Temp(3)）
    uint8_t data[6];
    if (bmp280_read_regs(BMP280_REGISTER_PRESSUREDATA, data, 6) != ESP_OK) {
        return ESP_FAIL;
    }

    // 生データ取得
    uint32_t press_raw = ((uint32_t)data[0] << 12) | ((uint32_t)data[1] << 4) | ((uint32_t)data[2] >> 4);
    uint32_t temp_raw = ((uint32_t)data[3] << 12) | ((uint32_t)data[4] << 4) | ((uint32_t)data[5] >> 4);

    // 温度補正計算（BMP280補正式）
    // 参考: Adafruit_BMP280::readTemperature()
    int32_t var1, var2, t_fine;
    var1 = ((((int32_t)temp_raw >> 3) - ((int32_t)s_bmp280_calib.dig_T1 << 1)) *
            ((int32_t)s_bmp280_calib.dig_T2)) >> 11;
    var2 = (((((int32_t)temp_raw >> 4) - ((int32_t)s_bmp280_calib.dig_T1)) *
             (((int32_t)temp_raw >> 4) - ((int32_t)s_bmp280_calib.dig_T1)) >> 12) *
            ((int32_t)s_bmp280_calib.dig_T3)) >> 14;
    t_fine = var1 + var2;
    // 参考コード: (t_fine * 5 + 128) >> 8 で0.01℃単位の整数値（例：2480 = 24.80℃）
    // 0.1℃単位にするため、10で割る（2480 / 10 = 248 → 24.8℃）
    int32_t t_calc_01c = ((t_fine * 5 + 128) >> 8) / 10;
    *temp = (int16_t)t_calc_01c;

    // 気圧補正計算（BMP280補正式）
    // 参考: Adafruit_BMP280::readPressure()
    int64_t var1_p, var2_p, p;
    var1_p = ((int64_t)t_fine) - 128000;
    var2_p = var1_p * var1_p * (int64_t)s_bmp280_calib.dig_P6;
    var2_p = var2_p + ((var1_p * (int64_t)s_bmp280_calib.dig_P5) << 17);
    var2_p = var2_p + (((int64_t)s_bmp280_calib.dig_P4) << 35);
    var1_p = ((var1_p * var1_p * (int64_t)s_bmp280_calib.dig_P3) >> 8) +
             ((var1_p * (int64_t)s_bmp280_calib.dig_P2) << 12);
    var1_p = (((((int64_t)1) << 47) + var1_p)) * ((int64_t)s_bmp280_calib.dig_P1) >> 33;
    
    if (var1_p == 0) {
        return ESP_FAIL;  // ゼロ除算回避
    }
    
    p = 1048576 - (int64_t)press_raw;
    p = (((p << 31) - var2_p) * 3125) / var1_p;
    var1_p = (((int64_t)s_bmp280_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2_p = (((int64_t)s_bmp280_calib.dig_P8) * p) >> 19;
    p = ((p + var1_p + var2_p) >> 8) + (((int64_t)s_bmp280_calib.dig_P7) << 4);
    
    // 参考コード: (float)p / 256 でPa単位（例：258240 / 256 = 1008.75 Pa）
    // つまり p は (Pa * 256) の単位
    // Pa → hPa への変換: 1hPa = 100Pa なので p/256 / 100 = p / 25600
    // 0.1hPa単位にするには10倍: (p / 25600) * 10 = (p * 10) / 25600
    *pressure = (uint32_t)((p * 10) / 25600);  // 0.1hPa単位

    return ESP_OK;
}

// ==== 最新センサデータ取得関数 ====
void temp_sens_get_latest_data(temp_sens_data_t *data)
{
    if (data) {
        *data = s_last_data;
    }
}

// ==== 公開関数 ====
void start_temp_sens_task(void)
{
    if (s_task) {
        return;
    }

    // 初期化失敗フラグをリセット
    set_sensor_init_failed(false);

    // I2C初期化
    esp_err_t err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed");
        set_sensor_init_failed(true);
        return;
    }

    // AHT20初期化
    if (aht20_init() != ESP_OK) {
        ESP_LOGE(TAG, "AHT20 initialization failed");
        set_sensor_init_failed(true);
        return;
    }
    syslog(INFO, "AHT20 initialized");

    // BMP280初期化
    if (bmp280_init() != ESP_OK) {
        ESP_LOGE(TAG, "BMP280 initialization failed");
        set_sensor_init_failed(true);
        return;
    }
    syslog(INFO, "BMP280 initialized");

    // 初期データ初期化
    memset(&s_last_data, 0, sizeof(s_last_data));
    s_last_data.seq = 0;

    // タスク作成
    xTaskCreatePinnedToCore(temp_sens_task_entry, "tempSensTask", 4096, NULL, 3, &s_task, 1);
    syslog(INFO, "tempSensTask started");
}

// ==== タスクエントリ ====
static void temp_sens_task_entry(void *arg)
{
    static uint32_t seq = 0;

    while (1) {
        temp_sens_data_t data = {0};
        data.seq = seq++;

        // AHT20測定
        int16_t aht_temp = 0;
        uint16_t aht_humidity = 0;
        if (aht20_read(&aht_temp, &aht_humidity) == ESP_OK) {
            data.aht_t01 = aht_temp;
            data.aht_rh01 = aht_humidity;
            data.aht_ok = true;
            s_last_data.aht_t01 = aht_temp;
            s_last_data.aht_rh01 = aht_humidity;
        } else {
            ESP_LOGW(TAG, "AHT20 read failed, using last value");
            data.aht_t01 = s_last_data.aht_t01;
            data.aht_rh01 = s_last_data.aht_rh01;
            data.aht_ok = false;
        }

        // BMP280測定
        int16_t bmp_temp = 0;
        uint32_t bmp_pressure = 0;
        if (bmp280_read(&bmp_temp, &bmp_pressure) == ESP_OK) {
            data.bmp_t01 = bmp_temp;
            data.bmp_p01 = bmp_pressure;
            data.bmp_ok = true;
            s_last_data.bmp_t01 = bmp_temp;
            s_last_data.bmp_p01 = bmp_pressure;
        } else {
            ESP_LOGW(TAG, "BMP280 read failed, using last value");
            data.bmp_t01 = s_last_data.bmp_t01;
            data.bmp_p01 = s_last_data.bmp_p01;
            data.bmp_ok = false;
        }

        // WiFiキューに送信（構造体データ）
        wifi_send_temp_sens_data(&data);

        // SDカードに書き込み（SDカードがマウントされている場合のみ）
        if (sd_is_mounted()) {
            sd_write_sensor_data(&data);
        }

        // UARTにログ出力（2秒ごと）
        // 温度・湿度は0.1単位（例：253 = 25.3℃）、気圧は0.1hPa単位（例：100845 = 10084.5hPa）
        syslog(INFO, "TEMP: AHT T=%.1fC RH=%.1f%% BMP T=%.1fC P=%.1fhPa [seq=%lu aht_ok=%s bmp_ok=%s]",
               (float)data.aht_t01 / 10.0f,      // 0.1℃単位 → ℃
               (float)data.aht_rh01 / 10.0f,     // 0.1%単位 → %
               (float)data.bmp_t01 / 10.0f,      // 0.1℃単位 → ℃
               (float)data.bmp_p01 / 10.0f,      // 0.1hPa単位 → hPa
               (unsigned long)data.seq,
               data.aht_ok ? "OK" : "NG",
               data.bmp_ok ? "OK" : "NG");

        // 2秒周期待機
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
