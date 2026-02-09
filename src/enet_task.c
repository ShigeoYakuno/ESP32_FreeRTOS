// enet_task.c — ESP32 + W5500 UDP (Tx/Rx tasks split, mutex-protected, non-blocking Rx)

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_mac.h"
#include <string.h>
#include <arpa/inet.h>
#include "enet_test_task.h"
#include "temp_sens_task.h"  // センサデータ取得用

#include "flash_data.h"

#ifdef MR
#undef MR
#endif

// ==== WIZnet stack ====
#define _WIZCHIP_  W5500
#include "wizchip_conf.h"
#include "W5500/w5500.h"
#include "wizchip_port.h"

// 注: W5500ではping機能がサポートされていないため、リンク状態監視のみ実施

#define close wiz_close
#include "socket.h"
#undef close

static const char *TAG = "enet";

// === UDP共有メッセージ（enet_test_taskと共有） ===
char g_last_udp_msg[128] = {0};
SemaphoreHandle_t g_msg_mutex = NULL;

// ===================================================
// グローバル（タスク間共有）
// ===================================================
static int g_sock = -1;                  // UDP socket# (0..7) / -1 = invalid
static SemaphoreHandle_t g_wiz_mutex;    // W5500アクセス排他
static volatile bool g_link_up = false;  // PHYリンク状態（簡易フラグ）

