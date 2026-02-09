#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_task.h"
#include "user_test.h"
#include "freertos/portmacro.h"
#include "esp_rom_sys.h"
#include "isr_func.h" 
#include "enet_task.h" 
#include "flash_data.h"
#include "user_common.h"
#include "sd_task.h"
#include "scale_cmd.h"
#include "version.h"

#define TEST_UART_NUM UART_NUM_0
#define BUF_SIZE      128




static void sd_write_test(void)
{
    char line[128];
    snprintf(line, sizeof(line),"SDTEST,%lu",(unsigned long)xTaskGetTickCount());
    bool ok = sd_enqueue_line(line);
    syslog(INFO, ok ? "SD write queued: %s" : "SD queue full", line);
}



static void setBTdeviceNo(void)
{
    syslog(INFO, "Enter BT Device Number (00-99):");

    char buf[4] = {0};
    int i = 0;
    while (i < 2) {
        int ch = fgetc(stdin);
        if (ch >= '0' && ch <= '9') buf[i++] = (char)ch;
        else if (ch == '\r' || ch == '\n') break;
    }
    buf[i] = '\0';
    int n = atoi(buf);

    setBtDevNum(n);

    syslog(INFO, "BT Device name changed to PE_DEV_%02d", n);
}



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
                case 'd':   flashdata_dump_all();                               break;
                case 'e':   flashdata_clear_all();                              break;
                case 'E':   flash_force_erase_test();                           break;
                case 'f':                                                       break;
                case 'g':                                                       break;
                case 'h':                                                       break;
                case 'i':   enet_set_ip(10);                                    break;
                case 'I':   enet_set_ip(30);                                    break;
                case 'j':                                                       break;
                case 'k':                                                       break;
                case 'l':   flashdata_load();                                   break;
                case 'm':                                                       break;
                case 'n':   setBtDevNum(1);                                     break;
                case 'N':   setBTdeviceNo();                                    break;
                case 'o':   setBtDevNum(2);                                     break;
                case 'p':   setBtDevNum(3);                                     break;
                case 'q':                                                       break;
                case 'r':   exec_soft_reset();                                  break;
                case 's':   flashdata_save();                                   break;
                case 't':   sd_write_test();                                    break;
                case 'u':   sd_request_flashdata_export();                      break;
                case 'v':   syslog(INFO, "Version: %s", version);               break;
                case 'w':   test_span();                                                    break;
                case 'x':   test_ad();                                                    break;
                case 'y':                                                       break;
                case 'z':   test_zero();                                                    break;
                
                case '1':   
                    setWifiDevNo(1);
                    flashdata_save();
                    syslog(INFO, "Device number set to 1");
                    break;
                case '2':   
                    setWifiDevNo(2);
                    flashdata_save();
                    syslog(INFO, "Device number set to 2");
                    break;
                case '3':   
                    setWifiDevNo(3);
                    flashdata_save();
                    syslog(INFO, "Device number set to 3");
                    break;
                case '4':   
                    setWifiDevNo(4);
                    flashdata_save();
                    syslog(INFO, "Device number set to 4");
                    break;
                
                // SSID number setting commands (A,B,C,D -> 0,1,2,3)
                case 'A':   
                    setSsidNo(0);
                    flashdata_save();
                    syslog(INFO, "SSID number set to 0 (PE_IOT_GATEWAY_0)");
                    break;
                case 'B':   
                    setSsidNo(1);
                    flashdata_save();
                    syslog(INFO, "SSID number set to 1 (PE_IOT_GATEWAY_1)");
                    break;
                case 'C':   
                    setSsidNo(2);
                    flashdata_save();
                    syslog(INFO, "SSID number set to 2 (PE_IOT_GATEWAY_2)");
                    break;
                case 'D':   
                    setSsidNo(3);
                    flashdata_save();
                    syslog(INFO, "SSID number set to 3 (PE_IOT_GATEWAY_3)");
                    break;
                
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
