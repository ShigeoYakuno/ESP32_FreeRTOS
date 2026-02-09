#pragma once
#include "user_common.h"

#ifdef __cplusplus
extern "C" {
#endif

void normalizeExec(void);
void reCalcData(UW old_coef);
UB processingData(void);
void newDataCreate(void);
void updateSendData(void);

UW getRawData(void);   void setRawData(UW);
UW getNormalizeData(void); void setNormalizeData(UW);
UW getFilteredData(void);  void setFilteredData(UW);
UW getBaseData(void);      void setBaseData(UW);
UW getTrueZeroData(void);  void setTrueZeroData(UW);
UW getTareZeroData(void);  void setTareZeroData(UW);
UW getPrevData(void);      void setPrevData(UW);
UW getDispData(void);      void setDispData(UW);
UW getOfsTareData(void);   void setOfsTareData(UW);
UW getOfsBaseData(void);   void setOfsBaseData(UW);
W  getSendWgtDt(void);     void setSendWgtDt(W);

void setStatesDspFlg(UB flag);

#ifdef __cplusplus
}
#endif
