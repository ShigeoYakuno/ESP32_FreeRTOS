#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define NO_FLUSH 1

typedef enum 
{
    DEBUG = 2, 
    DEBUG_WIFI,
    INFO, 
    WARNING,
    ERR  
} log_mode;


extern QueueHandle_t logQueue;

void start_log_task(void);
void log_printf_fromISR(const char *fmt, ...);
void log_printf(const char *fmt, ...);
void syslog(unsigned char mode,const char *fmt, ...);
