#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "log_task.h"
#include "user_io.h"

// ==== 定義 ====
#define USER_SW_PIN   GPIO_NUM_27
#define USER_LED_PIN  GPIO_NUM_4

// ==== 割り込みハンドラ ====
static void IRAM_ATTR sw_isr_handler(void *arg)
{
    // 割り込み発生時のログ送信（ISR対応版）
    syslog(DEBUG,"SW interrupt detected (GPIO%d)", USER_SW_PIN);
}

// ==== 初期化 ====
void init_userIO(void)
{
    // --- LED出力設定 ---
    gpio_reset_pin(USER_LED_PIN);
    gpio_set_direction(USER_LED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(USER_LED_PIN, 0);

    // --- スイッチ入力設定（内部プルアップ付き） ---
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << USER_SW_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,  // 押下でLOWになる想定
    };
    gpio_config(&io_conf);

    // --- 割り込みサービス登録 ---
    gpio_install_isr_service(0);
    gpio_isr_handler_add(USER_SW_PIN, sw_isr_handler, NULL);

    syslog(DEBUG,"UserIO initialized (LED=GPIO%d, SW=GPIO%d)", USER_LED_PIN, USER_SW_PIN);
}

// ==== テスト用ループタスク（LED点滅） ====
static void userIO_task(void *pvParameters)
{
    while (1) {
        gpio_set_level(USER_LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(USER_LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ==== 公開関数 ====
void start_userIO_task(void)
{
    init_userIO();
    xTaskCreate(userIO_task, "UserIO_Task", 2048, NULL, 2, NULL);
}
