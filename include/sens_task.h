// src/sens_task.h
#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif


// サンプル配信用キュー（必要に応じて aggregator などが受信）
QueueHandle_t sens_get_queue(void);

// タスク起動（app_main から1回だけ呼ぶ）
void start_sens_task(void);

// 任意操作（必要なら）
void sens_force_calibration(void);   // 再校正
void sens_set_rate_hz(uint16_t sps); // 10/20/40/80/320 のいずれか

#ifdef __cplusplus
}
#endif