// ===================================================
// 便利ダンプ
// ===================================================
static void dump_netregs(void) {
    uint8_t ip[4], sn[4], gw[4], mac[6];
    getSIPR(ip);  getSUBR(sn);  getGAR(gw);  getSHAR(mac);
    ESP_LOGI(TAG, "SIPR=%d.%d.%d.%d  SUBR=%d.%d.%d.%d  GAR=%d.%d.%d.%d",
        ip[0],ip[1],ip[2],ip[3], sn[0],sn[1],sn[2],sn[3], gw[0],gw[1],gw[2],gw[3]);
    ESP_LOGI(TAG, "SHAR=%02X:%02X:%02X:%02X:%02X:%02X",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
}


/**
 * @brief 固定形式（192.168.1.X）でIPアドレスを設定する
 * @param host_id 1〜200の範囲（例：50 → 192.168.1.50）
 * @return true=成功, false=範囲外
 */
bool enet_set_ip(uint8_t host_id)
{
    if (host_id < 1 || host_id > 200) {
        ESP_LOGW("enet_ip", "Invalid host_id: %d (valid 1–200)", host_id);
        return false;
    }

    // 192.168.1.X を 32bit値へ変換
    uint32_t new_ip = (192 << 24) | (168 << 16) | (1 << 8) | host_id;
    setIpAddr(new_ip);

    uint32_t now_ip = getIpAddr();
    ESP_LOGI("enet_ip",
            "IP updated: %u.%u.%u.%u (0x%08X)",
            (unsigned int)((now_ip >> 24) & 0xFF),
            (unsigned int)((now_ip >> 16) & 0xFF),
            (unsigned int)((now_ip >> 8)  & 0xFF),
            (unsigned int)(now_ip & 0xFF),
            (unsigned int)now_ip);

    // 必要に応じて保存
    // flashdata_save();

    return true;
}


// ===================================================
// W5500 初期化
// ===================================================
static esp_err_t enet_spi_init(void)
{
    ESP_LOGI(TAG, "=== W5500 Initialization Start ===");
    
    // wizchip_port.c 側でSPIピン/デバイスを初期化
    ESP_LOGI(TAG, "Step 1: SPI initialization...");
    wizchip_spi_init();
    
    ESP_LOGI(TAG, "Step 2: Hardware reset...");
    wizchip_hw_reset();
    vTaskDelay(pdMS_TO_TICKS(100));  // リセット後の安定待ち
    
    ESP_LOGI(TAG, "Step 3: Port register...");
    wizchip_port_register();

    // 配線にプルアップ無い場合はMISOをPullUp
    gpio_pullup_en(GPIO_NUM_19);
    ESP_LOGI(TAG, "Step 4: MISO pullup enabled");

    // TX/RXバッファ（合計32KB配分。ここでは控えめに）
    uint8_t txsize[8] = {2,2,2,2,2,2,2,2};
    uint8_t rxsize[8] = {2,2,2,2,2,2,2,2};
    ESP_LOGI(TAG, "Step 5: Wizchip init (TX/RX buffers)...");
    if (wizchip_init(txsize, rxsize) != 0) {
        ESP_LOGE(TAG, "wizchip_init failed!");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "wizchip_init OK");
    
    ESP_LOGI(TAG, "Step 6: Software reset...");
    wizchip_sw_reset();
    vTaskDelay(pdMS_TO_TICKS(100));  // リセット後の安定待ち
    
    // W5500バージョン確認（SPI通信の動作確認）
    uint8_t version = getVERSIONR();
    ESP_LOGI(TAG, "W5500 Version Register: 0x%02X (expected: 0x04)", version);
    if (version != 0x04) {
        ESP_LOGW(TAG, "W5500 version mismatch! Check SPI connection.");
        ESP_LOGW(TAG, "SPI pins: MOSI=23, MISO=19, SCK=18, CS=5 (VSPI/SPI3_HOST)");
        ESP_LOGW(TAG, "RESET pin: GPIO26");
        // バージョン不一致でも続行（ハードウェアの問題の可能性）
    } else {
        ESP_LOGI(TAG, "W5500 version OK - SPI communication working");
    }
    
    // 追加のSPI通信確認: MRレジスタを読み取り
    uint8_t mr = getMR();
    ESP_LOGI(TAG, "W5500 MR (Mode Register): 0x%02X", mr);

    
    
    /*
    ビット47–24	OUI (Organizationally Unique Identifier)	ベンダーID（IEEEが企業に割り当て）
    ビット23–0	NIC部分	ベンダーが機器ごとに自由設定
    ビット1	“Locally Administered”フラグ	1にすると自作/ローカル用途を示す
    ビット0	マルチキャストフラグ	0 = ユニキャスト（通常）、1 = マルチキャスト
    IEEEでは、ベンダーIDを持たない個人・研究者でも使えるように、
    ローカル管理アドレス（Locally Administered Address, LAA）という仕組みを設けている。

    ローカル管理アドレス（LAA）の条件
    最上位バイトの2ビット目（bit1）を1にする
    最下位ビット（bit0）は0にする
    それ以外の46bitは自由

    以下の方針でLAAを使用する
    ESP32チップ内部のeFuseに格納された出荷時MAC（Wi-Fi STA用）を取得
    (Espressif が出荷時に焼き込んだ ユニークなグローバルMAC)
    そのMACを元にして、先頭バイトのビット1（LAAフラグ）をON
    最後のバイトをXOR ^ 0x55 でランダム化して、W5500用の独自MAC（LAA形式）を生成。
    */

    // 一意MAC生成（ESP32のWi-Fi STA MACからLAA化
    uint8_t base_mac[6];
    esp_read_mac(base_mac, ESP_MAC_WIFI_STA);
    uint8_t my_mac[6] = {
        (uint8_t)(base_mac[0] | 0x02),  // LAA
        base_mac[1], base_mac[2], base_mac[3], base_mac[4],
        (uint8_t)(base_mac[5] ^ 0x55)
    };

    // 固定IP設定
    wiz_NetInfo netinfo = {
        .mac  = { my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5] },
        .ip   = {192,168,1,50},
        .sn   = {255,255,255,0},
        .gw   = {0,0,0,0},
        .dns  = {0,0,0,0},
        .dhcp = NETINFO_STATIC,
    };
    wizchip_setnetinfo(&netinfo);
    setGAR(netinfo.gw);
    setSUBR(netinfo.sn);
    setSIPR(netinfo.ip);

    // 再送設定（余裕）
    setRTR(2000);
    setRCR(8);

    dump_netregs();

    // PHY設定＆リンク待ち
    ESP_LOGI(TAG, "Step 7: PHY configuration...");
    wiz_PhyConf phyconf = {
        .by = PHY_CONFBY_SW,
        .mode = PHY_MODE_AUTONEGO,
        .speed = PHY_SPEED_100,
        .duplex = PHY_DUPLEX_FULL,
    };
    wizphy_setphyconf(&phyconf);
    wizphy_reset();
    vTaskDelay(pdMS_TO_TICKS(500));  // PHYリセット後の安定待ち

    ESP_LOGI(TAG, "Step 8: Waiting for PHY link...");
    g_link_up = false;
    int stable = 0;
    for (int i = 0; i < 100; i++) { // 最大約10秒
        uint8_t phy = getPHYCFGR();
        if (phy & 0x01) {
            if (++stable >= 3) { 
                g_link_up = true; 
                ESP_LOGI(TAG, "PHY link detected (stable count: %d)", stable);
                break; 
            }
        } else {
            stable = 0;
        }
        if ((i % 10) == 0) {  // 1秒ごとにログ出力
            ESP_LOGI(TAG, "PHY status check: %d/100 (PHYCFGR=0x%02X, link=%s)", 
                     i, phy, (phy & 0x01) ? "detected" : "not detected");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (g_link_up) {
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "PHY Link: Up");
        
        // リンクアップ時の詳細情報
        uint8_t phy_cfgr = getPHYCFGR();
        ESP_LOGI(TAG, "PHYCFGR: 0x%02X (Link=%d, Speed=%s, Duplex=%s)",
                 phy_cfgr,
                 (phy_cfgr & 0x01) ? 1 : 0,
                 (phy_cfgr & 0x04) ? "100M" : "10M",
                 (phy_cfgr & 0x08) ? "Full" : "Half");
    } else {
        ESP_LOGE(TAG, "PHY Link: Down (timeout after 10 seconds)");
        ESP_LOGE(TAG, "Check: Ethernet cable connection, switch/router power");
        ESP_LOGE(TAG, "Last PHYCFGR value: 0x%02X", getPHYCFGR());
    }

    ESP_LOGI(TAG, "=== W5500 Initialization %s ===", g_link_up ? "SUCCESS" : "FAILED");
    
    // リンクがダウンでも初期化は続行（pingテストで確認可能）
    if (!g_link_up) {
        ESP_LOGW(TAG, "Continuing with link down - ping test will be available");
    }
    
    return ESP_OK;  // リンクダウンでもESP_OKを返す（pingテスト可能にする）
}

// ===================================================
// UDPソケット初期化（socket#0使用）
// ===================================================
static int udp_socket_init(void)
{
    // 成功時は 0（= socket#0）が返る点に注意
    int s = socket(0, Sn_MR_UDP, 3000, 0);
    if (s < 0) {
        ESP_LOGE(TAG, "socket open failed: %d", s);
        return -1;
    }
    ESP_LOGI(TAG, "UDP socket opened (port 3000, sock=%d)", s);
    return s; // 期待値は 0
}

// ===================================================
// ソケット健全性確認＆必要なら再オープン
// ===================================================
static void ensure_socket_alive(void)
{
    xSemaphoreTake(g_wiz_mutex, portMAX_DELAY);
    uint8_t sr = getSn_SR(g_sock);
    xSemaphoreGive(g_wiz_mutex);

    if (sr != SOCK_UDP) {
        ESP_LOGW(TAG, "Socket state invalid (SR=0x%02X). Reopen...", sr);
        xSemaphoreTake(g_wiz_mutex, portMAX_DELAY);
        if (g_sock >= 0) close(g_sock);
        int s = socket(0, Sn_MR_UDP, 3000, 0);
        if (s < 0) {
            ESP_LOGE(TAG, "Reopen failed: %d", s);
            g_sock = -1;
        } else {
            g_sock = s;
            ESP_LOGI(TAG, "Reopened socket: %d", g_sock);
        }
        xSemaphoreGive(g_wiz_mutex);
    }
}

// ===================================================
// 送信タスク（2秒ごとにセンサデータをUDP送信）
// ===================================================
static void enet_send_task(void *arg)
{
    const uint8_t DEST_IP[4] = {192,168,1,20};
    const uint16_t DEST_PORT = 3000;

    for (;;)
    {
        if (!g_link_up || g_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        ensure_socket_alive();

        // 最新のセンサデータを取得
        temp_sens_data_t sens_data;
        temp_sens_get_latest_data(&sens_data);

        // JSON形式でセンサデータをフォーマット
        // フォーマット: {"seq":123,"aht_t":25.3,"aht_rh":48.2,"bmp_t":25.1,"bmp_p":1008.45,"aht_ok":true,"bmp_ok":true}
        char msg[256];
        int len = snprintf(msg, sizeof(msg),
            "{\"seq\":%lu,\"aht_t\":%.1f,\"aht_rh\":%.1f,\"bmp_t\":%.1f,\"bmp_p\":%.1f,\"aht_ok\":%s,\"bmp_ok\":%s}",
            (unsigned long)sens_data.seq,
            (float)sens_data.aht_t01 / 10.0f,      // 0.1℃単位 → ℃
            (float)sens_data.aht_rh01 / 10.0f,     // 0.1%単位 → %
            (float)sens_data.bmp_t01 / 10.0f,      // 0.1℃単位 → ℃
            (float)sens_data.bmp_p01 / 10.0f,      // 0.1hPa単位 → hPa
            sens_data.aht_ok ? "true" : "false",
            sens_data.bmp_ok ? "true" : "false");

        if (len > 0 && len < sizeof(msg)) {
            xSemaphoreTake(g_wiz_mutex, portMAX_DELAY);
            int ret = sendto(g_sock, (uint8_t*)msg, len, (uint8_t*)DEST_IP, DEST_PORT);
            xSemaphoreGive(g_wiz_mutex);

            if (ret > 0) {
                ESP_LOGI(TAG, "[SEND] Sensor data (%dB): %s", ret, msg);
            } else {
                ESP_LOGE(TAG, "[SEND] failed (%d)", ret);
            }
        } else {
            ESP_LOGE(TAG, "[SEND] Message buffer overflow");
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // 2秒周期送信
    }
}


// ===================================================
// UDP受信タスク（UARTコンソール表示付き） ノンブロッキング
// ===================================================
static void enet_recv_task(void *arg)
{
    uint8_t rxbuf[512];
    uint8_t rip[4];
    uint16_t rport;

    for (;;)
    {
        if (!g_link_up || g_sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        ensure_socket_alive();

        xSemaphoreTake(g_wiz_mutex, portMAX_DELAY);
        uint16_t rxsz = getSn_RX_RSR(g_sock);
        if (rxsz > 0) {
            int32_t len = recvfrom(g_sock, rxbuf, sizeof(rxbuf)-1, rip, &rport);
            xSemaphoreGive(g_wiz_mutex);

            if (len > 0) {
                rxbuf[len] = '\0';  // 文字列終端
                // ログ出力（タグ付き）
                ESP_LOGI(TAG, "[RECV] from %d.%d.%d.%d:%d => %s",
                         rip[0], rip[1], rip[2], rip[3], rport, rxbuf);
                printf("<<< UDP RX <<<  %s\r\n", (char*)rxbuf);
                fflush(stdout);

                //共有メッセージへ保存（排他保護）
                xSemaphoreTake(g_msg_mutex, portMAX_DELAY);
                strncpy(g_last_udp_msg, (char*)rxbuf, sizeof(g_last_udp_msg) - 1);
                g_last_udp_msg[sizeof(g_last_udp_msg) - 1] = '\0'; // 念のため
                xSemaphoreGive(g_msg_mutex);
            }
        } else {
            xSemaphoreGive(g_wiz_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 定期yieldでWDT回避
    }
}

// ===================================================
// 初期化タスク（SPI/W5500/ソケット/子タスク起動）
// ===================================================
static void enet_main_task(void *arg)
{
    esp_err_t init_result = enet_spi_init();
    if (init_result != ESP_OK) {
        ESP_LOGE(TAG, "Ethernet init failed: %s", esp_err_to_name(init_result));
        ESP_LOGE(TAG, "Hardware check:");
        ESP_LOGE(TAG, "  - SPI pins: MOSI=23, MISO=19, SCK=18, CS=5 (VSPI/SPI3_HOST)");
        ESP_LOGE(TAG, "  - RESET pin: GPIO26");
        ESP_LOGE(TAG, "  - W5500 power supply");
        ESP_LOGE(TAG, "  - SPI bus connections");
        // 初期化失敗時は即座に終了（ウォッチドッグをリセットする必要はない）
        ESP_LOGE(TAG, "ENET task terminating due to initialization failure");
        vTaskDelete(NULL);
        return;
    }

    // === Mutex初期化 ===
    g_wiz_mutex = xSemaphoreCreateMutex();
    configASSERT(g_wiz_mutex);

    g_msg_mutex = xSemaphoreCreateMutex();    // UDPメッセージ共有用
    configASSERT(g_msg_mutex);

    g_sock = udp_socket_init();  // 期待値は 0
    if (g_sock < 0) {
        ESP_LOGE(TAG, "UDP socket init failed");
        vTaskDelete(NULL);
        return;
    }

    // 送受信タスク起動（引数は不要。グローバル参照）
    xTaskCreate(enet_send_task, "enet_send_task", 4096, NULL, 5, NULL);
    xTaskCreate(enet_recv_task, "enet_recv_task", 4096, NULL, 5, NULL);

    // ヘルスチェック（リンク状態監視）
    ESP_LOGI(TAG, "=== ENET Main Task Started ===");
    ESP_LOGI(TAG, "Link status will be checked every 5 seconds");
    
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5秒ごとにリンク状態確認
        
        // リンク状態を再確認
        if (g_sock >= 0) {
            xSemaphoreTake(g_wiz_mutex, portMAX_DELAY);
            uint8_t phy = getPHYCFGR();
            xSemaphoreGive(g_wiz_mutex);
            
            bool current_link = (phy & 0x01) ? true : false;
            if (current_link != g_link_up) {
                g_link_up = current_link;
                ESP_LOGI(TAG, "PHY Link status changed: %s", g_link_up ? "UP" : "DOWN");
            }
            
            // リンク状態とソケット状態をログ出力
            if (g_link_up) {
                ESP_LOGI(TAG, "[STATUS] Link=UP, Socket=%d, IP=192.168.1.50", g_sock);
            } else {
                ESP_LOGW(TAG, "[STATUS] Link=DOWN, Socket=%d - check cable connection", g_sock);
            }
        } else {
            ESP_LOGW(TAG, "[STATUS] Socket not initialized (sock=%d)", g_sock);
        }
        
        // ウォッチドッグをリセット（タスクが生きていることを示す）
        // vTaskDelayで既にリセットされているが、明示的に確認
    }
}


// ===================================================
// 外部公開：起動エントリ
// ===================================================
void start_enet_task(void)
{
    xTaskCreate(enet_main_task, "enet_main_task", 4096, NULL, 5, NULL);
    start_enet_test_task();
}
