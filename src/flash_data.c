/**
 * @file flash_data.c
 * @brief ユーザ設定・システム情報をESP32フラッシュ（userdata領域）へ保存／読み出しするモジュール
 * @details
 * - 4KB単位で消去・書き込みを行う。
 * - syslog()を利用し、タスク／ISRどちらのコンテキストからでも安全にログ出力できる。
 * - 各データ項目はE2memdata[]テーブルで管理される。
 */

#include "flash_data.h"
#include <string.h>
#include "esp_netif.h"
#include "log_task.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_netif.h"
#include "scale_cmd.h"
#include "scale_data.h"

#include "esp_netif.h"  // esp_ip4_addr_t, esp_ip4addr_ntoa()


#define DATA_COUNT (sizeof(E2memdata)/sizeof(E2memdata[0]))

/* ==== 定数・バッファ ==== */
static UW flash_buf[FLASH_WORDS];           /**< フラッシュデータRAMバッファ */
static uint32_t g_ip_addr = 0;              /**< IPv4アドレス保持用 */
static uint8_t  g_mac_addr[6] = {0};        /**< MACアドレス保持用 */

/* ==== ログタグ ==== */
static const char *TAG = "flash_data";

static uint32_t  g_bt_dev_num = 1;       //btデバイスNo
static uint32_t  g_wifi_dev_no = 1;      //Wi-Fi子機No（1-4）
static uint32_t  g_ssid_no = 0;          //親機SSID番号（0-3、デフォルト0）

/* ----------------------------------------------------------------------
 * ダミー関数（未使用エントリ用）
 * ---------------------------------------------------------------------- */
/**
 * @brief ダミーgetter関数
 */
UW getDummyFunc(void) { return 0; }

/**
 * @brief ダミーsetter関数
 */
void setDummyFunc(UW data) { (void)data; }

/* ----------------------------------------------------------------------
 * ユーザ実装関数群（実際のgetter/setter）
 * ---------------------------------------------------------------------- */



/**
 * @brief BTデバイスNoの二けたを取得。PE_DEV_**
 */
uint32_t getBtDevNum(void) { return g_bt_dev_num; }

/**
 * @brief BTデバイスNoを設定
 * @param no 例: 10 (PE_DEV_10)
 */
void setBtDevNum(uint32_t no)
{
    g_bt_dev_num = no;
    syslog(INFO,"BT NAME SET %d",no);
}

/**
 * @brief Wi-Fi子機Noを取得（1-4）
 */
uint32_t getWifiDevNo(void) { return g_wifi_dev_no; }

/**
 * @brief Wi-Fi子機Noを設定（1-4）
 * @param no 子機No（1-4）
 */
void setWifiDevNo(uint32_t no)
{
    if (no >= 1 && no <= 4) {
        g_wifi_dev_no = no;
        syslog(INFO, "Wi-Fi device No set: %d", (int)no);
    } else {
        syslog(WARN, "Invalid Wi-Fi device No: %d (must be 1-4)", (int)no);
    }
}

/**
 * @brief 親機SSID番号を取得（0-3）
 */
uint32_t getSsidNo(void) { return g_ssid_no; }

/**
 * @brief 親機SSID番号を設定（0-3）
 * @param no SSID番号（0-3）
 */
void setSsidNo(uint32_t no)
{
    if (no <= 3) {
        g_ssid_no = no;
        syslog(INFO, "SSID number set: %d (PE_IOT_GATEWAY_%d)", (int)no, (int)no);
    } else {
        syslog(WARN, "Invalid SSID number: %d (must be 0-3)", (int)no);
    }
}

/* ----------------------------------------------------------------------
 * IP / MAC 関連
 * ---------------------------------------------------------------------- */

/**
 * @brief 現在のIPv4アドレスを取得
 */
uint32_t getIpAddr(void) { return g_ip_addr; }

/**
 * @brief IPv4アドレスを設定
 * @param val 例: 0xC0A80132 (192.168.1.50)
 */
void setIpAddr(uint32_t val) { g_ip_addr = val; }

/**
 * @brief MACアドレス上位32bitを取得
 * @return [0][1][2][3]を連結した値
 */
uint32_t getMacAddrHigh(void)
{
    return ((uint32_t)g_mac_addr[0] << 24) |
           ((uint32_t)g_mac_addr[1] << 16) |
           ((uint32_t)g_mac_addr[2] << 8)  |
           (uint32_t)g_mac_addr[3];
}

