#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "log_task.h"
#include "user_io.h"
#include "wifi_task.h"  // wifi_is_connected()用
#include "flash_data.h"  // getWifiDevNo()用
#include <stdint.h>

// ==== デバウンス時間定義 ====
#define SW_DEBOUNCE_MS  50  // スイッチのデバウンス時間（50ms）

// ==== セマフォ（ISRからタスクへの通知用） ====
static SemaphoreHandle_t sw_sem = NULL;

// ==== 初期化失敗フラグ ====
static bool s_sensor_init_failed = false;
static SemaphoreHandle_t s_init_failed_mutex = NULL;

// ==== 割り込みハンドラ ====
static void IRAM_ATTR sw_isr_handler(void *arg)
{
    // 割り込みを一時的に無効化（チャタリング防止）
    gpio_intr_disable(USER_SW);
    
    // セマフォでタスクに通知（ISRからセマフォをgiveできる）
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (sw_sem != NULL) {
        xSemaphoreGiveFromISR(sw_sem, &xHigherPriorityTaskWoken);
    }
    
    // コンテキストスイッチが必要な場合は実行
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}



// ==== 初期化 ====
void init_userIO(void)
{
    // --- LED出力設定 ---
    gpio_reset_pin(HEART_BEAT_LED);
    gpio_set_direction(HEART_BEAT_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(HEART_BEAT_LED, 0);



    //スイッチ入力設定

    gpio_config_t io_conf_sw1 = {
        .pin_bit_mask = 1ULL << USER_SW,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // 押下でLOWになる想定
    };
    gpio_config(&io_conf_sw1);




    // --- セマフォ作成 ---
    sw_sem = xSemaphoreCreateBinary();
    if (sw_sem == NULL) {
        syslog(ERR, "Failed to create SW semaphore");
        return;
    }

    // --- 割り込みサービス登録 ---
    gpio_install_isr_service(0);
    gpio_isr_handler_add(USER_SW, sw_isr_handler, NULL);

    syslog(DEBUG,"UserIO initialized");
}

// ==== LED点滅タスク（Wi-Fi接続状態に応じて周期を変更） ====
#define LED_BLINK_SLOW_MS  1000  // 接続できていない場合：1秒周期
#define LED_BLINK_FAST_MS  200   // 接続できた場合：200ms周期（速い）
#define LED_BLINK_ERROR_MS 500   // 初期化失敗時：0.5秒周期（高速点滅）

// ==== SW割り込み処理タスク ====
static void sw_interrupt_task(void *pvParameters)
{
    while (1) {
        // セマフォでSW割り込みを待機（無限待機）
        if (xSemaphoreTake(sw_sem, portMAX_DELAY) == pdTRUE) {
            // デバウンス時間待機（チャタリング除去）
            vTaskDelay(pdMS_TO_TICKS(SW_DEBOUNCE_MS));
            
            // スイッチの状態を確認（確実にLOWになっているか確認）
            if (gpio_get_level(USER_SW) == 0) {
                // 実際の処理（ログ出力など）
                syslog(DEBUG, "SW pressed (GPIO%d)", USER_SW);
                
                // ここにSW押下時の処理を追加可能
                // 例: 何かの機能を実行する
            }
            
            // 割り込みを再度有効化
            gpio_intr_enable(USER_SW);
        }
    }
}

// ==== LED点滅タスク ====
static void led_blink_task(void *pvParameters)
{
    while (1) {
        uint32_t blink_period;
        
        // 初期化失敗フラグをチェック（最優先）
        bool init_failed = false;
        if (s_init_failed_mutex != NULL) {
            if (xSemaphoreTake(s_init_failed_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                init_failed = s_sensor_init_failed;
                xSemaphoreGive(s_init_failed_mutex);
            }
        }
        
        if (init_failed) {
            // 初期化失敗時：0.5秒周期で高速点滅
            blink_period = LED_BLINK_ERROR_MS;
        } else {
            // Wi-Fi接続状態を確認して点滅周期を変更
            bool is_connected = wifi_is_connected();
            blink_period = is_connected ? LED_BLINK_FAST_MS : LED_BLINK_SLOW_MS;
        }
        
        gpio_set_level(HEART_BEAT_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(blink_period));
        gpio_set_level(HEART_BEAT_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(blink_period));
    }
}




// ==== 初期化失敗フラグ設定関数 ====
void set_sensor_init_failed(bool failed)
{
    if (s_init_failed_mutex == NULL) {
        s_init_failed_mutex = xSemaphoreCreateMutex();
        if (s_init_failed_mutex == NULL) {
            syslog(ERR, "Failed to create init_failed mutex");
            return;
        }
    }
    
    if (xSemaphoreTake(s_init_failed_mutex, portMAX_DELAY) == pdTRUE) {
        s_sensor_init_failed = failed;
        xSemaphoreGive(s_init_failed_mutex);
    }
}

// ==== 子機No取得関数 ====
// E2memdataのWIFI_DEV_NOから子機Noを取得
// 値が無効な場合は1を返す
//
// 【将来の拡張用（コメントアウト）】
// DIPSWが実装された場合は、GPIOピンから2bit読み取って1-4の値を返す
// 例: GPIO39, GPIO34等のDIPSWピンから2bit読み取り
// #define DIPSW_PIN0  GPIO_NUM_39
// #define DIPSW_PIN1  GPIO_NUM_34
// uint8_t dipsw_bits = (gpio_get_level(DIPSW_PIN0) ? 1 : 0) | 
//                      ((gpio_get_level(DIPSW_PIN1) ? 1 : 0) << 1);
// return (dipsw_bits & 0x03) + 1;  // 0-3 → 1-4
//
uint8_t get_child_device_no(void)
{
    uint32_t dev_no = getWifiDevNo();
    if (dev_no >= 1 && dev_no <= 4) {
        return (uint8_t)dev_no;
    }
    // 無効な値の場合は1を返す（デフォルト）
    return 1;
}

// ==== 公開関数 ====
void start_userIO_task(void)
{
    init_userIO();
    // SW割り込み処理タスクを起動
    xTaskCreate(sw_interrupt_task, "SW_Interrupt_Task", 2048, NULL, 5, NULL);
    // LED点滅タスクを起動
    xTaskCreate(led_blink_task, "LED_Blink_Task", 2048, NULL, 2, NULL);
}
