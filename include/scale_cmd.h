
#pragma once
#include "user_common.h"

#ifdef __cplusplus
extern "C" {
#endif



UW getSpanCoef(void);
void setSpanCoef(UW data);
unsigned char getDataRate(void);
void cmdZeroExec(void);
void cmdSpanExec(void);

void test_zero(void);
void test_ad(void);
void test_span(void);

#ifdef __cplusplus
}
#endif
