#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "wifi_task.h"
#include "log_task.h" 

// ==== 内部シンボル ====
static EventGroupHandle_t s_wifi_event_group;
static TaskHandle_t mainTaskHandle_ = NULL;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// ==== Wi-Fiイベントハンドラ ====
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        syslog(DEBUG_WIFI,"Wi-Fi start → connecting...");
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        syslog(DEBUG_WIFI,"Wi-Fi disconnected → retry");
        esp_wifi_connect();
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        syslog(DEBUG_WIFI,"Got IP: %d.%d.%d.%d",
            IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ==== NTPで初回のみ時刻取得 ====
static bool obtain_time_once(void)
{
    setenv("TZ", "JST-9", 1);
    tzset();

    esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.nict.jp");
    esp_sntp_setservername(1, "ntp.jst.mfeed.ad.jp");
    esp_sntp_setservername(2, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm info = {0};
    int retry = 0, max_retry = 20;

    while (retry++ < max_retry) {
        time(&now);
        localtime_r(&now, &info);
        if (info.tm_year >= (2020 - 1900)) {
            syslog(DEBUG_WIFI,"NTP sync OK: %04d-%02d-%02d %02d:%02d:%02d",
                       info.tm_year + 1900, info.tm_mon + 1, info.tm_mday,
                       info.tm_hour, info.tm_min, info.tm_sec);
            esp_sntp_stop();
            return true;
        }
        syslog(DEBUG_WIFI,"Waiting NTP... (%d/%d)", retry, max_retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    syslog(DEBUG_WIFI,"NTP sync timeout (no update)");
    esp_sntp_stop();
    return false;
}

// ==== Wi-Fiタスク本体 ====
static void wifi_task(void *pvParameters)
{
    syslog(DEBUG_WIFI,"Wi-Fi init sequence start");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "hikaruAP",
            .password = "hikaru0405",
        },
    };

    syslog(DEBUG_WIFI,"Connecting to SSID: %s", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wi-Fi接続待機（15秒）
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(15000)
    );

    if (bits & WIFI_CONNECTED_BIT) {
        syslog(DEBUG_WIFI,"Wi-Fi connected, start NTP sync");
        vTaskDelay(pdMS_TO_TICKS(2000)); // DNS安定待ち
        bool ok = obtain_time_once();

        if (ok && mainTaskHandle_ != NULL) {
            xTaskNotify(mainTaskHandle_, 0x1234, eSetValueWithOverwrite);
        }
    } else {
        syslog(DEBUG_WIFI,"Wi-Fi connect timeout, skip NTP");
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    syslog(DEBUG_WIFI,"Wi-Fi stopped (time sync done or skipped)");

    //wifiだけ自動でcore0に割当たる
    printf("WiFi task running on core %d\n", xPortGetCoreID());

    vTaskDelete(NULL);
}

// ==== 公開関数 ====
void start_wifi_task(TaskHandle_t mainTaskHandle)
{
    mainTaskHandle_ = mainTaskHandle;
    xTaskCreate(wifi_task, "WiFiTask", 8192, NULL, 3, NULL);
}
