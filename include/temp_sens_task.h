#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ==== データ構造定義 ====
// WiFiタスクに送信するセンサーデータ構造体
// wifi_task.hの前方宣言と一致させるため、構造体に名前を付ける
typedef struct temp_sens_data {
    int16_t aht_t01;    // AHT温度（0.1℃単位、例：253 = 25.3℃）
    uint16_t aht_rh01;  // AHT湿度（0.1%単位、例：482 = 48.2%）
    int16_t bmp_t01;    // BMP温度（0.1℃単位、例：251 = 25.1℃）
    uint32_t bmp_p01;   // BMP気圧（0.1hPa単位、例：100845 = 10084.5hPa）
    bool aht_ok;        // 今回のAHT測定成功フラグ
    bool bmp_ok;        // 今回のBMP測定成功フラグ
    uint32_t seq;       // 送信ごとにインクリメントする連番
} temp_sens_data_t;

// ==== 公開関数 ====
void start_temp_sens_task(void);
void temp_sens_get_latest_data(temp_sens_data_t *data);  // 最新センサデータ取得

#ifdef __cplusplus
}
#endif
