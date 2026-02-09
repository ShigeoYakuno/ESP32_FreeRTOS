#pragma once

#ifdef __cplusplus
extern "C" {
#endif


// ==== 定義 ====
#define USER_SW         GPIO_NUM_4
#define HEART_BEAT_LED  GPIO_NUM_2

// ==== 子機No取得関数 ====
// 現状: DIPSWはないため、子機No「1」固定で返す
// 将来の拡張のため、関数として実装（DIPSW実装時はGPIOから読み取る）
uint8_t get_child_device_no(void);

// ==== 初期化失敗フラグ設定関数 ====
// センサー初期化失敗時に呼び出してLEDを高速点滅させる
void set_sensor_init_failed(bool failed);




void start_userIO_task(void);
void init_userIO(void);

#ifdef __cplusplus
}
#endif