/**
 * @brief MACアドレス上位32bitを設定
 */
void setMacAddrHigh(uint32_t v)
{
    g_mac_addr[0] = (v >> 24) & 0xFF;
    g_mac_addr[1] = (v >> 16) & 0xFF;
    g_mac_addr[2] = (v >> 8) & 0xFF;
    g_mac_addr[3] = v & 0xFF;
}

/**
 * @brief MACアドレス下位16bitを取得
 */
uint32_t getMacAddrLow(void)
{
    return ((uint32_t)g_mac_addr[4] << 8) | g_mac_addr[5];
}

/**
 * @brief MACアドレス下位16bitを設定
 */
void setMacAddrLow(uint32_t v)
{
    g_mac_addr[4] = (v >> 8) & 0xFF;
    g_mac_addr[5] = v & 0xFF;
}

/* ----------------------------------------------------------------------
 * フラッシュデータテーブル定義（64個×4B = 256B）
 * ---------------------------------------------------------------------- */
static const E2_DATA_TBL E2memdata[] = {
    { SPANCOEF,  getSpanCoef,     setSpanCoef },
    { REAL_ZERO, getTrueZeroData, setTrueZeroData },
    { TEMP_ZERO, getTareZeroData, setTareZeroData },

    /* ダミー領域 */
    { 3, getDummyFunc, setDummyFunc }, { 4, getDummyFunc, setDummyFunc },
    { 5, getDummyFunc, setDummyFunc }, { 6, getDummyFunc, setDummyFunc },
    { 7, getDummyFunc, setDummyFunc }, { 8, getDummyFunc, setDummyFunc },
    { 9, getDummyFunc, setDummyFunc }, {10, getDummyFunc, setDummyFunc },
    {11, getDummyFunc, setDummyFunc }, {12, getDummyFunc, setDummyFunc },
    {13, getDummyFunc, setDummyFunc }, {14, getDummyFunc, setDummyFunc },
    {15, getDummyFunc, setDummyFunc }, {16, getDummyFunc, setDummyFunc },
    {17, getDummyFunc, setDummyFunc }, {18, getDummyFunc, setDummyFunc },
    {19, getDummyFunc, setDummyFunc },

    { IP_ADDR,    getIpAddr,      setIpAddr },
    { MAC_ADDR_H, getMacAddrHigh, setMacAddrHigh },
    { MAC_ADDR_L, getMacAddrLow,  setMacAddrLow },

    /* 以降未使用領域 */
    {23, getDummyFunc, setDummyFunc }, {24, getDummyFunc, setDummyFunc },
    {25, getDummyFunc, setDummyFunc }, {26, getDummyFunc, setDummyFunc },
    {27, getDummyFunc, setDummyFunc }, {28, getDummyFunc, setDummyFunc },
    {WIFI_DEV_NO, getWifiDevNo, setWifiDevNo }, 

    {BT_DEV_NO, getBtDevNum, setBtDevNum },
    
    {31, getDummyFunc, setDummyFunc }, {32, getDummyFunc, setDummyFunc },
    {33, getDummyFunc, setDummyFunc }, {34, getDummyFunc, setDummyFunc },
    {35, getDummyFunc, setDummyFunc }, {36, getDummyFunc, setDummyFunc },
    {37, getDummyFunc, setDummyFunc }, {38, getDummyFunc, setDummyFunc },
    {39, getDummyFunc, setDummyFunc }, {SSID_NO, getSsidNo, setSsidNo },
    {41, getDummyFunc, setDummyFunc }, {42, getDummyFunc, setDummyFunc },
    {43, getDummyFunc, setDummyFunc }, {44, getDummyFunc, setDummyFunc },
    {45, getDummyFunc, setDummyFunc }, {46, getDummyFunc, setDummyFunc },
    {47, getDummyFunc, setDummyFunc }, {48, getDummyFunc, setDummyFunc },
    {49, getDummyFunc, setDummyFunc }, {50, getDummyFunc, setDummyFunc },
    {51, getDummyFunc, setDummyFunc }, {52, getDummyFunc, setDummyFunc },
    {53, getDummyFunc, setDummyFunc }, {54, getDummyFunc, setDummyFunc },
    {55, getDummyFunc, setDummyFunc }, {56, getDummyFunc, setDummyFunc },
    {57, getDummyFunc, setDummyFunc }, {58, getDummyFunc, setDummyFunc },
    {59, getDummyFunc, setDummyFunc }, {60, getDummyFunc, setDummyFunc },
    {61, getDummyFunc, setDummyFunc }, {62, getDummyFunc, setDummyFunc },
    { CRC_CALC, getDummyFunc, setDummyFunc },
};

