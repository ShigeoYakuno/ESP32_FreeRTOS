#include "freertos/FreeRTOS.h"
#include "scale_filter.h"
#include "scale_data.h"
#include "scale_cmd.h"
#include "user_common.h"

/* -------------------------------------------------------------------------
            定数定義
   ----------------------------------------------------------------------*/
#define FIR_TAPS 65

/* Remz Algorithm Parks-McClellan 最初のノッチが15Hz 65TAP */
static const UW filter_coef[FIR_TAPS] = {
	0x0002E6FA,0x0001F6FD,0x0002991D,0x00035753,0x000432BE,0x00052CDD,0x00064631,0x00077F88,
	0x0008D84D,0x000A4FE7,0x000BE526,0x000D966C,0x000F6163,0x001143B4,0x0013391D,0x00153E90,
	0x00174E92,0x00196572,0x001B7DF0,0x001D92B3,0x001F9D5C,0x0021992A,0x00238068,0x00254DC1,
	0x0026FA07,0x00288226,0x0029E0FE,0x002B10BC,0x002C0F85,0x002CD88E,0x002D6A64,0x002DC282,
	0x002DE00C,0x002DC282,0x002D6A64,0x002CD88E,0x002C0F85,0x002B10BC,0x0029E0FE,0x00288226,
	0x0026FA07,0x00254DC1,0x00238068,0x0021992A,0x001F9D5C,0x001D92B3,0x001B7DF0,0x00196572,
	0x00174E92,0x00153E90,0x0013391D,0x001143B4,0x000F6163,0x000D966C,0x000BE526,0x000A4FE7,
	0x0008D84D,0x00077F88,0x00064631,0x00052CDD,0x000432BE,0x00035753,0x0002991D,0x0001F6FD,
	0x0002E6FA
};

/* -------------------------------------------------------------------------
            内部変数定義
   ----------------------------------------------------------------------*/
static unsigned long fltr_data[FIR_TAPS];
static unsigned char fltr_cnt = 0;
static unsigned char exec_cnt = 0;
static unsigned long coef_ttl = 0;

/* -------------------------------------------------------------------------
	フィルタ処理
   ----------------------------------------------------------------------*/
unsigned char calcFirFilter(unsigned long ad_data)
{
    DLONG mul, ans, sum = {0};
    long cnt = 0; const UW *tbl = filter_coef;
    if(fltr_cnt>=FIR_TAPS) fltr_cnt=0;
    fltr_data[fltr_cnt]=ad_data;

    if(++exec_cnt <= getDataRate()){ fltr_cnt++; return 0; }
    exec_cnt=0; cnt=fltr_cnt;

    for(int i=FIR_TAPS;i>0;i--){
        if(cnt<0) cnt=FIR_TAPS-1;
        dlong_mul(fltr_data[cnt--], tbl[i-1], &mul);
        sum.upper += mul.upper;
        sum.lower += mul.lower;
    }
    fltr_cnt++;
    dlong_div(&sum, coef_ttl, &ans);
    setFilteredData(ans.lower);
    setBaseData(ans.lower);
    return 1;
}

void firFilterInit(void)
{
    memset(fltr_data, 0, sizeof(fltr_data));
    coef_ttl=0;
    for(int i=0;i<FIR_TAPS;i++) coef_ttl += filter_coef[i];
}
