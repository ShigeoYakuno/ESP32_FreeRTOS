#pragma once

#ifdef __cplusplus
extern "C" {
#endif


void set_imask(uint32_t level);
uint32_t get_imask(void);

void testISR_1(void);
void testISR_2(void);
void testISR_3(void);

#ifdef __cplusplus
}
#endif