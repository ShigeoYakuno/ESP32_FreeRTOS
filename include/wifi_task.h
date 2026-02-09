#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// temp_sens_data_tの型定義（前方宣言として使用）
struct temp_sens_data;
typedef struct temp_sens_data temp_sens_data_t;

extern TaskHandle_t wifiTaskHandle;

void start_wifi_task(TaskHandle_t mainTaskHandle);

// データ送信用関数
QueueHandle_t wifi_get_data_queue(void);
bool wifi_send_data(uint32_t data);  // 既存互換性用
bool wifi_send_temp_sens_data(const temp_sens_data_t *data);  // 新規構造体用

// Wi-Fi接続状態取得関数
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
