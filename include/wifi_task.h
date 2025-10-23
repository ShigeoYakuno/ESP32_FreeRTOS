#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern TaskHandle_t wifiTaskHandle;

void start_wifi_task(TaskHandle_t mainTaskHandle);
