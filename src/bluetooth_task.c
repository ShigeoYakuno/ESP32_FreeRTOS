
#include <string.h>
#include "esp_bt.h"
#include "esp_spp_api.h"
#include "log_task.h"
#include "bluetooth_task.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_log.h"
#include "flash_data.h"

QueueHandle_t qBtRx = NULL;
QueueHandle_t logBtQueue = NULL;
volatile uint32_t bt_handle = 0;    // 現在のSPPハンドル（接続中のみ有効）
volatile bool bt_connected = false;  // 接続状態フラグ

static void bt_log_task(void *pv);
static void spp_init_task(void *pv);
static void syslog_switch_to_bt_task(void *pv);

static char bt_dev_name[32] = "PE_DEV_01";

// コールバック→タスク通知用（切替やUART復帰はここ経由）
static QueueHandle_t qCtl = NULL;
typedef enum { CTL_TO_BT, CTL_TO_UART } ctl_msg_t;



// --- GAP ---
static void gap_cb(esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p)
{
    switch (e) {
    case ESP_BT_GAP_PIN_REQ_EVT: {
        esp_bt_pin_code_t pin = { '3','2','1','1' };
        esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin);
        break;
    }
    default:
        break;
    }
}


static void bt_set_device_name_number(uint8_t num)
{
    snprintf(bt_dev_name, sizeof(bt_dev_name), "PE_DEV_%02u", num);
    esp_bt_dev_set_device_name(bt_dev_name);
    syslog(INFO, "BT name set to %s", bt_dev_name);
}

// フラッシュから復元時呼び出し用
static void bt_load_device_name_from_flash(void)
{
    uint8_t n = getBtDevNum();  // flash_data.cに追加するgetter
    if (n == 0xFF || n == 0) n = 1;      // 未設定時デフォルト01
    bt_set_device_name_number(n);
}

// --- SPP EV ---
// bt_event_cb の重要イベントで “printf”
static void bt_event_cb(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_INIT_EVT:
        printf("[SPP:EVT] INIT\n");
        
        //esp_bt_dev_set_device_name(bt_dev_name);
        bt_load_device_name_from_flash();
        printf("[SPP] set name OK\n");

        ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));
        printf("[SPP] GAP visibility set (connectable + discoverable)\n");
        
        ESP_ERROR_CHECK(esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, bt_dev_name));
        printf("[SPP] start_srv OK\n");
        break;

    case ESP_SPP_START_EVT:
        printf("[SPP:EVT] START\n");
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        bt_handle = param->srv_open.handle;
        bt_connected = true;
        printf("[SPP:EVT] SRV_OPEN handle=%ld\n", bt_handle);
        syslog_set_output(LOG_OUTPUT_BLUETOOTH);
        break;


    case ESP_SPP_DATA_IND_EVT: {
        // 受信データ処理
        uint8_t *d = param->data_ind.data;
        uint16_t len = param->data_ind.len;

        for (int i = 0; i < len; i++) {
            char c = (char)d[i];
            // 改行や制御文字も拾うため1文字ずつ投入
            if (qBtRx) {
                xQueueSend(qBtRx, &c, 0);
            }
        }

        break;
    }


    case ESP_SPP_CLOSE_EVT:
        printf("[SPP:EVT] CLOSE\n");
        bt_handle = 0;
        bt_connected = false;
        syslog_set_output(LOG_OUTPUT_UART);

        // 再待受け再開
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "YAKUN_TEST");
        break;


    default:
        break;
    }
}


// --- 出力先切替管理（コントロールタスク） ---
static void syslog_switch_to_bt_task(void *pv)
{
    ctl_msg_t m;
    for (;;) {
        if (xQueueReceive(qCtl, &m, portMAX_DELAY)) {
            if (m == CTL_TO_BT) {
                // 接続が本当に開いているか最終確認（最大2秒待つ）
                TickType_t waited = 0, step = pdMS_TO_TICKS(100);
                while (bt_handle == 0 && waited < pdMS_TO_TICKS(2000)) {
                    vTaskDelay(step);
                    waited += step;
                }
                if (bt_handle) syslog_set_output(LOG_OUTPUT_BLUETOOTH);
            } else { // CTL_TO_UART
                syslog_set_output(LOG_OUTPUT_UART);
            }
        }
    }
}

// --- SPP/BT 初期化（単一生成・順序保証） ---
// spp_init_task の先頭〜各段階に “printf” を直置き
static void spp_init_task(void *pv)
{
    printf("[SPP] spp_init_task enter\n");

    // ① Bluedroid ENABLE待ち（堅牢化）
    for (int i = 0; i < 50; ++i) {
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED &&
            esp_bluedroid_get_status()   == ESP_BLUEDROID_STATUS_ENABLED) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    printf("[SPP] ctrl=%d, bd=%d\n",
           esp_bt_controller_get_status(), esp_bluedroid_get_status());

    // ② GAP/PIN
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    esp_bt_pin_code_t pin = { '3','2','1','1' };
    ESP_ERROR_CHECK(esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin));
    printf("[SPP] GAP+PIN OK\n");

    // ③ SPP callback登録
    ESP_ERROR_CHECK(esp_spp_register_callback(bt_event_cb));
    printf("[SPP] spp_register_callback OK\n");

    // ④ ログキュー/タスク（重複なし）
    if (!logBtQueue) {
        logBtQueue = xQueueCreate(32, sizeof(char[LOG_MSG_LEN]));
        configASSERT(logBtQueue != NULL);
    }
    static TaskHandle_t h_btlog = NULL;
    if (!h_btlog) xTaskCreate(bt_log_task, "bt_log", 4096, NULL, 3, &h_btlog);
    printf("[SPP] bt_log_task ready\n");

    // ⑤ SPP初期化（通常版）
    printf("[SPP] esp_spp_init() call\n");
    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_CB));
    printf("[SPP] esp_spp_init() returned\n");  // ← ここが出るか要確認

    vTaskDelete(NULL);
}


// --- 公開：BT開始（生成はここだけ） ---
void start_bluetooth_task(void)
{
    qBtRx = xQueueCreate(64, sizeof(char));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    vTaskDelay(pdMS_TO_TICKS(1000));

    // SPP/ログ一式は spp_init_task 側で“単一生成”
    xTaskCreate(spp_init_task, "spp_init", 4096, NULL, 4, NULL);
}

// --- BTログ出力（単一タスク） ---
static void bt_log_task(void *pv)
{

    // logBtQueueがNULLのまま起動されたときの保険（最大2秒待ち）
    for (TickType_t waited = 0; logBtQueue == NULL && waited < pdMS_TO_TICKS(2000); waited += pdMS_TO_TICKS(50)) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    configASSERT(logBtQueue != NULL);  // ここで止まれば順序バグ
    
    char msg[LOG_MSG_LEN];
    for (;;) {
        if (xQueueReceive(logBtQueue, msg, portMAX_DELAY)) {
            msg[LOG_MSG_LEN - 1] = '\0';
            size_t n = strnlen(msg, LOG_MSG_LEN);
            if (!n) continue;

            uint32_t h = bt_handle;
            if (!h) {
                if (logQueue) xQueueSend(logQueue, msg, 0);
                continue;
            }

            esp_err_t er = esp_spp_write(h, n, (uint8_t*)msg);
            if (er != ESP_OK && logQueue) {
                xQueueSend(logQueue, msg, 0);
            }
        }
    }
}
