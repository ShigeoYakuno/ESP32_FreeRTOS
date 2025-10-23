#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "log_task.h"
#include "esp_private/esp_task_wdt.h"   



// ==== 定数・リソース ====
#define LOG_MSG_LEN   128
#define LOG_QUEUE_LEN 16


QueueHandle_t logQueue = NULL;
static TaskHandle_t logTaskHandle = NULL;
static TimerHandle_t logTimer = NULL;

// ==== 内部関数宣言 ====
static void log_task(void *pvParameters);
static void log_timer_cb(TimerHandle_t xTimer);

// ==== printfラッパ（通常タスクから）====
void log_printf(const char *fmt, ...)
{
    if (!logQueue) return;
    char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // タスクコンテキストから送信
    xQueueSend(logQueue, msg, 0);
}

// ==== ISR対応版（FromISRで呼べる）====
void log_printf_fromISR(const char *fmt, ...)
{
    if (!logQueue) return;
    char msg[LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(logQueue, msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}



/*コンテキスト自動判別付き。これに１本化する*/
void syslog(unsigned char mode, const char *fmt, ...)
{
    // --- 早期リターン条件 ---
    if (!logQueue) return;       // ログキュー未初期化時は無視
    if (mode < NO_FLUSH) return; // ログ抑制レベルより下なら出力しない

    char buf[LOG_MSG_LEN]; // 書式整形済みの本文用
    char msg[LOG_MSG_LEN]; // [ISR]/[TSK]タグ付きの最終出力用
    va_list args;

    // --- 書式展開 ---
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);  // フォーマット済み文字列をbufに格納
    va_end(args);

    // --- 現在の実行コンテキストを1回だけ判定 ---
    bool isIsr = xPortInIsrContext();

    // --- タグを選択して安全に結合 ---
    const char *prefix = isIsr ? "[ISR] " : "[TSK] ";
    size_t prefix_len = strlen(prefix);
    size_t remain = sizeof(msg) - prefix_len - 1; // 終端分を確保
    strncpy(msg, prefix, sizeof(msg));
    strncat(msg, buf, remain);
    msg[sizeof(msg) - 1] = '\0'; // 念のため終端保証

    // --- ログをキューに送信（コンテキストに応じて処理を分岐） ---
    if (isIsr) {
        // ISR（割り込みコンテキスト）から送信
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(logQueue, msg, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    } else {
        // 通常タスクコンテキストから送信
        xQueueSend(logQueue, msg, 0);
    }
}





// ==== タスク本体 ====
static void log_task(void *pvParameters)
{
    char msg[LOG_MSG_LEN];
    while (1) {
        if (xQueueReceive(logQueue, msg, portMAX_DELAY)) {
            printf("%s\n", msg);
        }
    }
}

/*
    周期タイマハンドラ
    FreeRTOS では、xTimerCreate() で作られたタイマーは
    Timer Service Taskによって管理されるのでタスクコンテキスト
*/
static void log_timer_cb(TimerHandle_t xTimer)
{
    //syslog(DEBUG,"LogTimer tick");
}

// ==== 初期化関数 ====
void start_log_task(void)
{
    logQueue = xQueueCreate(LOG_QUEUE_LEN, sizeof(char[LOG_MSG_LEN]));
    xTaskCreate(log_task, "LogTask", 4096, NULL, 2, &logTaskHandle);

    // 1秒周期で動作するソフトウェアタイマー
    logTimer = xTimerCreate("LogTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, log_timer_cb);
    if (logTimer) xTimerStart(logTimer, 0);
}
