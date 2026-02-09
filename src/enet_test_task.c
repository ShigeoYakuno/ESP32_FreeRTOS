#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" 
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>
#include "enet_test_task.h"
#include "user_io.h"


extern SemaphoreHandle_t g_msg_mutex;  // 共有メッセージ保護
extern char g_last_udp_msg[128];

#define LED_GPIO   GPIO_NUM_4
static const char *TAG = "enet_test";

static void enet_test_task(void *pvParameters)
{
    ESP_LOGI(TAG, "enet_test_task started (LED on GPIO%d)", LED_GPIO);

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    char local_copy[128];

    // ★★★ 追加: g_msg_mutex 初期化完了を待つ（ここ重要）★★★
    while (g_msg_mutex == NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    for (;;)
    {
        // ★ タイムアウト付きでTryTake（未取得ならスキップしてyield）
        if (xSemaphoreTake(g_msg_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            // 受信メッセージを取り出してクリア
            if (g_last_udp_msg[0] != '\0') {
                strcpy(local_copy, g_last_udp_msg);
                g_last_udp_msg[0] = '\0';
            } else {
                local_copy[0] = '\0';
            }
            xSemaphoreGive(g_msg_mutex);
        } else {
            local_copy[0] = '\0';
        }

        if (local_copy[0] != '\0') {
            ESP_LOGI(TAG, "Received UDP command: %s", local_copy);

            // 大文字化
            for (int i = 0; local_copy[i]; i++) {
                local_copy[i] = (char)toupper((unsigned char)local_copy[i]);
            }

            switch (local_copy[0]) {
                case 'L':
                    if (strcmp(local_copy, "LED_ON") == 0) {
                        gpio_set_level(LED_GPIO, 1);
                        ESP_LOGI(TAG, "LED ON");
                    } else if (strcmp(local_copy, "LED_OFF") == 0) {
                        gpio_set_level(LED_GPIO, 0);
                        ESP_LOGI(TAG, "LED OFF");
                    } else {
                        ESP_LOGW(TAG, "Unknown LED command: %s", local_copy);
                    }
                    break;

                case 'R':
                    if (strcmp(local_copy, "RESET") == 0) {
                        ESP_LOGI(TAG, "System reset requested");
                        // esp_restart();
                    } else {
                        ESP_LOGW(TAG, "Unknown R command: %s", local_copy);
                    }
                    break;

                case 'S':
                    if (strcmp(local_copy, "STATUS") == 0) {
                        ESP_LOGI(TAG, "STATUS: LED=%d", gpio_get_level(LED_GPIO));
                    } else {
                        ESP_LOGW(TAG, "Unknown S command: %s", local_copy);
                    }
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown command: %s", local_copy);
                    break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void start_enet_test_task(void)
{
    xTaskCreatePinnedToCore(enet_test_task, "enet_test_task", 4096, NULL, 4, NULL, 1);
}
