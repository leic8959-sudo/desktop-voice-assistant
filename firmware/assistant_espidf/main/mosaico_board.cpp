#include "mosaico_board.h"

#include "esp_check.h"
#include "esp_efuse.h"
#include "esp_efuse_table.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *TAG = "mosaico_board";
bool g_attempted, g_ready, g_version_known, g_early;
i2c_master_bus_handle_t g_i2c;

esp_err_t configure_output(gpio_num_t pin, int value, gpio_mode_t mode = GPIO_MODE_OUTPUT) {
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, value), TAG, "output latch");
    gpio_config_t config = {};
    config.pin_bit_mask = 1ULL << pin; config.mode = mode;
    return gpio_config(&config);
}

esp_err_t initialize() {
    uint16_t version = 0;
    ESP_RETURN_ON_ERROR(esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, &version, 16), TAG, "read hardware revision");
    if (version != 0x0100 && version != 0x0101 && version != 0x0102) {
        ESP_LOGE(TAG, "unsupported eFuse hardware revision0x%04x;no pin fallback", version);
        return ESP_ERR_NOT_SUPPORTED;
    }
    g_version_known = true; g_early = version == 0x0100;
    // Detect before touching56/3: these are codec power/LED on v1.0 and I2C
    // on v1.1/v1.2. Unknown versions must never inherit one wiring revision.
    ESP_RETURN_ON_ERROR(configure_output(GPIO_NUM_45, 0, GPIO_MODE_INPUT_OUTPUT), TAG, "PA off with readback");
    ESP_RETURN_ON_ERROR(configure_output(GPIO_NUM_60, 0), TAG, "VCC active-low rail");
    if (g_early) ESP_RETURN_ON_ERROR(configure_output(GPIO_NUM_56, 1), TAG, "v1.0 codec supply");
    ESP_RETURN_ON_ERROR(configure_output(GPIO_NUM_57, 1, GPIO_MODE_OUTPUT_OD), TAG, "release shutdown open-drain");
    vTaskDelay(pdMS_TO_TICKS(10));
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_0;
    bus.sda_io_num = g_early ? GPIO_NUM_0 : GPIO_NUM_56;
    bus.scl_io_num = g_early ? GPIO_NUM_1 : GPIO_NUM_3;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7; bus.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_i2c), TAG, "revision-specific I2C0");
    ESP_LOGI(TAG, "revision=%u.%u I2C=%d/%d QSPIclock/reset=%d/%d;PAoff", version >> 8, version & 0xff,
        g_early ? 0 : 56, g_early ? 1 : 3, g_early ? 44 : 42, g_early ? 42 : 44);
    return ESP_OK;
}
}

esp_err_t mosaico_board_init() {
    if (g_attempted) return g_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_attempted = true;
    const esp_err_t result = initialize();
    g_ready = result == ESP_OK;
    if (!g_ready) {
        // A confirmed revision owns PA45. Preserve power/credentials on error.
        const esp_err_t mute = g_version_known ? gpio_set_level(GPIO_NUM_45, 0) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "startup stopped:%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(mute));
    }
    return result;
}
gpio_num_t mosaico_display_clock() { return g_ready ? (g_early ? GPIO_NUM_44 : GPIO_NUM_42) : GPIO_NUM_NC; }
gpio_num_t mosaico_display_reset() { return g_ready ? (g_early ? GPIO_NUM_42 : GPIO_NUM_44) : GPIO_NUM_NC; }
esp_err_t mosaico_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_45, enabled ? 1 : 0), TAG, "PA level");
    return gpio_get_level(GPIO_NUM_45) == (enabled ? 1 : 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
esp_err_t bsp_i2c_init() { return mosaico_board_init(); }
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }
