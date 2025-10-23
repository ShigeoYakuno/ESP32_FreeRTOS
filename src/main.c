#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "wifi_task.h"
#include "log_task.h"
#include "esp_log.h" 
#include "user_io.h" 
#include "user_test.h"
#include "isr_func.h" 


static TaskHandle_t mainTaskHandle = NULL;

void main_task(void *pvParameters)
{
    char timeMsg[64] = "Waiting NTP...";

    while (1) {
        // Wi-Fiタスクから通知を待つ
        uint32_t notifyValue;
        if (xTaskNotifyWait(0, 0, &notifyValue, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // 受け取ったtimeStrポインタを使う（安全のためコピー）
            strncpy(timeMsg, (const char *)notifyValue, sizeof(timeMsg));
            timeMsg[sizeof(timeMsg) - 1] = '\0';
        }

        // 1秒おきにログタスクへ送信
        xQueueSend(logQueue, timeMsg, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
/*
app_mainがesp-idfのエントリーポイント
reset vector⇒esp_startup.c⇒main_task()⇒app_main() 
の順で実行される
*/

void app_main(void)
{
    start_log_task();
    syslog(DEBUG,"System start");
    start_userIO_task();
    start_test_task();


    //wifi起動しない場合だいぶリソース軽くなる
    //start_wifi_task(NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


