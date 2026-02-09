#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_partition.h"
#include "esp_log.h"
#include <stdint.h>
#include "user_common.h"

#define FLASH_PAGE_BYTES  0x1000    //書き込みサイズは4K単位
#define FLASH_WORDS       (FLASH_PAGE_BYTES / sizeof(UW))  // = 1024ワード
#define FLASH_BCC_INIT    0x5A5A5A5A
#define USERDATA_SUBTYPE  0x40   // partitions.csvで定義
#define USERDATA_NAME     "userdata"


/* ID定義 */
typedef enum {
    SPANCOEF = 0,   /* スパン係数 */
    REAL_ZERO,      /* 真ゼロ */
    TEMP_ZERO,      /* 仮ゼロ */
    IP_ADDR = 20,   /* IPv4 4バイト */
    MAC_ADDR_H,     /* MAC上位（32bit） */
    MAC_ADDR_L,     /* MAC下位（16bit + padding） */
    WIFI_DEV_NO = 29,  /* Wi-Fi子機No（1-4） */
    BT_DEV_NO = 30,
    SSID_NO = 40,      /* 親機SSID番号（0-3） */
    CRC_CALC = 63,  /* チェックサム */
    E2DATA_MAX,
} E2_DATA;

/* テーブル定義構造体 */
typedef struct {
    E2_DATA id;
    UW (*get_func)(void);
    void (*set_func)(UW val);
} E2_DATA_TBL;

/* 関数プロトタイプ */
esp_err_t flashdata_load(void);
esp_err_t flashdata_save(void);

void setIpAddr(uint32_t val);
uint32_t getIpAddr(void);
void flashdata_dump_all(void);
esp_err_t flashdata_clear_all(void);
void flash_force_erase_test(void);

void setBtDevNum(uint32_t no);
uint32_t getBtDevNum(void);

void setWifiDevNo(uint32_t no);
uint32_t getWifiDevNo(void);

void setSsidNo(uint32_t no);
uint32_t getSsidNo(void);




#ifdef __cplusplus
}
#endif
