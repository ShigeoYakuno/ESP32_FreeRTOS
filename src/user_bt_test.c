#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "bluetooth_task.h"
#include "log_task.h"
#include "isr_func.h"
#include "enet_task.h"
#include "flash_data.h"
#include "user_bt_test.h"
#include "user_common.h"
#include "scale_cmd.h"



void user_bt_test_task(void *pv)
{
    syslog(INFO, "user_bt_test_task started (BT SPP RX)");

    char c;
    while (1)
    {
        // Bluetooth接続待ち
        if (!bt_connected) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // 受信待機
        if (xQueueReceive(qBtRx, &c, portMAX_DELAY))
        {
            switch (c)
            {
                case 'a':                                                       break;
                case 'b':                                                       break;
                case 'c':                                                       break;
                case 'd':   flashdata_dump_all();                               break;
                case 'e':   flashdata_clear_all();                              break;
                case 'E':   flash_force_erase_test();                           break;
                case 'i':   enet_set_ip(10);                                    break;
                case 'I':   enet_set_ip(30);                                    break;
                case 'l':   flashdata_load();                                   break;
                case 'r':   exec_soft_reset();                                  break;
                case 's':   flashdata_save();                                   break;

                case 'w':   test_span();                                       break;
                case 'x':   test_ad();                                         break;
                case 'y':                                                       break;
                case 'z':   test_zero();                                       break;

                case '\r': case '\n':                                           break;
                default:
                    syslog(INFO, "Unknown BT cmd: %c (0x%02X)", c, c);
                    break;
            }
        }
    }
}

void start_user_bt_test_task(void)
{
    xTaskCreatePinnedToCore(user_bt_test_task, "bt_test", 4096, NULL, 4, NULL, 1);
}
