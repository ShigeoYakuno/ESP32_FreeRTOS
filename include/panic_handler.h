#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// スタックオーバーフローフック（FreeRTOSから呼ばれる）
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);

// malloc失敗フック（FreeRTOSから呼ばれる）
void vApplicationMallocFailedHook(void);

// メモリ監視タスクを起動
void start_memory_monitor_task(void);

#ifdef __cplusplus
}
#endif
