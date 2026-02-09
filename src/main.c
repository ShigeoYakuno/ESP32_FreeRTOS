#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_flash.h"

#include "wifi_task.h"
#include "log_task.h"
#include "user_io.h"
#include "user_test.h"
#include "user_bt_test.h"
#include "user_common.h"
#include "sd_task.h"

#include "sens_task.h"
#include "temp_sens_task.h"  // 新規追加
#include "isr_func.h"
#include "enet_task.h"
#include "flash_data.h"
#include "bluetooth_task.h"

#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_system.h"
#include "panic_handler.h"




static TaskHandle_t mainTaskHandle = NULL;

void exec_soft_reset(void)
{
    syslog(INFO,"system reset exec after 5 seconds...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    syslog(INFO,"system reset exec after 4 seconds...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    syslog(INFO,"system reset exec after 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    syslog(INFO,"system reset exec after 2 seconds...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    syslog(INFO,"system reset exec after 1 seconds...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}


static void init_nvs_or_panic(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}


void app_main(void)
{
    //記憶している設定をロード
    flashdata_load();
    vTaskDelay(pdMS_TO_TICKS(100)); 
    // === 1. NVSを最初に初期化 ===
    init_nvs_or_panic();

    // === 2. BLEメモリ解放 ===
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);

    
    // ログタスクを起動（Bluetooth安定後） ===
    start_log_task();
    syslog_set_output(LOG_OUTPUT_UART);

    // === WiFiタスクを起動 ===
    mainTaskHandle = xTaskGetCurrentTaskHandle();
    start_wifi_task(mainTaskHandle);

    //BT系のエラー表示しない
    esp_log_level_set("BT_APPL", ESP_LOG_ERROR);    //アプリケーション層
    esp_log_level_set("BT_HCI", ESP_LOG_ERROR);     //HCI層
    esp_log_level_set("BT_BTM",  ESP_LOG_ERROR);    //BTM層
    esp_log_level_set("BT_GAP",  ESP_LOG_ERROR);    //GAP層
    esp_log_level_set("BT_BTM",  ESP_LOG_WARN);     //SPP層
    esp_log_level_set("BT",  ESP_LOG_ERROR);        //全体


    //BTタスクスタート
    start_bluetooth_task();
    vTaskDelay(pdMS_TO_TICKS(1000)); 


#if 1
    // === 6. 通常処理 ===
    syslog(INFO, "System start");
    
    // メモリ監視タスクを起動（メモリリーク検出用）
    start_memory_monitor_task();
    
    start_userIO_task();
    start_test_task();
    //start_user_bt_test_task();
    //start_sd_task();
    //start_sens_task();  // NAU7802関連：無効化
    start_temp_sens_task();  // AHT20+BMP280：新規追加

    //start_enet_task();

#endif
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

