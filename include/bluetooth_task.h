#pragma once

#ifdef __cplusplus
extern "C" {
#endif


void start_bluetooth_task(void);

extern volatile uint32_t bt_handle;
extern volatile bool bt_connected;
extern QueueHandle_t logBtQueue; 
extern QueueHandle_t qBtRx;        // SPP受信キュー

#ifdef __cplusplus
}
#endif