#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "scale_data.h"
#include "scale_filter.h"
#include "scale_cmd.h"
#include "user_common.h"

/* -------------------------------------------------------------------------
        定数定義
   -------------------------------------------------------------------------*/
#define STB_CNT_INIT 5
#define STB_WIDTH    3
#define UNST_WIDTH   50
#define BIAS_ZERO    20000
#define COUNT_1EYE   10
#define DIGIT_1EYE   1

static const char *TAG = "scale_data";

/* -------------------------------------------------------------------------
            構造体定義
   ----------------------------------------------------------------------*/
typedef struct {
	UW raw;							/*	ADCからの生データ*/
	UW normalize;					/*	スパン係数で正規化したデータ*/
	UW filter;						/*	デジタルフィルタを通したデータ	*/
	UW base;						/*	フィルタまで通したベースデータ	*/
	UW previous;					/*	前回データ	*/
	UW display;						/*	表示送信用データ	*/
	UW true_zero;					/*	真ゼロデータ	*/
	UW tare_zero;					/*	風袋ゼロデータ	*/

	union {
		UB byte;
		struct {
			UB stb_flg:1;			/*	安定フラグ 0:不安定 1:安定	*/
			UB restb_latch_flg:1;	/*	再安定ラッチフラグ 0:状態変化なし 1:安定⇒再安定となった1回目	*/
			UB minus_flg:1;			/*	マイナスフラグ 0:+ 1:-	*/
			UB disp_flg:1;			/*	表示更新フラグ 0:非更新 1:更新	*/
			UB cnt_flg:1;			/*	精度拡張モードフラグ*/
			UB stb_latch_flg:1;		/*	安定ラッチフラグ 0:状態変化なし 1:不安定⇒安定となった1回目 */
			UB send_test_flg:1;		/*	送信データがAD値か重量値か	*/
			UB send_req_flg:1;		/*	送信要求フラグ	*/
		} bit;
	} states;

	UW ofs_tare_data;				/*	オフセットした風袋ゼロ基準データ	*/
	UW ofs_base_data;				/*	オフセットした真ゼロ基準データ	*/
	W snd_weight_data;				/*	送信重量データ	*/
} SCALE_DATA;

static SCALE_DATA sdata;

/* 安定判定用カウンタ */
static B stb_cnt = STB_CNT_INIT;

/* -------------------------------------------------------------------------
            内部関数プロトタイプ
   ----------------------------------------------------------------------*/
static UW calNormalizeData(UW ad);
static UB chkWidth(W sample, W standard, W width, UB mode);
static void UnstbExec(void);
static void unstbCtl(void);
static void stbCtl(void);

/* -------------------------------------------------------------------------
            Getter / Setter 群
   ----------------------------------------------------------------------*/
UW getRawData(void){return sdata.raw;} void setRawData(UW d){sdata.raw=d;}
UW getNormalizeData(void){return sdata.normalize;} void setNormalizeData(UW d){sdata.normalize=d;}
UW getFilteredData(void){return sdata.filter;} void setFilteredData(UW d){sdata.filter=d;}
UW getBaseData(void){return sdata.base;} void setBaseData(UW d){sdata.base=d;}
UW getTrueZeroData(void){return sdata.true_zero;} void setTrueZeroData(UW d){sdata.true_zero=d;}
UW getTareZeroData(void){return sdata.tare_zero;} void setTareZeroData(UW d){sdata.tare_zero=d;}
UW getDispData(void){return sdata.display;} void setDispData(UW d){sdata.display=d;}
UW getPrevData(void){return sdata.previous;} void setPrevData(UW d){sdata.previous=d;}
UW getOfsTareData(void){return sdata.ofs_tare_data;} void setOfsTareData(UW d){sdata.ofs_tare_data=d;}
UW getOfsBaseData(void){return sdata.ofs_base_data;} void setOfsBaseData(UW d){sdata.ofs_base_data=d;}
W getSendWgtDt(void){return sdata.snd_weight_data;} void setSendWgtDt(W d){sdata.snd_weight_data=d;}
void setStatesDspFlg(UB f){sdata.states.bit.disp_flg=f;}

/* -------------------------------------------------------------------------
	スパン係数を掛ける
   ----------------------------------------------------------------------*/
static UW calNormalizeData(UW ad)
{
	DLONG cal_data;
	dlong_mul(ad, getSpanCoef(), &cal_data);
	if(cal_data.lower & 0x80000000) cal_data.upper++;
	return cal_data.upper;
}

/* -------------------------------------------------------------------------
	データ収束判定
	引数		sample		サンプル
				standard	基準
				width		幅
				mode		0:イコールは含まない 1:イコールは含む
	戻り値		0:範囲外 1:範囲内
   ----------------------------------------------------------------------*/
static UB chkWidth(W sample,W standard,W width,UB mode)
{
	if(sample > (standard+width)) return 0;
	if(sample == (standard+width)) return (mode==1)?1:0;
	if(sample < (standard-width)) return 0;
	if(sample == (standard-width)) return (mode==1)?1:0;
	return 1;
}

/* -------------------------------------------------------------------------
	正規化処理
   ----------------------------------------------------------------------*/
