#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"  // IP2STRマクロ用
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "esp_log.h"
#include "esp_system.h"
#include "wifi_task.h"
#include "temp_sens_task.h"  // temp_sens_data_t定義用
#include "log_task.h"
#include "user_io.h"  // get_child_device_no()用
#include "flash_data.h"  // getSsidNo()用

// ==== 内部シンボル ====
static EventGroupHandle_t s_wifi_event_group;
static TaskHandle_t mainTaskHandle_ = NULL;
static QueueHandle_t s_data_queue = NULL;
static TaskHandle_t s_data_send_task = NULL;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// ==== 親機AP設定（仕様書準拠） ====
#define PARENT_AP_SSID_BASE "PE_IOT_GATEWAY_"  // SSIDベース名
#define PARENT_AP_PASSWORD "12345678"
#define PARENT_AP_SSID_MAX_LEN 32  // SSID最大長

// ==== 子機設定（仕様書準拠） ====
#define PARENT_AP_IP       "192.168.4.1"    // 親機IPアドレス（ゲートウェイ）
#define PARENT_AP_PORT     50000            // UDP送信先ポート
#define SUBNET_MASK        "255.255.255.0"  // サブネットマスク
#define STATIC_IP_BASE     192, 168, 4, 10  // 子機IPベース（10 + 子機No）
#define DATA_QUEUE_SIZE    10               // データキューのサイズ

// ==== 再接続設定 ====
#define RECONNECT_MAX_RETRY 20  // 最大リトライ回数（20回連続失敗でリブート）
#define RECONNECT_SCAN_RETRY 5  // スキャン失敗時のリトライ回数
#define CONNECTION_TIMEOUT_MS 10000  // 接続タイムアウト（10秒）
#define MIN_SCAN_INTERVAL_MS 10000  // スキャン処理の最小間隔（10秒）- スタックオーバーフロー防止

// ==== MACアドレス表示用マクロ ====
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

// ==== グローバル状態 ====
static uint8_t s_child_no = 0;           // 子機No（1-4）
static int s_reconnect_fail_count = 0;   // 連続失敗カウント
static bool s_is_connected = false;      // Wi-Fi接続状態フラグ
static esp_netif_t *s_sta_netif = NULL;  // STA用ネットインターフェース
static TickType_t s_last_scan_time = 0;  // 最後にスキャンした時刻（スタックオーバーフロー防止）

// ==== Wi-Fiイベントハンドラ（STAモード用） ====
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        syslog(DEBUG_WIFI, "Wi-Fi STA started");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        syslog(DEBUG_WIFI, "Wi-Fi disconnected: reason=%d", event->reason);
        
        s_is_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        
        // 再接続試行（イベントハンドラ内では時間のかかる処理を避けるため、タスク側で処理）
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        syslog(DEBUG_WIFI, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        
        s_is_connected = true;
        s_reconnect_fail_count = 0;  // 成功時に失敗カウントをリセット
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }
}

// ==== 前方宣言 ====
static void data_send_task(void *pvParameters);

// ==== SSID生成関数 ====
static void get_parent_ap_ssid(char *ssid_buf, size_t buf_size)
{
    uint32_t ssid_no = getSsidNo();
    if (ssid_no > 3) {
        ssid_no = 0;  // 安全のため、範囲外の場合は0に設定
    }
    snprintf(ssid_buf, buf_size, "%s%d", PARENT_AP_SSID_BASE, (int)ssid_no);
}

// ==== 静的IP設定関数 ====
static esp_err_t set_static_ip(uint8_t child_no)
{
    if (s_sta_netif == NULL) {
        return ESP_FAIL;
    }
    
    // 子機Noに基づいてIPアドレスを計算（192.168.4.(10 + N)）
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "192.168.4.%d", 10 + child_no);
    
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    esp_netif_dns_info_t dns_info;
    
    inet_pton(AF_INET, ip_str, &ip_info.ip);
    inet_pton(AF_INET, PARENT_AP_IP, &ip_info.gw);  // ゲートウェイ = 親機IP
    inet_pton(AF_INET, SUBNET_MASK, &ip_info.netmask);
    inet_pton(AF_INET, PARENT_AP_IP, &dns_info.ip.u_addr.ip4);  // DNS = 親機IP（未使用だが設定）
    
    // DHCPを停止して静的IPを設定
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(s_sta_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(s_sta_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info));
    
    syslog(DEBUG_WIFI, "Static IP set: " IPSTR ", Gateway: " IPSTR ", Netmask: " IPSTR,
           IP2STR(&ip_info.ip), IP2STR(&ip_info.gw), IP2STR(&ip_info.netmask));
    
    return ESP_OK;
}

// ==== Wi-Fiスキャン関数 ====
// スタックオーバーフロー防止のため、最小間隔を設けてスキャン
static bool scan_for_parent_ap(void)
{
    // 最小間隔チェック（スタックオーバーフロー防止）
    TickType_t current_time = xTaskGetTickCount();
    if (s_last_scan_time != 0) {
        TickType_t elapsed = current_time - s_last_scan_time;
        if (elapsed < pdMS_TO_TICKS(MIN_SCAN_INTERVAL_MS)) {
            // 最小間隔に達していない場合はスキャンをスキップ
            syslog(DEBUG_WIFI, "Scan skipped: too soon (elapsed=%lu ms)", 
                   (unsigned long)(elapsed * portTICK_PERIOD_MS));
            return false;
        }
    }
    
    s_last_scan_time = current_time;
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        }
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);  // ブロッキングスキャン
    if (ret != ESP_OK) {
        syslog(DEBUG_WIFI, "Wi-Fi scan start failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    
    if (ap_count == 0) {
        syslog(DEBUG_WIFI, "No AP found in scan");
        return false;
    }
    
    // 動的にSSIDを生成（関数の先頭で宣言）
    char target_ssid[PARENT_AP_SSID_MAX_LEN];
    get_parent_ap_ssid(target_ssid, sizeof(target_ssid));
    
    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_records == NULL) {
        syslog(DEBUG_WIFI, "Failed to allocate memory for AP records");
        return false;
    }
    
    // メモリ確保成功時は必ずfreeする（エラー時も含む）
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_records);
    bool found = false;
    
    if (ret == ESP_OK) {
        for (uint16_t i = 0; i < ap_count; i++) {
            if (strcmp((char *)ap_records[i].ssid, target_ssid) == 0) {
                syslog(DEBUG_WIFI, "Found parent AP: %s, RSSI: %d", ap_records[i].ssid, ap_records[i].rssi);
                found = true;
                break;
            }
        }
    } else {
        syslog(DEBUG_WIFI, "Failed to get AP records: %s", esp_err_to_name(ret));
    }
    
    // 必ずメモリを解放（エラー時も含む）
    free(ap_records);
    ap_records = NULL;  // ダブルフリー防止
    
    if (!found) {
        syslog(DEBUG_WIFI, "Parent AP '%s' not found in scan", target_ssid);
    }
    
    return found;
}

// ==== Wi-Fi再接続処理 ====
static bool wifi_reconnect(void)
{
    syslog(DEBUG_WIFI, "Attempting to reconnect to parent AP...");
    
    // Wi-Fiスキャンを実行して親機APを探す
    bool found = scan_for_parent_ap();
    if (!found) {
        syslog(DEBUG_WIFI, "Parent AP not found, retry scan...");
        // スキャンをリトライ
        for (int i = 0; i < RECONNECT_SCAN_RETRY; i++) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            found = scan_for_parent_ap();
            if (found) break;
        }
    }
    
    if (!found) {
        char target_ssid[PARENT_AP_SSID_MAX_LEN];
        get_parent_ap_ssid(target_ssid, sizeof(target_ssid));
        syslog(DEBUG_WIFI, "Parent AP '%s' not found after scan retries", target_ssid);
        return false;
    }
    
    // Wi-Fi接続を試行
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        syslog(DEBUG_WIFI, "Wi-Fi connect failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 接続完了待機（イベントで通知される）
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdTRUE,  // 取得したビットをクリア
        pdFALSE,  // 両方のビットが立つ必要はない
        pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS)
    );
    
    if (bits & WIFI_CONNECTED_BIT) {
        syslog(DEBUG_WIFI, "Wi-Fi reconnected successfully");
        s_reconnect_fail_count = 0;  // 成功時にリセット
        
        // データ送信タスクが起動していない場合は起動（念のため）
        // タスクが既に存在するか確認（重複作成を防ぐ）
        bool task_exists = false;
        if (s_data_send_task != NULL) {
            eTaskState task_state = eTaskGetState(s_data_send_task);
            if (task_state != eDeleted && task_state != eInvalid) {
                task_exists = true;
            } else {
                // タスクが削除されている場合はハンドルをクリア
                s_data_send_task = NULL;
            }
        }
        
        if (!task_exists) {
            xTaskCreate(data_send_task, "UdpDataSendTask", 2048, NULL, 2, &s_data_send_task);
            syslog(DEBUG_WIFI, "UDP data send task started after reconnection");
        }
        
        return true;
    } else {
        syslog(DEBUG_WIFI, "Wi-Fi reconnection failed");
        return false;
    }
}

