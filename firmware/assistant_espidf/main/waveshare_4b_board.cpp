#include "waveshare_4b_board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_io_expander_tca9554.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "waveshare_4b";
constexpr uint32_t kAmplifier = IO_EXPANDER_PIN_NUM_3;
constexpr uint32_t kTouchReset = IO_EXPANDER_PIN_NUM_5;
constexpr uint32_t kTouchInterrupt = IO_EXPANDER_PIN_NUM_6;
constexpr uint32_t kLcdReset = IO_EXPANDER_PIN_NUM_7;
esp_io_expander_handle_t g_expander;
SemaphoreHandle_t g_lock;
bool g_ready;

esp_err_t set_checked_level(uint32_t pins, bool high) {
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(g_expander, pins, high), TAG, "TCA9554 level request");
    // get_level reads the physical input register, unlike the driver's
    // output/direction getters, which return software caches.
    uint32_t actual = 0;
    ESP_RETURN_ON_ERROR(esp_io_expander_get_level(g_expander, pins, &actual), TAG, "TCA9554 input readback");
    if (actual != (high ? pins : 0)) {
        ESP_LOGE(TAG, "level_mismatch pins=0x%02lx expected=%d actual=0x%02lx", static_cast<unsigned long>(pins), high, static_cast<unsigned long>(actual));
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
}

esp_err_t waveshare_4b_board_init(i2c_master_bus_handle_t bus) {
    if (g_ready) return ESP_OK;
    if (!bus) return ESP_ERR_INVALID_ARG;
    // BL4 is active low. Park it dark before display allocation or I2C work.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_4, 1), TAG, "backlight dark latch");
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << GPIO_NUM_4;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight dark output");
    if (!g_lock) g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    // Installed2.0.3 resets the entire port to directionFF/outputFF.
    // Preload PA/reset/INT low before enabling outputs; do not assert PA
    // merely because the owning board's startup code requests P3 high.
    ESP_RETURN_ON_ERROR(esp_io_expander_new_i2c_tca9554(bus,
        ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &g_expander), TAG, "TCA9554 constructor");
    uint32_t output = 0;
    ESP_RETURN_ON_ERROR(g_expander->read_output_reg(g_expander, &output), TAG, "TCA9554 output cache");
    ESP_RETURN_ON_ERROR(g_expander->write_output_reg(g_expander, output & ~(kAmplifier | kTouchReset | kTouchInterrupt)), TAG, "preload safe output latch");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(g_expander,
        kAmplifier | kTouchReset | kTouchInterrupt | kLcdReset, IO_EXPANDER_OUTPUT), TAG, "TCA9554 directions");
    ESP_RETURN_ON_ERROR(set_checked_level(kAmplifier | kTouchReset | kTouchInterrupt, false), TAG, "PA off/touch reset/INT low");
    // Schematic P7 is LCD_RST. Hold it high; the ST7701 startup sends
    // SWRESET120ms through P0/P2/P1 before its vendor command table.
    ESP_RETURN_ON_ERROR(set_checked_level(kLcdReset, true), TAG, "LCD reset released");
    vTaskDelay(pdMS_TO_TICKS(200));
    // P6 must be low when P5 rises to select GT9110x5D. Both pins are
    // behind TCA9554, not ESP GPIO6 (which is audio DOUT on this board).
    ESP_RETURN_ON_ERROR(set_checked_level(kTouchReset, true), TAG, "touch reset release");
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(g_expander,
        IO_EXPANDER_PIN_NUM_4 | kTouchInterrupt, IO_EXPANDER_INPUT), TAG, "release touch INT/leave SYS_OUT input");
    g_ready = true;
    ESP_LOGI(TAG, "TCA9554 whole-port reset completed; input_readback PAoff LCDresetHIGH touch5D; BLdark");
    return ESP_OK;
}

esp_io_expander_handle_t waveshare_4b_get_expander() { return g_ready ? g_expander : nullptr; }

esp_err_t waveshare_4b_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    // Display initialization and its three-wire IO deletion finish before
    // audio starts. Runtime PA updates share this lock; no other port writer.
    const esp_err_t result = set_checked_level(kAmplifier, enabled);
    xSemaphoreGive(g_lock);
    return result;
}
