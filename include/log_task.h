#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define NO_FLUSH 1
#define LOG_MSG_LEN   256

typedef enum 
{
    DEBUG = 2, 
    DEBUG_WIFI,
    INFO,
    INFO_ENET, 
    WARN,
    ERR  
} log_mode;

// ==== 出力種別 ====
typedef enum {
    LOG_OUTPUT_UART = 0,
    LOG_OUTPUT_BLUETOOTH,
    LOG_OUTPUT_BOTH
} log_output_t;

extern QueueHandle_t logQueue;

// ==== 外部公開関数 ====

void syslog_set_output(int mode);
void start_log_task(void);
void log_printf_fromISR(const char *fmt, ...);
void log_printf(const char *fmt, ...);
void syslog(unsigned char mode,const char *fmt, ...);

#ifdef __cplusplus
}
#endif
