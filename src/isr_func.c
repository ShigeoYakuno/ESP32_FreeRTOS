#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "log_task.h"
#include "user_test.h"
#include "freertos/portmacro.h"
#include "esp_rom_sys.h"
#include "isr_func.h"

// ===== 割り込みマスクレベルを取得 =====
inline uint32_t get_imask(void)
{
    uint32_t ps;
    asm volatile ("rsr %0, ps" : "=r"(ps));
    return ps & 0xF;  // 下位4bitがINTLEVEL
}

// ===== 割り込みマスクレベルを設定 =====
inline void set_imask(uint32_t level)
{
    uint32_t ps;
    asm volatile ("rsr %0, ps" : "=r"(ps));      // 現在のPSを取得
    ps = (ps & ~0xF) | (level & 0xF);            // 下位4bitだけ更新
    asm volatile ("wsr %0, ps" :: "r"(ps) : "memory");
    asm volatile ("rsync");                      // 書き込みを反映
}


/*
    portENTER_CRITICAL() / portEXIT_CRITICAL() のテスト
    FreeRTOSのSMP安全な割り込み禁止と復帰の動作確認
*/

// グローバルにスピンロックを1個定義（FreeRTOSが使う排他制御用構造体）
static portMUX_TYPE my_spinlock = portMUX_INITIALIZER_UNLOCKED;

void testISR_1(void)
{
    portENTER_CRITICAL(&my_spinlock);//割り込み禁止
    
    // ↓ この間は他の割り込み・スケジューラ割込みが発生しない
    esp_rom_delay_us(10); // 10µs待つ
    // ↑ 割り込み禁止時間が長すぎるとWDTに引っかかる
    
    portEXIT_CRITICAL(&my_spinlock);  // 割り込み許可
    
    syslog(INFO, "taskEXIT_CRITICAL() done");

}

/*
    PSレジスタ直接操作テスト
    Xtensa CPU の PS(Processor Status) レジスタの下位4bitは
    割り込みレベル(INTLEVEL)を示す。
    INTLEVELを7(最大)に設定すると全割り込みがマスクされる。
    ここではアセンブラ命令 rsr / wsr を使って
    直接PSレジスタを読み書きし、完全に割り込みを禁止する。
    FreeRTOSの保護なしにCPUレベルで割り込みを止めるため、
    ごく短時間(数十µs程度)で使う。
*/
void testISR_2(void)
{   
    uint32_t old_ps;

    // 現在のPSレジスタを読み取る（rsr命令：Read Special Register）
    asm volatile("rsr %0, ps" : "=r"(old_ps));

    // PSレジスタの下位4bit(INTLEVEL)を7にして全割り込み禁止
    // wsr命令：Write Special Register
    asm volatile("wsr %0, ps" :: "r"(old_ps | (7 << 0)) : "memory");

    // この区間ではハードウェアレベルで全割り込みが止まる
    esp_rom_delay_us(10);//クリティカル処理を実行
    
    // PSレジスタを元に戻す（割り込み再許可）
    asm volatile("wsr %0, ps" :: "r"(old_ps) : "memory");

    syslog(INFO, "PS register restore done");
}


/*
    SHでおなじみの割り込みマスク関数を模したもの
    一時的に割り込みレベルを上げたいときに使用する
*/
void testISR_3(void)
{
    uint32_t old = get_imask();
    set_imask(7);   // 全割り込み禁止
    // --- クリティカル処理 ---
    set_imask(old); // 元に戻す
    
}
