#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前方宣言
typedef struct temp_sens_data temp_sens_data_t;

void start_sd_task(void);
bool sd_enqueue_line(const char *line);
bool sd_is_mounted(void);
void sd_force_unmount(void);

bool sd_request_flashdata_export(void);
bool sd_write_sensor_data(const temp_sens_data_t *data);

#ifdef __cplusplus
}
#endif