// ==== UDPデータ送信タスク ====
static int s_udp_sock = -1;
static struct sockaddr_in s_server_addr;

static void data_send_task(void *pvParameters)
{
    syslog(DEBUG_WIFI, "UDP data send task started");
    
    // サーバーアドレスを事前設定（UDPなので接続不要）
    memset(&s_server_addr, 0, sizeof(s_server_addr));
    s_server_addr.sin_family = AF_INET;
    s_server_addr.sin_port = htons(PARENT_AP_PORT);
    inet_pton(AF_INET, PARENT_AP_IP, &s_server_addr.sin_addr);
    
    while (1) {
        temp_sens_data_t data;
        
        // キューからデータを受信（タイムアウト: 1秒）
        if (xQueueReceive(s_data_queue, &data, pdMS_TO_TICKS(1000)) == pdTRUE) {
            
            // Wi-Fi接続確認
            EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
            if (!(bits & WIFI_CONNECTED_BIT)) {
                syslog(DEBUG_WIFI, "WiFi not connected, skipping data send");
                // UDPソケットは接続不要なので、クリアしない
                continue;
            }
            
            // UDPソケットが未作成の場合は作成
            if (s_udp_sock < 0) {
                s_udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
                if (s_udp_sock < 0) {
                    syslog(DEBUG_WIFI, "Failed to create UDP socket");
                    continue;
                }
                
                // 送信タイムアウト設定（3秒）
                struct timeval timeout;
                timeout.tv_sec = 3;
                timeout.tv_usec = 0;
                setsockopt(s_udp_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            }
            
            // RSSI取得（接続中のAPから）
            int8_t rssi = 0;
            wifi_ap_record_t ap_info;
            esp_err_t rssi_ret = esp_wifi_sta_get_ap_info(&ap_info);
            if (rssi_ret == ESP_OK) {
                rssi = ap_info.rssi;
            } else {
                // RSSI取得失敗時は0を設定（または前回値を保持する実装も可）
                syslog(DEBUG_WIFI, "Failed to get RSSI: %s", esp_err_to_name(rssi_ret));
            }
            
            // データ送信（JSON形式 + 子機Noを先頭に追加 + RSSI追加）
            // フォーマット: {"child_no":1,"aht_t01":253,"aht_rh01":482,"bmp_t01":251,"bmp_p01":100845,"aht_ok":true,"bmp_ok":true,"seq":123,"rssi":-65}\n
            char send_buf[180];  // RSSIフィールド追加により拡張
            int len = snprintf(send_buf, sizeof(send_buf),
                "{\"child_no\":%u,\"aht_t01\":%d,\"aht_rh01\":%u,\"bmp_t01\":%d,\"bmp_p01\":%lu,\"aht_ok\":%s,\"bmp_ok\":%s,\"seq\":%lu,\"rssi\":%d}\n",
                (unsigned int)s_child_no,
                (int)data.aht_t01,
                (unsigned int)data.aht_rh01,
                (int)data.bmp_t01,
                (unsigned long)data.bmp_p01,
                data.aht_ok ? "true" : "false",
                data.bmp_ok ? "true" : "false",
                (unsigned long)data.seq,
                (int)rssi);
            
            // UDP送信（connect不要、sendtoを使用）
            int sent = sendto(s_udp_sock, send_buf, len, 0,
                             (struct sockaddr *)&s_server_addr, sizeof(s_server_addr));
            
            if (sent < 0) {
                syslog(DEBUG_WIFI, "Failed to send UDP data");
                // UDP送信失敗はソケットを閉じない（接続がないため）
            } else {
                syslog(DEBUG_WIFI, "Sent UDP data: child_no=%u, seq=%lu", 
                       (unsigned int)s_child_no, (unsigned long)data.seq);
            }
        }
    }
}

// ==== Wi-Fiタスク本体（STAモード） ====
static void wifi_task(void *pvParameters)
{
    syslog(DEBUG_WIFI, "Wi-Fi STA init sequence start");
    
    // 子機Noを取得
    s_child_no = get_child_device_no();
    if (s_child_no < 1 || s_child_no > 4) {
        syslog(DEBUG_WIFI, "Invalid child_no: %u (must be 1-4), using 1", (unsigned int)s_child_no);
        s_child_no = 1;  // 安全のためデフォルト値
    }
    syslog(DEBUG_WIFI, "Child device number: %u", (unsigned int)s_child_no);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // STA用のネットインターフェースを作成
    s_sta_netif = esp_netif_create_default_wifi_sta();
    if (s_sta_netif == NULL) {
        syslog(DEBUG_WIFI, "Failed to create STA netif");
        vTaskDelete(NULL);
        return;
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_wifi_event_group = xEventGroupCreate();
    
    // イベントハンドラ登録
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // データ送信用キューを作成
    s_data_queue = xQueueCreate(DATA_QUEUE_SIZE, sizeof(temp_sens_data_t));
    if (s_data_queue == NULL) {
        syslog(DEBUG_WIFI, "Failed to create data queue");
        vTaskDelete(NULL);
        return;
    }

    // STAモード設定
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // 動的にSSIDを生成
    char target_ssid[PARENT_AP_SSID_MAX_LEN];
    get_parent_ap_ssid(target_ssid, sizeof(target_ssid));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = {0},
            .password = PARENT_AP_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    // SSIDをコピー（最大32文字）
    strncpy((char *)wifi_config.sta.ssid, target_ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // 静的IP設定（接続前に設定）
    set_static_ip(s_child_no);
    
    ESP_ERROR_CHECK(esp_wifi_start());
    
    syslog(DEBUG_WIFI, "Wi-Fi STA started, connecting to '%s'", target_ssid);
    
    // 親機APが見つかるまでスキャンを繰り返す（親機が後から起動する場合に対応）
    bool ap_found = false;
    int scan_attempts = 0;
    const int MAX_INITIAL_SCAN_ATTEMPTS = 30;  // 最大30回（約30秒）スキャンを試行
    
    syslog(DEBUG_WIFI, "Waiting for parent AP '%s' to become available...", target_ssid);
    
    while (!ap_found && scan_attempts < MAX_INITIAL_SCAN_ATTEMPTS) {
        ap_found = scan_for_parent_ap();
        if (!ap_found) {
            scan_attempts++;
            if (scan_attempts % 5 == 0) {  // 5回ごとにログ出力
                syslog(DEBUG_WIFI, "Parent AP not found yet (attempt %d/%d), retrying...", 
                       scan_attempts, MAX_INITIAL_SCAN_ATTEMPTS);
            }
            vTaskDelay(pdMS_TO_TICKS(1000));  // 1秒待機してから再スキャン
        }
    }
    
    if (!ap_found) {
        syslog(DEBUG_WIFI, "Parent AP '%s' not found after %d scan attempts, continuing anyway...", 
               target_ssid, MAX_INITIAL_SCAN_ATTEMPTS);
        // APが見つからなくても接続を試行（親機が起動中かもしれない）
    } else {
        syslog(DEBUG_WIFI, "Parent AP '%s' found, attempting connection...", target_ssid);
    }
    
    // データ送信タスクを最初から起動（接続状態はタスク内で監視）
    // これにより、子機が先に起動して親機が後から起動する場合でも確実にデータ送信できる
    // タスクが既に存在するか確認（重複作成を防ぐ）
    bool task_exists = false;
    if (s_data_send_task != NULL) {
        eTaskState task_state = eTaskGetState(s_data_send_task);
        if (task_state != eDeleted && task_state != eInvalid) {
            task_exists = true;
        } else {
            // タスクが削除されている場合はハンドルをクリア
            s_data_send_task = NULL;
        }
    }
    
    if (!task_exists) {
        xTaskCreate(data_send_task, "UdpDataSendTask", 2048, NULL, 2, &s_data_send_task);
        syslog(DEBUG_WIFI, "UDP data send task started (will wait for connection)");
    }
    
    // 初期接続試行（イベントハンドラで処理される）
    esp_wifi_connect();
    
    // 接続完了待機
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS)
    );
    
    if (bits & WIFI_CONNECTED_BIT) {
        syslog(DEBUG_WIFI, "Wi-Fi connected successfully");
        
        if (mainTaskHandle_ != NULL) {
            xTaskNotify(mainTaskHandle_, 0x1234, eSetValueWithOverwrite);
        }
    } else {
        syslog(DEBUG_WIFI, "Wi-Fi initial connection failed (will retry in main loop)");
        s_reconnect_fail_count++;
    }
    
    syslog(DEBUG_WIFI, "WiFi STA task running on core %d", xPortGetCoreID());

    // メインループ（再接続監視）
    // 子機が常時電源ONで、親機が後から起動する場合にも対応
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5秒ごとに状態を確認
        
        bits = xEventGroupGetBits(s_wifi_event_group);
        
        if (bits & WIFI_CONNECTED_BIT) {
            // 接続中
            s_reconnect_fail_count = 0;  // 接続中は失敗カウントをリセット
        } else {
            // 未接続 → 再接続試行
            syslog(DEBUG_WIFI, "Wi-Fi disconnected, attempting reconnect (fail_count=%d)", s_reconnect_fail_count);
            
            // スタックオーバーフロー防止のため、メインループ内ではスキャンを1回だけ実行
            // 詳細なスキャンはwifi_reconnect()内で実行される
            bool ap_found = scan_for_parent_ap();
            
            // 動的にSSIDを生成
            char reconnect_ssid[PARENT_AP_SSID_MAX_LEN];
            get_parent_ap_ssid(reconnect_ssid, sizeof(reconnect_ssid));
            
            if (!ap_found) {
                syslog(DEBUG_WIFI, "Parent AP '%s' not found, will retry in next cycle", reconnect_ssid);
                s_reconnect_fail_count++;
                // 親機が見つからない場合は、次のサイクルで再試行（リセットしない）
            } else {
                // 親機APが見つかったので接続を試行
                syslog(DEBUG_WIFI, "Parent AP '%s' found, attempting connection...", reconnect_ssid);
                
                bool success = wifi_reconnect();
                if (!success) {
                    s_reconnect_fail_count++;
                    syslog(DEBUG_WIFI, "Reconnect failed, fail_count=%d/%d", 
                           s_reconnect_fail_count, RECONNECT_MAX_RETRY);
                } else {
                    // 接続成功
                    syslog(DEBUG_WIFI, "Reconnected successfully");
                    s_reconnect_fail_count = 0;
                }
            }
            
            // 連続20回失敗した場合の処理（通常は到達しないが、念のため）
            if (s_reconnect_fail_count >= RECONNECT_MAX_RETRY) {
                syslog(DEBUG_WIFI, "Max reconnect failures reached (%d), resetting counter and continuing", 
                       RECONNECT_MAX_RETRY);
                s_reconnect_fail_count = 0;  // リセットして再試行を続ける（常時電源ONのため）
            }
        }
    }
}

