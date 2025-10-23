#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_task.h"
#include "user_test.h"
#include "freertos/portmacro.h"
#include "esp_rom_sys.h"
#include "isr_func.h" 

#define TEST_UART_NUM UART_NUM_0
#define BUF_SIZE      128




void test_task(void *pvParameters)
{
    syslog(INFO, "test_task started (UART0 RX=GPIO3)");

    // UART0はESP-IDF標準で初期化済み（ログ出力に使用されるため）

    while (1) {
        int c = fgetc(stdin);  // UART0のRX(GPIO3)から入力を読む
        if (c != EOF) {
            switch (c) {
                case 'a':   testISR_1();                                        break;
                case 'b':   testISR_2();                                        break;
                case 'c':   testISR_3();                                        break;
                case 'r':   syslog(INFO, "Command [R] reset requested");        break;
                case '\r': case '\n': break; // 無視
                default: syslog(INFO, "Unknown command: %c (0x%02X)", c, c);    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // watchdog防止
    }
}

void start_test_task(void)
{
    xTaskCreatePinnedToCore(test_task, "test_task", 4096, NULL, 4, NULL, 1);
}
