#include "m5_corep4_board.h"

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2c_register_bits.h"

// Register mappings/power and CST3530 flow derive from M5GFX tree
// 28d47949bdc84f30881623476f555398e2194313 and M5IOE1 1.0.9.
// Licenses: third_party/m5_corep4/LICENSE-M5GFX-MIT and LICENSE-LovyanGFX.
namespace {
constexpr const char *TAG = "m5_corep4_board";
i2c_master_bus_handle_t g_i2c;
i2c_master_dev_handle_t g_pmic, g_ioe, g_touch;
SemaphoreHandle_t g_lock;
bool g_attempted, g_ready;

esp_err_t update(i2c_master_dev_handle_t device, uint8_t reg, uint8_t mask, uint8_t value) {
    return update_i2c_register_bits(device, reg, mask, value, 100, TAG);
}

esp_err_t write16(uint8_t reg, uint16_t value) {
    const uint8_t bytes[] = {reg, static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(g_ioe, bytes, sizeof(bytes), 100), TAG, "IOE16 write");
    uint8_t actual[2]{};
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(g_ioe, &reg, 1, actual, sizeof(actual), 100), TAG, "IOE16 readback");
    return (actual[0] | static_cast<uint16_t>(actual[1]) << 8) == value ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t touch_command(uint32_t command) {
    const uint8_t bytes[] = {static_cast<uint8_t>(command >> 24), static_cast<uint8_t>(command >> 16),
        static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command)};
    return i2c_master_transmit(g_touch, bytes, sizeof(bytes), 100);
}

esp_err_t initialize() {
    g_lock = xSemaphoreCreateMutex();
    if (!g_lock) return ESP_ERR_NO_MEM;
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = GPIO_NUM_11; bus.scl_io_num = GPIO_NUM_9;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &g_i2c), TAG, "system I2C1");
    i2c_device_config_t device = {};
    device.dev_addr_length = I2C_ADDR_BIT_LEN_7; device.scl_speed_hz = 100000;
    device.device_address = 0x6E;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &device, &g_pmic), TAG, "PM1 known address");
    uint8_t reg = 0, id[2]{};
    esp_err_t result = i2c_master_transmit_receive(g_pmic, &reg, 1, id, sizeof(id), 100);
    if (result == ESP_ERR_INVALID_RESPONSE || result == ESP_ERR_NOT_FOUND) {
        // PM1 can transiently NACK while waking; retry this same address once.
        vTaskDelay(pdMS_TO_TICKS(10));
        result = i2c_master_transmit_receive(g_pmic, &reg, 1, id, sizeof(id), 100);
    }
    ESP_RETURN_ON_ERROR(result, TAG, "PM1 identity");
    ESP_RETURN_ON_FALSE((id[0] | static_cast<uint16_t>(id[1]) << 8) == 0x2050, ESP_ERR_INVALID_RESPONSE, TAG, "PM1 signature mismatch");
    ESP_RETURN_ON_ERROR(update(g_pmic, 0x06, 0x08, 0x08), TAG, "PM1 display supply");
    device.device_address = 0x4F;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &device, &g_ioe), TAG, "IOE1 known address");
    // M5IOE1_PIN_n is a one-based name: PIN_1=bit0, PIN_3=bit2,
    // PIN_8=bit7, PIN_9=bit8, PIN_10=bit9, PIN_11=bit10.
    // Do not treat the names in the owning config as register bit indices.
    // Stage PA low and PWM zero before enabling the related output pads.
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x05, 0x85, 0), TAG, "audio/PA/touch reset off latch");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x13, 0x85, 0), TAG, "audio/PA/touch push-pull");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x03, 0x85, 0x85), TAG, "audio/PA/touch outputs");
    ESP_RETURN_ON_ERROR(write16(0x1B, 0x8000), TAG, "PWM1 zero");
    ESP_RETURN_ON_ERROR(write16(0x25, 1000), TAG, "PWM1 frequency");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x06, 0x07, 0x01), TAG, "display reset/power latch");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x14, 0x07, 0), TAG, "display push-pull");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x04, 0x07, 0x07), TAG, "display outputs");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x06, 0x02, 0x02), TAG, "LCD supply on");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x05, 0x01, 0x01), TAG, "audio supply on");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x05, 0x80, 0x80), TAG, "touch reset release");
    ESP_RETURN_ON_ERROR(update(g_ioe, 0x06, 0x04, 0x04), TAG, "LCD reset release");
    vTaskDelay(pdMS_TO_TICKS(150));
    device.device_address = 0x58; device.scl_speed_hz = 400000;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &device, &g_touch), TAG, "CST3530 known address");
    ESP_RETURN_ON_ERROR(touch_command(0xD0000400), TAG, "CST first setup");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(touch_command(0xD0000400), TAG, "CST second setup");
    vTaskDelay(pdMS_TO_TICKS(20));
    constexpr uint32_t commands[] = {0xD0000000u, 0xD0000C00u, 0xD0000100u};
    for (uint32_t command : commands) {
        ESP_RETURN_ON_ERROR(touch_command(command), TAG, "CST setup");
    }
    return ESP_OK;
}
}

