#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_task.h"
#include "panic_handler.h"

// メモリ監視設定
#define MEMORY_MONITOR_TASK_STACK_SIZE 2048
#define MEMORY_MONITOR_INTERVAL_MS 30000  // 30秒ごとにチェック
#define MEMORY_LOW_THRESHOLD_BYTES 10000  // 10KB以下で警告
#define MEMORY_CRITICAL_THRESHOLD_BYTES 5000  // 5KB以下でリセット

// スタックオーバーフローフック（FreeRTOSから呼ばれる）
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    syslog(ERR, "=== STACK OVERFLOW DETECTED ===");
    syslog(ERR, "Task: %s", pcTaskName ? pcTaskName : "Unknown");
    syslog(ERR, "Task handle: %p", xTask);
    
    // メモリ情報を出力
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    syslog(ERR, "Free heap: %zu bytes", free_heap);
    syslog(ERR, "Min free heap: %zu bytes", min_free_heap);
    
    syslog(ERR, "System will restart in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // システムリセット
    esp_restart();
    
    // 到達しないはずだが、念のため
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// malloc失敗フック（FreeRTOSから呼ばれる）
void vApplicationMallocFailedHook(void)
{
    syslog(ERR, "=== MALLOC FAILED ===");
    syslog(ERR, "Free heap: %zu bytes", esp_get_free_heap_size());
    syslog(ERR, "Min free heap: %zu bytes", esp_get_minimum_free_heap_size());
    
    syslog(ERR, "System will restart in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // システムリセット
    esp_restart();
    
    // 到達しないはずだが、念のため
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// メモリ監視タスク（定期的にメモリ使用状況をチェック）
static void memory_monitor_task(void *pvParameters)
{
    syslog(INFO, "Memory monitor task started");
    
    size_t last_free_heap = 0;
    int consecutive_low_memory_count = 0;
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MEMORY_MONITOR_INTERVAL_MS));
        
        size_t free_heap = esp_get_free_heap_size();
        size_t min_free_heap = esp_get_minimum_free_heap_size();
        
        // メモリが減少しているかチェック
        if (last_free_heap > 0 && free_heap < last_free_heap - 1000) {
            syslog(WARN, "Memory decreased: %zu -> %zu bytes (delta: %d)", 
                   last_free_heap, free_heap, (int)(last_free_heap - free_heap));
        }
        
        // 低メモリ警告
        if (free_heap < MEMORY_LOW_THRESHOLD_BYTES) {
            syslog(WARN, "Low memory warning: %zu bytes (threshold: %d)", 
                   free_heap, MEMORY_LOW_THRESHOLD_BYTES);
            consecutive_low_memory_count++;
        } else {
            consecutive_low_memory_count = 0;
        }
        
        // クリティカルなメモリ不足
        if (free_heap < MEMORY_CRITICAL_THRESHOLD_BYTES) {
            syslog(ERR, "=== CRITICAL MEMORY SHORTAGE ===");
            syslog(ERR, "Free heap: %zu bytes (threshold: %d)", 
                   free_heap, MEMORY_CRITICAL_THRESHOLD_BYTES);
            syslog(ERR, "Min free heap: %zu bytes", min_free_heap);
            syslog(ERR, "System will restart in 3 seconds...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            esp_restart();
        }
        
        // 連続して低メモリが続く場合（メモリリークの可能性）
        if (consecutive_low_memory_count >= 3) {
            syslog(ERR, "=== POSSIBLE MEMORY LEAK DETECTED ===");
            syslog(ERR, "Low memory for %d consecutive checks", consecutive_low_memory_count);
            syslog(ERR, "Free heap: %zu bytes", free_heap);
            syslog(ERR, "Min free heap: %zu bytes", min_free_heap);
            syslog(ERR, "System will restart in 5 seconds...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_restart();
        }
        
        last_free_heap = free_heap;
    }
}

// メモリ監視タスクを起動
void start_memory_monitor_task(void)
{
    xTaskCreate(memory_monitor_task, "MemMonitor", MEMORY_MONITOR_TASK_STACK_SIZE, 
                NULL, 1, NULL);
    syslog(INFO, "Memory monitor task created");
}