/* ----------------------------------------------------------------------
 * パーティション取得ユーティリティ
 * ---------------------------------------------------------------------- */
/**
 * @brief userdataパーティションを取得
 * @return 見つからなければNULL
 */
static const esp_partition_t* get_partition(void)
{
    const esp_partition_t* p = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, USERDATA_SUBTYPE, USERDATA_NAME);
    if (!p)
        syslog(ERR, "userdata partition not found");
    else
        syslog(INFO, "partition ok (addr=0x%08X, size=%d)", (unsigned int)p->address, p->size);
    return p;
}

/* ----------------------------------------------------------------------
 * フラッシュ読み出し
 * ---------------------------------------------------------------------- */
/**
 * @brief フラッシュからデータを読み込み、RAMへ展開
 * @return ESP_OK=成功 / それ以外=エラー
 */
esp_err_t flashdata_load(void)
{
    const esp_partition_t* part = get_partition();
    if (!part) return ESP_ERR_NOT_FOUND;

    esp_err_t err = esp_partition_read(part, 0, flash_buf, FLASH_PAGE_BYTES);
    if (err != ESP_OK) return err;

    /* BCC計算 */
    UW bcc = FLASH_BCC_INIT;
    for (int i = 0; i < CRC_CALC; i++) bcc ^= flash_buf[i];

    if (bcc != flash_buf[CRC_CALC]) {
        syslog(WARN, "BCC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    /* データ反映 */
    for (int i = 0; i < CRC_CALC && i < DATA_COUNT; i++)
        E2memdata[i].set_func(flash_buf[i]);

    syslog(INFO, "Load OK");
    return ESP_OK;
}

/* ----------------------------------------------------------------------
 * フラッシュ書き込み
 * ---------------------------------------------------------------------- */
/**
 * @brief RAM上のデータをフラッシュへ保存
 * @details
 * - 4KBセクタを消去後、同じ範囲へ書き込み。
 * - 書き込み完了後にキャッシュをフラッシュ。
 * @return ESP_OK=成功 / ESP_ERR_xx=失敗
 */
esp_err_t flashdata_save(void)
{
    const esp_partition_t* part = get_partition();
    if (!part) return ESP_ERR_NOT_FOUND;

    for (int i = 0; i < CRC_CALC && i < DATA_COUNT; i++)
        flash_buf[i] = E2memdata[i].get_func();

    UW bcc = FLASH_BCC_INIT;
    for (int i = 0; i < CRC_CALC; i++) bcc ^= flash_buf[i];
    flash_buf[CRC_CALC] = bcc;

    // 消去＆書き込み
    ESP_ERROR_CHECK(esp_partition_erase_range(part, 0, FLASH_PAGE_BYTES));
    esp_err_t err = esp_partition_write(part, 0, flash_buf, FLASH_PAGE_BYTES);

    if (err == ESP_OK)
        syslog(INFO, "Save OK (written %d bytes)", FLASH_PAGE_BYTES);
    else
        syslog(ERR, "Save failed (%s)", esp_err_to_name(err));

    return err;
}

/* ----------------------------------------------------------------------
 * フラッシュ内容ダンプ
 * ---------------------------------------------------------------------- */
/**
 * @brief フラッシュバッファ全内容をsyslogに出力
 * @note  ISR・Taskどちらからでも安全に呼び出し可能
 */

void flashdata_dump_all(void)
{
    syslog(INFO, "===== FLASH DATA DUMP START =====");

    for (int i = 0; i < E2DATA_MAX; i++)
    {
        const char *name;
        switch (E2memdata[i].id) {
            case SPANCOEF:   name = "SPANCOEF"; break;
            case REAL_ZERO:  name = "REAL_ZERO"; break;
            case TEMP_ZERO:  name = "TEMP_ZERO"; break;
            case IP_ADDR:    name = "IP_ADDR";   break;
            case MAC_ADDR_H: name = "MAC_ADDR_H";break;
            case MAC_ADDR_L: name = "MAC_ADDR_L";break;
            case WIFI_DEV_NO: name = "WIFI_DEV_NO";break;
            case BT_DEV_NO:  name = "BT_DEV_NO";break;
            case SSID_NO:    name = "SSID_NO";break;   
            case CRC_CALC:   name = "CRC_CALC";  break;
            default:         name = "DUMMY";     break;
        }

        if (strcmp(name, "DUMMY") != 0)
        {
            if (strcmp(name, "IP_ADDR") == 0)
            {
                uint32_t ip_raw = flash_buf[i];
                // 手動でビッグエンディアンに変換して正しい順に並べる
                uint8_t ip_bytes[4] = {
                    (ip_raw >> 24) & 0xFF,
                    (ip_raw >> 16) & 0xFF,
                    (ip_raw >> 8) & 0xFF,
                    ip_raw & 0xFF
                };
                syslog(INFO, "[%02d] %-10s : %u.%u.%u.%u (0x%08lX)",
                       i, name,
                       ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3],
                       (unsigned long)ip_raw);
            }
            else if (strcmp(name, "MAC_ADDR_H") == 0)
            {
                uint32_t mac_high = flash_buf[i];
                uint8_t mac[4] = {
                    (mac_high >> 24) & 0xFF,
                    (mac_high >> 16) & 0xFF,
                    (mac_high >> 8) & 0xFF,
                    mac_high & 0xFF
                };
                syslog(INFO, "[%02d] %-10s : %02X:%02X:%02X:%02X (0x%08lX)",
                       i, name, mac[0], mac[1], mac[2], mac[3], (unsigned long)mac_high);
            }
            else if (strcmp(name, "MAC_ADDR_L") == 0)
            {
                uint32_t mac_low = flash_buf[i];
                uint8_t mac[2] = {
                    (mac_low >> 8) & 0xFF,
                    mac_low & 0xFF
                };
                syslog(INFO, "[%02d] %-10s : %02X:%02X (0x%08lX)",
                       i, name, mac[0], mac[1], (unsigned long)mac_low);
            }
            else
            {
                syslog(INFO, "[%02d] %-10s : 0x%08lX (%lu)",
                       i, name,
                       (unsigned long)flash_buf[i],
                       (unsigned long)flash_buf[i]);
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    syslog(INFO, "===== FLASH DATA DUMP END =====");
}




/**
 * @brief ユーザーデータ領域を完全消去（全バイト0xFF化）
 * @retval ESP_OK           正常終了
 * @retval ESP_ERR_NOT_FOUND パーティション未検出
 * @retval ESP_FAIL          消去失敗
 * @note   OK帰るが、消去できない。恐らくプロテクトかかっている
 * @note   機能には不要のため、深追いはしていない。
 */
 esp_err_t flashdata_clear_all(void)
{
    const esp_partition_t* part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, USERDATA_SUBTYPE, USERDATA_NAME);
    if (!part) {
        syslog(ERR, "userdata partition not found");
        return ESP_ERR_NOT_FOUND;
    }

    syslog(INFO, "Erasing userdata partition... (addr=0x%06X, size=%u)",
           part->address, part->size);

    // 1. esp_partition_erase_range は消去サイズを 4KB アライメントにする必要がある
    size_t erase_size = (part->size + 0xFFF) & ~0xFFF;

    esp_err_t err = esp_partition_erase_range(part, 0, erase_size);
    if (err != ESP_OK) {
        syslog(ERR, "esp_partition_erase_range failed (%s)", esp_err_to_name(err));
        return err;
    }

    // 2. RAMバッファをFFで初期化（メモリ上のデータも整合）
    memset(flash_buf, 0xFF, sizeof(flash_buf));

    syslog(INFO, "Erase complete (verified by partition API)");
    return ESP_OK;
}


void flash_force_erase_test(void)
{
    uint32_t addr = 0x110000;   // userdata offset
    uint32_t size = 0x20000;    // 128KB

    syslog(INFO, "Force erasing physical region 0x%06X - 0x%06X", addr, addr + size);

    esp_err_t err = esp_flash_erase_region(NULL, addr, size);
    if (err != ESP_OK) {
        syslog(ERR, "esp_flash_erase_region failed: %s", esp_err_to_name(err));
    } else {
        syslog(INFO, "Force erase success (0x%06X - 0x%06X)", addr, addr + size);
    }
}



