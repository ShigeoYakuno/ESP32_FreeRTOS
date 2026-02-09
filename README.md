
# FreeRTOS学習ガイド（Mastering the FreeRTOS Real-Time Kernel v1.1.0 ベース）


## 1. FreeRTOS配布物とプロジェクト構成

### 1.1 ディレクトリ構造と必須ソース
- `FreeRTOS/Source`: **必須** `tasks.c`, `list.c`、**ほぼ必須** `queue.c`、必要に応じ `timers.c`,
  `event_groups.c`, `stream_buffer.c`（メッセージ/ストリームバッファ）
- `portable/…`: ポート依存 (`portmacro.h`含む)、メモリ管理 (`MemMang/heap_*.c`)
- **インクルードパス**（必須3本）:
  1) `Source/include`
  2) `Source/portable/<compiler>/<arch>` 
  3) `FreeRTOSConfig.h`の置き場所

### 1.2 FreeRTOSConfig.h（Ch.2.2.3）
- プロジェクト固有に配置。`FreeRTOS.h`経由で自動インクルード。
- **代表設定**（抜粋）
  - スケジューラ: `configUSE_PREEMPTION`, `configUSE_TIME_SLICING`, `configMAX_PRIORITIES`
  - ティック: `configTICK_RATE_HZ`, `configTICK_TYPE_WIDTH_IN_BITS`
  - メモリ: `configTOTAL_HEAP_SIZE`, `configSUPPORT_STATIC_ALLOCATION`, `configSUPPORT_DYNAMIC_ALLOCATION`
  - 同期: `configUSE_MUTEXES`, `configUSE_COUNTING_SEMAPHORES`, `configUSE_QUEUE_SETS`
  - タイマー: `configUSE_TIMERS`, `configTIMER_TASK_PRIORITY`, `configTIMER_TASK_STACK_DEPTH`, `configTIMER_QUEUE_LENGTH`
  - フック: `configUSE_IDLE_HOOK`, `configUSE_TICK_HOOK`, `configUSE_MALLOC_FAILED_HOOK`
  - デバッグ: `configASSERT`, `configCHECK_FOR_STACK_OVERFLOW`


### 1.3 デモの使い方
- FreeRTOSのポートごとに割り込みハンドラやスタートアップコード、コンパイラ設定が異なる。
- デモプロジェクトはそのポートに最適化済みなので、正しい割り込み設定（SysTickやPendSVなど）が記載済み。
- FreeRTOSConfig.hの推奨値やリンカスクリプトやヒープ設定もすでに整っている。
- 既存デモから**削ぎ落として流用**するのが確実（割り込み/スタートアップ/最適オプション込み）
- ESP-IDFのFreeRTOSは独自拡張（マルチコア対応）なので、ESP-IDFのサンプル（examples/freertos）をベースにするのがベスト。
---

## 2. ヒープメモリ管理（ESP32視点）

### 2.1 動的/静的割り当て
- **動的**: FreeRTOS APIが**ヒープ**からTCB（Task Control Block）、スタック、キュー、セマフォなどを確保（`pvPortMalloc`/`vPortFree`）。
- **静的**: `xTaskCreateStatic` 等で**事前確保したメモリを渡す**。決定性が高く、メモリ不足時の挙動が明確。

### 2.2 5種類のヒープ実装
- `heap_1`: **割当のみ**（freeなし）—決定性・最小コード。初期生成のみのシステム向け。
- `heap_2`: best‑fit + free（**非推奨**、`heap_4`優先）。
- `heap_3`: libc `malloc/free` をスケジューラ停止でスレッドセーフ化。
- `heap_4`: first‑fit + **coalesce（隣接ブロック統合）**。動的生成/削除が多い場合に最適。
- `heap_5`: `heap_4` + **複数領域対応**（分割RAMや外部メモリを統合する場合に便利）。

