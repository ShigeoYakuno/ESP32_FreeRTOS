#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>


// 一般的なマクロ定義
#define	SSIZE( m, s )	sizeof( ((m*)0)->s )		/*要素のバイト数に変換*/
#define	Bcnv( a )		( (a + 1) / 2 )				/*桁数をバイト数に変換*/
#define	_byte( a )		( (a + 1) / 2 )				/*桁数をバイト数に変換*/
#define	DIM( x )		( sizeof(x)/sizeof(x[0]) )	/*配列の要素数を求める*/

#define GET_1_OF_WORD(a)	(((unsigned short)a & 0xFF00) >> 8)		// unsigned short の上１バイトを取り出す
#define GET_2_OF_WORD(a)	((unsigned short)a & 0xFF)				// unsigned short の下１バイトを取り出す
#define GET_WORD(a)			((((unsigned char *)a)[0] << 8) + ((unsigned char *)a)[1])	// unsigned short を取り出す
#define SET_WORD(a,b)		{((unsigned char *)a)[0]=GET_1_OF_WORD((unsigned short)b); ((unsigned char *)a)[1]=GET_2_OF_WORD((unsigned short)b);}	// unsigned short を書き込む

#define GET_1_OF_DWORD(a)	(((unsigned int)a & 0xFF000000) >> 24)	// unsigned int(4バイト) の1バイト目を取り出す
#define GET_2_OF_DWORD(a)	(((unsigned int)a & 0xFF0000) >> 16)	// unsigned int(4バイト) の2バイト目を取り出す
#define GET_3_OF_DWORD(a)	(((unsigned int)a & 0xFF00) >> 8)		// unsigned int(4バイト) の3バイト目を取り出す
#define GET_4_OF_DWORD(a)	((unsigned int)a & 0xFF)				// unsigned int(4バイト) の4バイト目を取り出す

// unsigned intを取り出す
#define GET_DWORD(a)		((((unsigned char *)a)[0] << 24) + (((unsigned char *)a)[1] << 16) + (((unsigned char *)a)[2] << 8) + ((unsigned char *)a)[3])
// unsigned int を書き込む
#define SET_DWORD(a,b)		{((unsigned char *)a)[0]=GET_1_OF_DWORD((unsigned int)b); ((unsigned char *)a)[1]=GET_2_OF_DWORD((unsigned int)b); ((unsigned char *)a)[2]=GET_3_OF_DWORD((unsigned int)b); ((unsigned char *)a)[3]=GET_4_OF_DWORD((unsigned int)b);}


// --- 汎用フラグ型 ---
typedef uint8_t BOOL;

#ifndef TRUE
#define TRUE  (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

	/*==========================================================
							構造体宣言
	==========================================================*/

// --- 基本整数型 ---
typedef uint8_t   UB;   // Unsigned Byte (8bit)
typedef int8_t    B;    // Signed Byte (8bit)
typedef uint16_t  UH;   // Unsigned Halfword (16bit)
typedef int16_t   H;    // Signed Halfword (16bit)
typedef uint32_t  UW;   // Unsigned Word (32bit)
typedef int32_t   W;    // Signed Word (32bit)

typedef struct {
	uint32_t  upper;
	uint32_t  lower;
} DLONG;

void *my_itoa( long value, void *asc, unsigned short keta );
void *my_ltoa( long value, void *asc, unsigned short keta );
void dlong_div( DLONG *s, unsigned long dst, DLONG *a );
void dlong_mul( unsigned long src, unsigned long dst,  DLONG *a );

void exec_soft_reset(void);

#ifdef __cplusplus
}
#endif
