#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_spp_api.h"     // ← これがないと esp_spp_write が見えない
#include "log_task.h"
#include "bluetooth_task.h"  // bt_handle, bt_connected を参照

#define LOG_QUEUE_LEN 16

QueueHandle_t logQueue = NULL;
static TaskHandle_t logTaskHandle = NULL;
static TimerHandle_t logTimer = NULL;

// ==== 内部関数宣言 ====
static void log_task(void *pvParameters);
static void log_timer_cb(TimerHandle_t xTimer);


// --- log_task.c 先頭付近 ---
static int syslog_force_mode = -1; // -1=自動, 0=UART, 1=BT

void syslog_set_output(int mode)
{
    syslog_force_mode = mode;
}

// ==== printfラッパ（通常タスク） ====
void log_printf(const char *fmt, ...)
{
    if (!logQueue) return;
    char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    xQueueSend(logQueue, msg, 0);
}

// ==== ISR対応版 ====
void log_printf_fromISR(const char *fmt, ...)
{
    if (!logQueue) return;
    char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    BaseType_t hpTaskWoken = pdFALSE;
    xQueueSendFromISR(logQueue, msg, &hpTaskWoken);
    portYIELD_FROM_ISR(hpTaskWoken);
}

// ==== 共通ログ関数 ====
void syslog(unsigned char mode, const char *fmt, ...)
{
    if (!logQueue) return;
    if (mode < NO_FLUSH) return;

    char msg[LOG_MSG_LEN];
    va_list args;

    // 書式展開（vsnprintfを2段構成にしない）
    va_start(args, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    if (n < 0) return;  // フォーマット失敗
    if (n >= sizeof(msg)) msg[sizeof(msg) - 1] = '\0';

    // 実行コンテキスト検出
    bool isIsr = xPortInIsrContext();

    // prefixを安全に前方追加（バッファ再利用）
    const char *prefix = isIsr ? "[ISR] " : "[TSK] ";
    size_t len_prefix = strlen(prefix);
    size_t len_msg = strlen(msg);
    if (len_prefix + len_msg + 1 < sizeof(msg)) {
        memmove(msg + len_prefix, msg, len_msg + 1);
        memcpy(msg, prefix, len_prefix);
    }

    // キュー送信
    if (isIsr) {
        BaseType_t hpTaskWoken = pdFALSE;
        xQueueSendFromISR(logQueue, msg, &hpTaskWoken);
        portYIELD_FROM_ISR(hpTaskWoken);
    } else {
        xQueueSend(logQueue, msg, 0);
    }
}


// ==== ログ出力タスク ====
static void log_task(void *pvParameters)
{
    char msg[LOG_MSG_LEN];

    while (1) {
        if (xQueueReceive(logQueue, msg, portMAX_DELAY)) {

            // SPP接続中はBluetooth送信、それ以外はUART出力
            if (bt_connected && bt_handle) {
                size_t len = strnlen(msg, LOG_MSG_LEN);
                esp_spp_write(bt_handle, len, (uint8_t *)msg);
                esp_spp_write(bt_handle, 1, (uint8_t *)"\n");
            } else {
                printf("%s\n", msg);
            }
        }
    }
}

// ==== タイマーコールバック ====
static void log_timer_cb(TimerHandle_t xTimer)
{
    // syslog(DEBUG, "LogTimer tick");
}

// ==== 初期化 ====
void start_log_task(void)
{
    logQueue = xQueueCreate(LOG_QUEUE_LEN, sizeof(char[LOG_MSG_LEN]));
    // Stack size reduced: 8192 -> 5120 (IRAM saving)
    xTaskCreate(log_task, "LogTask", 5120, NULL, 3, &logTaskHandle);

    logTimer = xTimerCreate("LogTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, log_timer_cb);
    if (logTimer) xTimerStart(logTimer, 0);
}