### 2.3 ESP32での実際の勘所
- **`heap_4`が無難**：ESP32はWi-Fi/BLEスタックやタスク生成が多く、動的確保と解放が頻発するため、断片化に強い`heap_4`が推奨。
- **タスクスタックは必ず内蔵RAM**に置く  
- スタックは頻繁にアクセスされるため、PSRAM（外部RAM）だとレイテンシが大きくリアルタイム性が低下。
- DevkitCにはPSRAMはついていないが、ESP-IDFの`heap_caps_malloc(MALLOC_CAP_SPIRAM)`でPSRAMを指定可能。
- **ヒープのチューニング**：
  - `xPortGetFreeHeapSize()`：現在の空きヒープサイズ。
  - `xPortGetMinimumEverFreeHeapSize()`：起動後の最小空き量（安全マージン確認）。
  - `vPortGetHeapStats()`：断片化やブロック数を確認。
- **ヒープ配置制御**：
  - `configAPPLICATION_ALLOCATED_HEAP`を使えば、ヒープ領域を自分で定義可能（高速RAMに配置）。
- **スタック専用メモリ制御**：
  - `pvPortMallocStack`/`vPortFreeStack`を定義すれば、タスクスタックだけを高速RAMに確保可能（ESP-IDFでサポート）。

#### 2.4 ESP32メモリ構造のポイント(スタックとヒープの違い)
- スタックは各タスクに専用で割り当てられるメモリ領域。
- 用途：ローカル変数、関数呼び出し時のコンテキスト。
- 静的：xTaskCreateStatic()で事前に確保した領域を渡す。動的：xTaskCreate()でヒープから確保。

- ヒープは、FreeRTOSの動的メモリ管理で使う共通領域。
- 用途：タスク制御ブロック（TCB）、キュー、セマフォ、その他オブジェクト。
- 確保方法：pvPortMalloc() / vPortFree()。

- **内蔵DRAM**：IRAM/DRAM（高速、リアルタイム処理向き）。
- **PSRAM**：大容量だが外部接続（SPI/QSPI）、キャッシュヒットしないと遅い。
- **FreeRTOSヒープ**：通常は内蔵DRAMに置く。PSRAMはバッファ専用に使う。

---

## 3. タスク管理

### 3.1 タスクの基本
- 形: `void vTask(void* pvParameters)` 基本的に無限ループ、削除したい場合は`vTaskDelete(NULL)`でリターン禁止
- 状態: **Running / Ready / Blocked / Suspended**（Blockedは時間/同期待ち）
- 優先度: `0..configMAX_PRIORITIES-1`、数は`FreeRTOSConfig.h`で設定
- 補足:FreeRTOSのタスクは無限ループで動作することを前提に設計されている。  
　　　タスク関数がreturnすると、FreeRTOSではスタックやTCB（Task Control Block）の解放処理は自動では行われない。  
　　　そのため、returnしてしまうとメモリリークや不定動作の原因になる。  


### 3.2 生成とスタック
- `xTaskCreate` / `xTaskCreateStatic`（スタック**ワード数**指定に注意）
- スタックサイズ最適化: `uxTaskGetStackHighWaterMark`で余裕があるか把握できる

### 3.3 時間管理
- ティック: `configTICK_RATE_HZ`、`pdMS_TO_TICKS()`で換算
- 遅延: `vTaskDelay`（相対）/ `vTaskDelayUntil`（**厳密周期**）
- 補足: `vTaskDelay`は ITRONのtslp_tskやdly_tskに近い動作。  
        「今から指定ティック数だけ待つ」という相対指定なので以下の注意点がある  
        ・待機中に高優先度タスクや割り込み処理が走ると、実際の再開は遅れる。  
        ・指定時間は最小保証であり、実際はそれ以上になる可能性がある。  
        一方、`vTaskDelayUntil`は「前回の起床時刻を基準に、次の起床時刻を計算」する  
        高優先度タスクや割り込みで遅れた場合でも、次の周期は元の基準から計算される  
        遅れを取り戻す（スケジュールを追いつかせる）ので、周期が安定


### 3.4 スケジューリング
- `configUSE_PREEMPTION`, `configUSE_TIME_SLICING` で挙動選択
- **Preemptive+TimeSlice**が一般的。**Cooperative**は上級（割り込み/明示yieldで切替）

### 3.5 アイドルタスク/フック
- アイドルは最下位優先度。自己削除タスクのリソース回収を担当
- `configUSE_IDLE_HOOK` で背景処理/アイドル割合計測/簡易省電力

---

## 4. キュー管理

### 4.1 基本
- 固定長アイテムのFIFO（**コピー**で保持）
- 読み/書き双方で**ブロック時間**指定可
- **複数ライタ/複数リーダ**対応（解除は優先度・待機時間順）

### 4.2 特殊ケース
- 大きな可変長データの場合は、**ポインタ**で固定長でアドレスを送る運用にする
- 同様に多様なイベントは、キューで**構造体**を送ることもできる
- 複数ソース待ちは、**Queue Set**（設計制約時のみで、通常は単一キュー＋構造体なので実際使わない）


### 4.3 メールボックス（長さ1）
- `xQueueOverwrite`/`xQueuePeek` で上書き・参照  
- **用途**: 最新値を保持する「状態共有」に最適（例：センサ値、設定値）  
- **特徴**: 上書きは古いデータを破棄するので、**最新データのみ必要な場合に有効**  
- **注意**: 長さ1のキュー専用。ISRから使う場合は`xQueueOverwriteFromISR()`を使う

---

## 5. ソフトウェアタイマー

### 5.1 コア概念
- **タイマーはタスクコンテキスト**（RTOS Daemon/Timer Service Task）でコールバック実行
- itronの周期ハンドラ(非タスク)とは違うので注意。
- **ブロッキングAPI禁止**（xQueueReceive 等は `0` ブロックのみ）
- タイマーの種類は、1回だけコールバックを実行する1ショットと周期的に呼ばれるオートリロードがある
- **Timer ID**で多重化して、1つのコールバック関数で、複数のタイマーを識別できる。(コード量、可読性向上)

### 5.2 コマンドキューと優先度
- `configTIMER_QUEUE_LENGTH`, `configTIMER_TASK_PRIORITY`, `configTIMER_TASK_STACK_DEPTH`
- 送信→デーモンタスクが処理。デーモンタスク優先度を**適切に**設定（期限に直結）
- FreeRTOSのソフトウェアタイマーは、ハードウェア割り込みではなく、**「タイマーサービス（デーモン）タスク」**が実行
- タイマーコールバックはデーモンタスクの優先度で実行されるので優先度の設定が必要

### 5.3 代表API
- `xTimerCreate`, `xTimerStart/Stop`, `xTimerChangePeriod`, `vTimerSetTimerID`/`pvTimerGetTimerID`

---

## 6. 割り込み管理

### 6.1 ISRセーフAPI
- `FromISR` 付きAPIを使用（`xQueueSendToBackFromISR`, `vTaskNotifyGiveFromISR` 等）
- `xHigherPriorityTaskWoken`/`portYIELD_FROM_ISR()`で**コンテキスト切り替え**
- 通常APIはブロッキングやスケジューラ操作を伴うため、ISRでは危険。
- FromISR版は非ブロッキングで、割り込み中でも安全に使えるよう設計されている。

### 6.2 遅延実行（Deferred ISR）
- ISRで重い処理を避け、最低限の処理だけを行い、残りをタスクコンテキストで実行する設計パターン。
- セマフォの場合、ISRでxSemaphoreGiveFromISR() を呼び、タスク側で xSemaphoreTake()。
- タスク通知の場合、ISRでvTaskNotifyGiveFromISR()を呼び、タスク側で ulTaskNotifyTake()。
- ISRからxTimerPendFunctionCallFromISR()で「関数呼び出し要求」をタイマーサービス（タスク）に送信。  
  これを使えば、タスクコンテキスト(サービスデーモン)で関数を実行できる。


### 6.3 ネストと優先度注意
- Cortex-MやGICなどは、割り込み優先度はビットフィールドで管理されている。
- FreeRTOSは、ISRからRTOS APIを呼び出す際に優先度制約がある。
- configMAX_SYSCALL_INTERRUPT_PRIORITY より低い（数値が大きい）優先度の割り込みからのみ、FromISR APIを呼べる。
- 高優先度ISRでRTOS APIを呼ぶと、デッドロックやハードフォルトが発生する。
- 割り込みネストが発生すると、高優先度ISRが低優先度ISRを中断する。
- FreeRTOSはカーネルクリティカルセクション中に割り込みを完全禁止しないので、優先度次第でスケジューラ整合性が崩れる可能性有。
- configPRIO_BITSの設定ミスは多い。ARM Cortex-Mは、実際の優先度ビット数に合わせる。（例：STM32は4ビット）。
- configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITYは、API呼び出し可能な最大優先度
- configKERNEL_INTERRUPT_PRIORITYは、カーネルが使う最下位優先度
- ISRで通常APIを呼んではいけない。必ずFromISR版を使用する。(itronみたいに統合されていない)
---

## 7. リソース管理

### 7.1 クリティカルセクション
- FreeRTOSでは、共有リソースを安全に扱うために「クリティカルセクション」を設ける。
- 代表的なAPI`taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`
- 割り込みを禁止し、現在のタスクが安全に共有リソースを操作できるようにする。
- 割り込みを（カーネル管理範囲で）無効化。ネスト可能（内部カウンタで管理）。
- **注意点** 長時間禁止はリアルタイム性を損なう。ブロッキングAPIは使用禁止。ISRでは使用不可。
- **用途** 数µs〜ms程度の短時間で、完全排他が必要な処理。

### 7.2 スケジューラ停止
- 代表API`vTaskSuspendAll()` / `xTaskResumeAll()`
- タスク切り替えを一時停止し、現在のタスクが処理を途切れなく実行できるようにする。
- 割り込みは許可されたまま。ISRからの通知はキューに溜まる。
- **注意点** 長時間停止も比較的許容されるが、再開時に保留処理が一度に走る。
- **用途** 複数の関連データを一括更新する場合など。


### 7.3 設計のポイント
- クリティカルセクションは最小限に。
- スケジューラ停止は長めでもOKだが、ISRとの整合性に注意。
- ISRからは必ず`FromISR` APIを使用。
- クリティカルセクション内でブロッキングAPIを使っていないか？
- 長すぎるクリティカルセクションでタイミングが乱れていないか？
- ISRで通常APIを呼んでいないか？
- 割り込み優先度設定（`configMAX_SYSCALL_INTERRUPT_PRIORITY`）が正しいか？


---

## 8. イベントグループ

- **ビット集合を使って複数のイベント状態を管理し、タスク間同期や条件待ちを効率的に行う仕組み**。  
- `EventGroupHandle_t` 型で管理し、**ビット単位でフラグを設定・待機**させる。

### 8.1 基本概念
- 各ビットは「イベントフラグ」を表す。
- 複数ビットを組み合わせて「AND条件」「OR条件」で待機可能。
- タスク間で共有され、ISRからも操作可能（専用API使用）。


### 8.2 多条件待ち：`xEventGroupWaitBits`
- **目的** 指定したビット集合が揃うまで待機する。
- AND条件（全ビット揃う）またはOR条件（いずれか揃う）を選択可能。
- 待機後にビットをクリアするか保持するか選択可能。
- タイムアウト指定可能。
- `uxBitsToWaitFor`：待機対象ビット集合。
- `xClearOnExit`：待機解除後にビットをクリアするか。
- `xWaitForAllBits`：AND条件（true）かOR条件（false）。
- `xTicksToWait`：タイムアウト。

### 8.3 ISRからの操作：`xEventGroupSetBitsFromISR`
- **目的** ISRからイベントビットをセットし、タスクに通知する。
- `BaseType_t *pxHigherPriorityTaskWoken` を使って即時コンテキスト切替可能。
- **注意** ISRでは通常APIを使わず、必ず`FromISR`版を使用。

### 8.4 タスク同期：`xEventGroupSync`（バリア的動作）
- **目的**  複数タスクが「全員到達」するまで待機する、**バリア同期**を実現。
- 各タスクは自分のビットをセットし、指定ビット集合が揃うまで待機。
- 全タスクが到達したら、全員が解除される。
- `uxBitsToSet`：自タスクがセットするビット。
- `uxBitsToWaitFor`：全員到達を示すビット集合。
- `xTicksToWait`：タイムアウト。

### 8.5 設計のポイント
- イベントグループは**ビット単位の軽量同期**に最適。
- **複数条件待ち**や**バリア同期**を簡潔に記述可能。
- **ISRからは必ず`FromISR` APIを使用**。
- ビット数は32ビット固定（`EventBits_t`）。
- ビットの再利用時にクリアを忘れることがある。
- タスク通知やセマフォとの使い分け不明確で、設計が混乱する。
- タイムアウト処理を考慮しない場合、デッドロックしてしまう。



---

## 9. タスク通知（Direct to Task Notification）

### 9.1 特徴
- **軽量・高速**（キュー/セマフォの代替）
- **1タスクにつき1通知スロット**→**1対1用途**（ブロードキャスト不可）

### 9.2 代表的置換パターン
- バイナリセマフォ: `vTaskNotifyGiveFromISR` + `ulTaskNotifyTake`
- カウンティング: `xTaskNotifyGive`（加算） + `ulTaskNotifyTake(pdFALSE, …)`
- 値受け渡し: `xTaskNotify(..., eSetValueWithOverwrite/WithoutOverwrite)`

---



## 10. デベロッパ支援

- `configASSERT`の実装（行番号記録など）
- スタック/ランタイム統計: `vTaskGetRunTimeStats`, `uxTaskGetSystemState`
- Stack overflow hook / Malloc failed hook
- トレース（Tracealyzer/Traceフック）

---

## 11. トラブルシュート

- FreeRTOSでよく遭遇する問題と、その原因

### 11.1 割り込み優先度設定ミス
- **症状**
  - ISRから`FromISR` APIを呼んだ際にハードフォルトや不定動作。
  - タスク通知やキュー送信が機能しない。
- **原因**
  - `configMAX_SYSCALL_INTERRUPT_PRIORITY`より高い優先度のISRでRTOS APIを呼んでいる。
  - `configPRIO_BITS`の設定ミス（Cortex-Mで実際の優先度ビット数と不一致）。
- **対策**
  - `FreeRTOSConfig.h`で優先度設定を確認。
  - ISRでRTOS APIを使う場合は、必ず優先度を制約範囲内に設定。


### 11.2 スタック不足
- **症状**
  - タスクが突然クラッシュ、ハードフォルト、不可解なリセット。
- **原因**
  - タスクスタックサイズが不足。
  - 再帰呼び出しや大きなローカル配列で消費。
- **検出方法**
  - `uxTaskGetStackHighWaterMark()`で残量確認。
  - スタックオーバーフローフック（`vApplicationStackOverflowHook`）を有効化。
- **対策**
  - タスク作成時に十分なスタックサイズを指定。
  - 動的に`HighWaterMark`を監視して調整。


### 11.3 ISRで非ISR API使用
- **症状**
  - ハードフォルト、デッドロック、スケジューラ異常。
- **原因**
  - ISR内で通常API（`xQueueSend`など）を呼んでいる。
- **対策**
  - ISRでは必ず`FromISR`版APIを使用。
  - `portYIELD_FROM_ISR()`で即時コンテキスト切替を考慮。


### 11.4 クリティカルセクション中にAPI呼び出し
- **症状**
  - タスクが停止、システムハング。
- **原因**
  - `taskENTER_CRITICAL()`中にブロッキングAPIを呼んでしまう。
- **対策**
  - クリティカルセクションは短時間で完了。
  - ブロッキングAPIは絶対に呼ばない。


### 11.5 スケジューラ未起動
- **症状**
  - タスクが動かない、`vTaskStartScheduler()`後に停止。
- **原因**
  - ヒープ不足でIdleタスクやTimerタスクが生成できない。
- **対策**
  - `configTOTAL_HEAP_SIZE`を増やす。
  - `heap_x.c`の実装（メモリ管理方式）を確認。
  - `xTaskCreate()`の戻り値や`configUSE_IDLE_HOOK`で診断。

---

## 12. FreeRTOSConfig.h で頻出する設定

- スケジューリング:
  - `#define configUSE_PREEMPTION        1`
  - `#define configUSE_TIME_SLICING      1`
  - `#define configMAX_PRIORITIES        ( 16 )`（ESP32は32でも可だが過大はRAM増）
- ティック/時間:
  - `#define configTICK_RATE_HZ          ( 1000 )`（1ms）
  - `#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS`
- メモリ:
  - `#define configTOTAL_HEAP_SIZE       ( 64*1024 )`（目安。実機で最小余裕を確認）
  - `#define configSUPPORT_STATIC_ALLOCATION 1`
  - `#define configSUPPORT_DYNAMIC_ALLOCATION 1`
- 同期/通信:
  - `#define configUSE_MUTEXES           1`
  - `#define configUSE_COUNTING_SEMAPHORES 1`
  - `#define configUSE_QUEUE_SETS        1`