// ==== 公開関数 ====
void start_wifi_task(TaskHandle_t mainTaskHandle)
{
    mainTaskHandle_ = mainTaskHandle;
    xTaskCreate(wifi_task, "WiFiTask", 6144, NULL, 3, NULL);  // スタック拡張（再接続処理用）
}

QueueHandle_t wifi_get_data_queue(void)
{
    return s_data_queue;
}

// 既存互換性用（非推奨、temp_sens_task使用時は使用しない）
bool wifi_send_data(uint32_t data)
{
    // 互換性のため、空実装（temp_sens_data_t構造体を使用すること）
    (void)data;
    return false;
}

// 新規構造体データ送信関数
bool wifi_send_temp_sens_data(const temp_sens_data_t *data)
{
    if (s_data_queue == NULL || data == NULL) {
        return false;
    }
    
    // キューにデータを送信（非ブロッキング）
    if (xQueueSend(s_data_queue, data, 0) == pdTRUE) {
        return true;
    } else {
        // キューが満杯の場合は古いデータを上書き
        temp_sens_data_t dummy;
        xQueueReceive(s_data_queue, &dummy, 0);
        return xQueueSend(s_data_queue, data, 0) == pdTRUE;
    }
}

// Wi-Fi接続状態取得関数
bool wifi_is_connected(void)
{
    if (s_wifi_event_group == NULL) {
        return false;
    }
    
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