esp_err_t m5_corep4_board_init() {
    if (g_attempted) return g_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_attempted = true;
    const esp_err_t result = initialize();
    g_ready = result == ESP_OK;
    if (!g_ready) {
        const esp_err_t mute = g_ioe ? update(g_ioe, 0x05, 0x04, 0) : ESP_ERR_INVALID_STATE;
        const esp_err_t dark = g_ioe ? write16(0x1B, 0x8000) : ESP_ERR_INVALID_STATE;
        ESP_LOGE(TAG, "startup stopped:%s PA=%s BL=%s;restart required", esp_err_to_name(result), esp_err_to_name(mute), esp_err_to_name(dark));
    }
    return result;
}

esp_err_t m5_corep4_set_amplifier(bool enabled) {
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = update(g_ioe, 0x05, 0x04, enabled ? 0x04 : 0);
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t m5_corep4_brightness(int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
    const esp_err_t result = write16(0x1B, 0x8000 | static_cast<uint16_t>(percent * 4095 / 100));
    xSemaphoreGive(g_lock);
    return result;
}

esp_err_t m5_corep4_touch_read(uint16_t *x, uint16_t *y, bool *pressed) {
    if (!x || !y || !pressed) return ESP_ERR_INVALID_ARG;
    *pressed = false;
    if (!g_ready) return ESP_ERR_INVALID_STATE;
    const uint8_t address[] = {0xD0, 0x07, 0, 0};
    uint8_t report[50]{};
    esp_err_t result = i2c_master_transmit_receive(g_touch, address, sizeof(address), report, 9, 100);
    const unsigned fingers = report[3] & 0x0F, keys = report[3] >> 4, total = fingers + keys;
    if (result == ESP_OK && total > 9) result = ESP_ERR_INVALID_RESPONSE;
    if (result == ESP_OK && total > 1) result = i2c_master_receive(g_touch, report + 9, (total - 1) * 5, 100);
    const esp_err_t acknowledge = touch_command(0xD00002AB);
    if (result != ESP_OK) return result;
    if (acknowledge != ESP_OK) return acknowledge;
    if (!fingers && !keys) return ESP_OK;
    if (fingers != 1 || keys) return ESP_ERR_INVALID_RESPONSE;
    if (!(report[8] >> 4)) return ESP_OK;
    const uint16_t px = report[4] | static_cast<uint16_t>(report[7] & 0x0F) << 8;
    const uint16_t py = report[5] | static_cast<uint16_t>(report[7] & 0xF0) << 4;
    if (px >= 480 || py >= 480) return ESP_ERR_INVALID_RESPONSE;
    *x = px; *y = py; *pressed = true;
    return ESP_OK;
}

esp_err_t bsp_i2c_init() { return m5_corep4_board_init(); }
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }
