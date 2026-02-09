
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef MR
#undef MR
#endif
#include "wizchip_port.h"
#include "wizchip_conf.h"

static const char *TAG = "wizport";

#define PIN_NUM_MOSI  23
#define PIN_NUM_MISO  19
#define PIN_NUM_SCK   18
#define PIN_NUM_CS     5   // ← ★ここで定義

static spi_device_handle_t spi_handle;

// ==== SPI選択・解除 ====
void wizchip_select(void)
{
    gpio_set_level(PIN_NUM_CS, 0);
}

void wizchip_deselect(void)
{
    gpio_set_level(PIN_NUM_CS, 1);
}

// ==== 割り込みマスク処理（ダミーでもOK） ====
void wizchip_critical_enter(void) {}
void wizchip_critical_exit(void)  {}


// ==== SPI初期化 ====
void wizchip_spi_init(void)
{
    // VSPI (SPI3_HOST) を使用
    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_NUM_MISO,
        .mosi_io_num = PIN_NUM_MOSI,
        .sclk_io_num = PIN_NUM_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %s", esp_err_to_name(ret));
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1 * 1000 * 1000,  // 1MHz
        .mode = 0,
        .spics_io_num = -1,                  // ← ★自前でCS制御するので無効
        .queue_size = 1,
    };
    ret = spi_bus_add_device(SPI3_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_NUM_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(PIN_NUM_CS, 1);

    gpio_pullup_en(GPIO_NUM_19); 

    ESP_LOGI(TAG, "W5500 SPI initialized (MOSI=%d, MISO=%d, SCK=%d, CS=%d)",
             PIN_NUM_MOSI, PIN_NUM_MISO, PIN_NUM_SCK, PIN_NUM_CS);
}

// ==== W5500リセット ====
void wizchip_hw_reset(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 26), // 例: GPIO26をRESETピンに接続していると仮定
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(26, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(26, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "W5500 hardware reset done");
}

static uint8_t spi_read_byte_impl(void)
{
    uint8_t tx = 0x00, rx = 0x00;
    spi_transaction_t t = { .length = 8, .tx_buffer = &tx, .rx_buffer = &rx };
    spi_device_transmit(spi_handle, &t);
    return rx;
}

static void spi_write_byte_impl(uint8_t wb)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &wb, .rx_buffer = NULL };
    spi_device_transmit(spi_handle, &t);
}



// ==== コールバック登録 ====
void wizchip_port_register(void)
{
    
    reg_wizchip_spi_cbfunc(spi_read_byte_impl, spi_write_byte_impl);
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_cris_cbfunc(wizchip_critical_enter, wizchip_critical_exit);

    ESP_LOGI(TAG, "WIZCHIP SPI callbacks registered");
}

// ==== W5500 レジスタ直接書き込み ====
// addrSel: 書き込み先レジスタアドレス (例: 0x0014 << 8 + ソケットオフセット)
// data:    書き込む1バイト
void wizchip_write(uint32_t addrSel, uint8_t data)
{
    wizchip_select();

    uint8_t addr[2];
    addr[0] = (addrSel >> 8) & 0xFF;
    addr[1] = addrSel & 0xFF;

    // W5500: [Addr1][Addr2][Control][Data]
    uint8_t ctrl = 0x04;  // VDMモード, 書き込み, 共通ブロック0x00の場合

    uint8_t tx_buf[3] = { addr[0], addr[1], ctrl };
    spi_transaction_t t = { .length = 24, .tx_buffer = tx_buf };
    spi_device_transmit(spi_handle, &t);

    spi_transaction_t d = { .length = 8, .tx_buffer = &data };
    spi_device_transmit(spi_handle, &d);

    wizchip_deselect();
}