void normalizeExec(void)
{
	setNormalizeData(calNormalizeData(getRawData()));
}

/* -------------------------------------------------------------------------
	スパン係数が変わったため、データを再計算する
   ----------------------------------------------------------------------*/
void reCalcData(UW old_coef)
{
	DLONG dl1,dl2;
	UW r=getBaseData();
	/* 新データ＝ベースデータ×新スパン係数÷旧スパン係数 */
	dlong_mul(r,getSpanCoef(),&dl1);
	dlong_div(&dl1,old_coef,&dl2);
	setBaseData(dl2.lower);
}

/* -------------------------------------------------------------------------
	強制不安定処理
   ----------------------------------------------------------------------*/
static void UnstbExec(void)
{
	sdata.states.bit.stb_flg = 0;
	sdata.states.bit.disp_flg = 1;
	stb_cnt = STB_CNT_INIT;
}

/* -------------------------------------------------------------------------
	不安定チェック処理
	不安定幅範囲外に出ると不安定処理を行なう。
	不安定幅範囲内の場合は処理しない。
	不安定中は表示更新フラグセット。
   ----------------------------------------------------------------------*/
static void unstbCtl(void)
{
	/* 不安定中は処理しない */
	if(!sdata.states.bit.stb_flg) return;

	/* リードADが表示ADから不安定幅以内でない場合、不安定処理 */
	if(!chkWidth(getBaseData(),getDispData(),UNST_WIDTH,1))
		UnstbExec();
}

/* -------------------------------------------------------------------------
	安定チェック処理
	安定幅範囲内に安定回数入った場合、安定処理。
	安定中は処理しない。
	安定中でない場合は表示更新フラグセット。
   ----------------------------------------------------------------------*/
static void stbCtl(void)
{
	/* すでに安定の場合、安定回数リセット */
	if(sdata.states.bit.stb_flg){
		stb_cnt = STB_CNT_INIT;
		return;
	}

	/* リードADが前回ADから安定幅以内でない場合 */
	if(!chkWidth(getBaseData(),getDispData(),STB_WIDTH,1)){
		/* 安定回数リセット */
		stb_cnt = STB_CNT_INIT;
		/* 表示更新フラグセット */
		sdata.states.bit.disp_flg = 1;
		return;
	}

	/* 安定範囲内の場合、以下を実行 */
	stb_cnt--;
	if(stb_cnt <= 0){
		/* 安定フラグセット */
		sdata.states.bit.stb_flg = 1;
		/* 表示更新フラグセット */
		sdata.states.bit.disp_flg = 1;
	}
}

/* -------------------------------------------------------------------------
	送信ADデータ更新処理
   ----------------------------------------------------------------------*/
void updateSendData(void)
{
	/* 表示更新フラグがセットされている時 */
	if(sdata.states.bit.disp_flg){
		/* 表示AD更新 */
		sdata.display = sdata.base;
		/* 表示更新フラグリセット */
		sdata.states.bit.disp_flg = 0;
	}
	/* 前回AD更新 */
	sdata.previous = sdata.base;
}

/* -------------------------------------------------------------------------
	送信データ作成処理
   ----------------------------------------------------------------------*/
void newDataCreate(void)
{
	long ad,tare_ad,wgt_ad,wgt_data,tare_cnt;
	unsigned char digit,count;
	unsigned long bias,tare_bias_ad;

	tare_ad = getTareZeroData();
	tare_cnt = tare_ad - getTrueZeroData();

	bias = BIAS_ZERO;
	count = COUNT_1EYE;
	digit = DIGIT_1EYE;

	/* 重量値に使うのは、表示更新フラグ対応のAD */
	ad = getDispData();
	/* テストADに使うのは、毎回のAD */
	tare_bias_ad = getBaseData();

	/* マイナスチェック */
	if(ad < tare_ad){
		sdata.states.bit.minus_flg = 1;
		wgt_ad = tare_ad - ad;
		tare_bias_ad = tare_ad - tare_bias_ad;
		ad = bias - wgt_ad;
		tare_bias_ad = bias - tare_bias_ad;
	}
	else{
		sdata.states.bit.minus_flg = 0;
		wgt_ad = ad - tare_ad;
		tare_bias_ad = tare_bias_ad - tare_ad;
		ad = ad + bias;
		tare_bias_ad = tare_bias_ad + bias;
	}

	/* 送信用データを更新 */
	setOfsTareData(tare_bias_ad);				/* バイアスを加えた仮ゼロ基準AD */
	setOfsBaseData(tare_bias_ad + tare_cnt);	/* バイアスを加えた真ゼロ基準AD */

	/* 送信用ADデータを重量(グラム)に変換 */
	wgt_data = (wgt_ad + (count / 2)) / count * digit;
	setSendWgtDt(wgt_data);
}

/* -------------------------------------------------------------------------
	データ加工処理ルーチン
   ----------------------------------------------------------------------*/
UB processingData(void)
{
	/* 正規化 */
	normalizeExec();
	/* デジタルフィルタ処理 */
	if(!calcFirFilter(getNormalizeData())) return 0;
	/* 不安定チェック */
	unstbCtl();
	/* 安定チェック */
	stbCtl();
	/* ADデータ更新 */
	updateSendData();
	return 1;
}