- タイマー:
  - `#define configUSE_TIMERS            1`
  - `#define configTIMER_TASK_PRIORITY   ( configMAX_PRIORITIES-2 )`
  - `#define configTIMER_QUEUE_LENGTH    16`
  - `#define configTIMER_TASK_STACK_DEPTH ( 2048/sizeof(StackType_t) )`
- フック/デバッグ:
  - `#define configUSE_IDLE_HOOK         0`
  - `#define configUSE_TICK_HOOK         0`
  - `#define configCHECK_FOR_STACK_OVERFLOW 2`
  - `#define configUSE_MALLOC_FAILED_HOOK 1`
  - `#define configASSERT(x)             if( ( x ) == 0 ) vAssertCalled(__FILE__, __LINE__)`
- 低消費電力:
  - `#define configUSE_TICKLESS_IDLE     0`（必要時に1）

> 実際の値はSoC・負荷・RAMに応じ調整。特に`configMAX_PRIORITIES`は増やし過ぎると**RAM**と**最悪時実行時間**が悪化。

---

## 13. ESP32（ESP‑IDF）での実装上の注意

1) **コアアフィニティ**
- `xTaskCreatePinnedToCore()` で core0/core1 固定。Wi‑Fi/BTと競合しないコアに制御系を配置。
- タイマー/デーモンタスクの優先度は**Wi‑Fiタスクより下げすぎない**。

2) **ISRの実装**
- `IRAM_ATTR`配置（割り込み遅延短縮）。FromISR API＋`portYIELD_FROM_ISR()`忘れに注意。

3) **メモリ**
- **タスクスタックは内蔵RAM**に。大容量バッファ/画像/ログはPSRAM検討(外付RAMあるシリーズなら)
- `heap_caps_malloc(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL, ...)` 等、領域指定の活用。

4) **タイマー**
- ソフトウェアタイマーの期限厳守が必要なら`configTIMER_TASK_PRIORITY`を**高め**に。

5) **ログ/printf**
- `printf` は標準出力やUARTに直接書き込みを行うため、**送信完了までブロック**することが多い。
- **フォーマット処理の負荷**  で可変引数や文字列変換など、CPU負荷が高い処理を含む。
- 長時間ブロックすると、リアルタイム性が低下し、他タスクやISRの応答が遅れる。
- 各タスクはログメッセージをリングバッファに書き込み、専用ログタスクがバッファから取り出し、UART出力する方式を推奨。
- 例：`xQueueSend()`でログ要求を非同期送信。ログタスクが受信して出力。
- ログタスクは低優先度で、システム負荷が低いときに処理。リングバッファサイズを十分確保すること。

6) **コンテキストの見分け方**
- 例えば独自ログ関数を、コンテキストを気にせず呼べるようにしたい場合、関数コールしたコンテキストを知りたい場合がある。
- FreeRTOSには「現在のコンテキストがタスクかISRかを直接判定する公式APIは無いが、ESP32(Xtensa)にはその機能がある。
- xPortInIsrContext()というESP-IDF独自APIで「True」ならISR、「False」ならタスク内と判断できる。
- Cortex-M系の場合は、SCB->ICSRレジスタのVECTACTIVEフィールドで見分けることができる。


---

## その他:主要API早見表

- タスク: `xTaskCreate`, `vTaskDelete`, `vTaskDelay`, `vTaskDelayUntil`, `vTaskPrioritySet`
- キュー: `xQueueCreate`, `xQueueSendToBack`, `xQueueReceive`, `xQueueOverwrite`, `xQueuePeek`
- セマフォ/ミューテックス: `xSemaphoreCreateBinary`, `xSemaphoreGiveFromISR`, `xSemaphoreCreateMutex`
- イベント: `xEventGroupCreate`, `xEventGroupSetBits`, `xEventGroupWaitBits`
- 通知: `vTaskNotifyGiveFromISR`, `ulTaskNotifyTake`, `xTaskNotify`, `xTaskNotifyWait`
- タイマー: `xTimerCreate`, `xTimerStart`, `xTimerChangePeriod`
- 省電力: `vPortSuppressTicksAndSleep`
- デバッグ: `uxTaskGetStackHighWaterMark`, `vTaskGetRunTimeStats`, `configASSERT`

---



**以上**
