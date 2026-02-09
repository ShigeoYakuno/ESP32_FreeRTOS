// src/sens_task.c
#include "sens_task.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <assert.h>

#include "nau7802_esp.h"
#include "log_task.h"
#include "scale_data.h"
#include "scale_filter.h"
#include "scale_cmd.h"
#include "wifi_task.h"

// ==== ハード依存：必要なら pin/port を変更 ====
// DevKitC 既定ピン（例）：SDA=21 / SCL=22
#ifndef SENS_I2C_PORT
#define SENS_I2C_PORT      I2C_NUM_0
#endif
#ifndef SENS_I2C_SDA
#define SENS_I2C_SDA       21
#endif
#ifndef SENS_I2C_SCL
#define SENS_I2C_SCL       22
#endif
// NAU7802 DRDY は “変換完了でLOW” (CTRL1:CRP=LOW) を前提
#ifndef SENS_DRDY_GPIO
#define SENS_DRDY_GPIO     25
#endif
#ifndef SENS_I2C_FREQ
#define SENS_I2C_FREQ      10000 /*10KHz*/
#endif

static const char *TAG = "sensTask";

static TaskHandle_t s_task = NULL;
static QueueHandle_t s_q = NULL;
static SemaphoreHandle_t s_sem_drdy = NULL;


// ====== 前方宣言 ======
static esp_err_t i2c_bus_init(void);
static void  drdy_isr(void *arg);
static void sens_task_entry(void *arg);

// ====== 公開関数 ======
QueueHandle_t sens_get_queue(void) { return s_q; }

void sens_set_rate_hz(uint16_t sps) {
    nau_set_sample_rate(sps);
}

void sens_force_calibration(void) {
    nau_calibrate_offset_internal();
}

void start_sens_task(void)
{
    if (s_task) return;

    // ---- I2C ----
    ESP_ERROR_CHECK(i2c_bus_init());

    // ---- DRDY GPIO 設定（まずは割り込み無効で入力として確定）----
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SENS_DRDY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     // 外部PU無しなら内部PUを有効
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,       // ★最初は必ず無効（起動直後の暴走防止）
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    // ---- セマフォ ----
    s_sem_drdy = xSemaphoreCreateBinary();
    assert(s_sem_drdy);

    // ---- GPIO ISR service ----
    {
        esp_err_t err = gpio_install_isr_service(0);
        if (err == ESP_ERR_INVALID_STATE) err = ESP_OK; // 既にインストール済み
        ESP_ERROR_CHECK(err);
    }

    // ---- ISR 登録（まだ割り込みは有効化しない）----
    ESP_ERROR_CHECK(gpio_isr_handler_add(SENS_DRDY_GPIO, drdy_isr, NULL));

    // ---- センサ初期化（DRDY割り込みを入れる前に完了させる）----
    ESP_ERROR_CHECK(nau_init());
    ESP_ERROR_CHECK(nau_default_config());       // ★DRDY_SEL_DATA_READY 推奨（5MHz出力は絶対NG）
    // ※ offset calib はタスク側でやる設計でもOK。ここでやっても可。
    // ESP_ERROR_CHECK(nau_calibrate_offset_internal());
    ESP_ERROR_CHECK(nau_start_conversion());

    // 初期安定化（必要なら）
    vTaskDelay(pdMS_TO_TICKS(50));

    // ---- ここで初めて割り込み条件を設定して有効化 ----
    // DRDYがActive-Lowで「データ準備でLOW」なら NEGEDGE が基本
    ESP_ERROR_CHECK(gpio_set_intr_type(SENS_DRDY_GPIO, GPIO_INTR_NEGEDGE));
    gpio_intr_enable(SENS_DRDY_GPIO);

    // ※ もし「最初からLOWで保持型」なら、ここで一回起こしても良い
    if (gpio_get_level(SENS_DRDY_GPIO) == 0) {
        xSemaphoreGive(s_sem_drdy);
    }

    // ---- Task ----
    xTaskCreatePinnedToCore(sens_task_entry, "sensTask", 6144, NULL, 3, &s_task, 1);
    syslog(INFO, "sensTask started");
}



// ====== 実装 ======
static esp_err_t i2c_bus_init(void)
{
    // すでにドライバが入っていると i2c_driver_install が失敗することがあるので、
    // 常に「消してから入れる」方針にして再初期化に強くする
    (void)i2c_driver_delete(SENS_I2C_PORT);

    i2c_config_t c = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SENS_I2C_SDA,
        .scl_io_num = SENS_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = SENS_I2C_FREQ,
        .clk_flags = 0,
    };

    ESP_ERROR_CHECK(i2c_param_config(SENS_I2C_PORT, &c));

    esp_err_t err = i2c_driver_install(SENS_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) {
        // まれに「すでにインストール済み」扱いで返る場合があるので吸収
        err = ESP_OK;
    }
    return err;
}


static void  IRAM_ATTR drdy_isr(void *arg)
{
    BaseType_t hp = pdFALSE;
    if (s_sem_drdy) {
        xSemaphoreGiveFromISR(s_sem_drdy, &hp);
    }
    if (hp) portYIELD_FROM_ISR();
}

static inline int32_t signext24(uint32_t u24)
{
    // 24bit 符号拡張
    if (u24 & 0x800000) u24 |= 0xFF000000;
    return (int32_t)u24;
}

static void adc_init(void)
{
#if 0
        /*フラッシュデータ読み出し*/
	err = setInitZeroSpandata();
    if(err != E_OK){
        syslog(LOG_INFO,"init %x %x %x",getSpanCoef(),getTrueZeroData(),getTareZeroData());
        tk_dly_tsk(10);
    }   
#endif
    setSpanCoef(0xFFFFFFFF);   
    firFilterInit();  
}

static void sens_task_entry(void *arg)
{
    //AD処理初期化
    adc_init();
    // 既定: 80SPS/GAIN=64/LDO=3V3/DRDY=LOWアクティブで設定（Pico実装準拠）
    nau_default_config();

    // 初回ダミーリード & オフセット内部校正
    nau_calibrate_offset_internal();

    // 変換開始
    nau_start_conversion();

    // 受信ループ（DRDY割り込みでデータ取得）
    while (1) {
        if (xSemaphoreTake(s_sem_drdy, pdMS_TO_TICKS(1000)) == pdTRUE) {

            uint8_t b2b1b0[3];
            static int fl_cnt=0;

            if (nau_read_adc_24b(b2b1b0) == ESP_OK) {

                uint32_t raw =
                    ((uint32_t)b2b1b0[0] << 16)
                    | ((uint32_t)b2b1b0[1] << 8)
                    |  (uint32_t)b2b1b0[2];

                int32_t raw24 = signext24(raw);
                setRawData(raw24);

                // 受信をトリガーに処理を1回回す
                processingData();
                
                fl_cnt++;
                if(fl_cnt >= 100){
                    uint32_t data = getNormalizeData();
                    syslog(INFO,"%d",data);
                    // WiFi経由でデータ送信
                    wifi_send_data(data);
                    fl_cnt=0;
                }
                

            }
        }else{
            syslog(INFO, "NAU7802 DRDY TIMEOUT");
        }

    }
}

