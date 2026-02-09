#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "scale_cmd.h"
#include "scale_data.h"
#include "user_common.h"
#include "log_task.h"

/* -------------------------------------------------------------------------
            定数定義
   ----------------------------------------------------------------------*/
#define SPAN_CNT 60000

static const char *TAG = "scale_cmd";

/* -------------------------------------------------------------------------
            構造体定義
   ----------------------------------------------------------------------*/
typedef struct {
	UW span_coef;
} FLASH_DATA;

static FLASH_DATA memdata;

/* -------------------------------------------------------------------------
            関数定義
   ----------------------------------------------------------------------*/
UW getSpanCoef(void)
{
    return memdata.span_coef;
}

void setSpanCoef(UW data)
{
    memdata.span_coef = data;
}

/* -------------------------------------------------------------------------
	AD変換レート設定(このレートで安定判断等を行う)
	1回のAD変換速度＝2.05ms
   ----------------------------------------------------------------------*/
unsigned char getDataRate(void)
{
	return 32;
}


//共通コマンド
void test_zero(void)
{
    syslog(INFO, "zero");
    cmdZeroExec();
}

void test_span(void)
{
    syslog(INFO, "span");
    cmdSpanExec();
}

void test_ad(void)
{
	syslog(INFO, "%d",getBaseData());
}


/* -------------------------------------------------------------------------
	真ゼロ点の取得
   ----------------------------------------------------------------------*/
void cmdZeroExec(void)
{
	UW ad;

	ad = getBaseData();
	/* 真ゼロデータをリードデータに更新 */
	setTrueZeroData(ad);
	/* 仮ゼロデータをリードデータに更新 */
	setTareZeroData(ad);
	/* 表示更新フラグセット */
	setStatesDspFlg(1);
	/* 前回Ａ／Ｄデータ更新処理 */
	updateSendData();
	/* 新規Ａ／Ｄデータ作成処理 */
	newDataCreate();

	syslog(INFO, "Zero Set true=%x, tare=%x", getTrueZeroData(), getTareZeroData());
}

/* -------------------------------------------------------------------------
	スパン係数 ＝（スパンカウント×スパン係数（Ｌ））／（リードカウント－真ゼロ（Ｌ）
   ----------------------------------------------------------------------*/
void cmdSpanExec(void)
{
	DLONG temp, new_coef, new_zero;
	UW read_ad, real_zero, ad, old_coef;

	read_ad = getBaseData();
	real_zero = getTrueZeroData();

	/* リードカウント＜＝真ゼロの場合は範囲外エラー */
	if(read_ad <= real_zero) return;

	dlong_mul(SPAN_CNT, getSpanCoef(), &temp);
	old_coef = getSpanCoef();

	ad = (read_ad - real_zero);
	dlong_div(&temp, ad, &new_coef);

	/* 真ゼロ（Ｎ）＝ 真ゼロ（Ｌ）×スパン係数（Ｎ）／スパン係数（Ｌ） */
	dlong_mul(getTrueZeroData(), new_coef.lower, &temp);
	dlong_div(&temp, getSpanCoef(), &new_zero);

	/* スパン係数を更新 */
	setSpanCoef(new_coef.lower);
	/* 真ゼロを更新 */
	ad = new_zero.lower;
	setTrueZeroData(ad);
	/* 仮ゼロデータをリードデータに更新 */
	setTareZeroData(ad);
	/* リードＡ／Ｄ再計算 */
	reCalcData(old_coef);
	/* 表示更新フラグセット */
	setStatesDspFlg(1);
	/* 新規Ａ／Ｄデータ作成処理 */
	newDataCreate();

	syslog(INFO, "Span updated coef=%x", new_coef.lower);
}
