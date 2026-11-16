// SH8601 protocol facts adapted from Waveshare's Apache-2.0 driver.
// Original fixed source, license and changes are recorded in third_party/waveshare_sh8601/README.md.
#include "sh8601_rgb565.h"

#include <cstddef>
#include <new>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_interface.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char *kTag = "sh8601_rgb565";
struct Context {
    esp_lcd_panel_io_t bridge{};
    esp_lcd_panel_io_handle_t raw = nullptr;
    gpio_num_t reset = GPIO_NUM_NC;
    bool reset_active_high = false;
    uint8_t initial_madctl = 0;
    Sh8601Geometry geometry{};
    const Sh8601Command *commands = nullptr;
    size_t count = 0;
    esp_err_t (*sdk_del)(esp_lcd_panel_t *) = nullptr;
    esp_err_t (*sdk_draw)(esp_lcd_panel_t *, int, int, int, int, const void *) = nullptr;
    esp_err_t (*sdk_gap)(esp_lcd_panel_t *, int, int) = nullptr;
    esp_err_t (*sdk_mirror)(esp_lcd_panel_t *, bool, bool) = nullptr;
};
static_assert(offsetof(Context, bridge) == 0, "IO bridge must be the first context member");
Context *state(esp_lcd_panel_t *panel) { return static_cast<Context *>(panel->user_data); }
Context *state(esp_lcd_panel_io_t *io) { return reinterpret_cast<Context *>(io); }
int encoded(uint8_t opcode, int command) {
    return command == -1 ? -1 : static_cast<int>((static_cast<uint32_t>(opcode) << 24) | (static_cast<uint32_t>(command) << 8));
}
esp_err_t write_parameter(esp_lcd_panel_io_t *io, int command, const void *data, size_t bytes) {
    if (command < -1 || command > 255) return ESP_ERR_INVALID_ARG;
    return esp_lcd_panel_io_tx_param(state(io)->raw, encoded(0x02, command), data, bytes);
}
esp_err_t write_color(esp_lcd_panel_io_t *io, int command, const void *data, size_t bytes) {
    if (command < -1 || command > 255) return ESP_ERR_INVALID_ARG;
    return esp_lcd_panel_io_tx_color(state(io)->raw, encoded(0x32, command), data, bytes);
}

esp_err_t reset_panel(esp_lcd_panel_t *panel) {
    Context *context = state(panel);
    if (context->reset == GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(write_parameter(&context->bridge, 0x01, nullptr, 0), kTag, "software reset");
        vTaskDelay(pdMS_TO_TICKS(80));
    } else {
        ESP_RETURN_ON_ERROR(gpio_set_level(context->reset, context->reset_active_high ? 1 : 0), kTag, "reset active");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(context->reset, context->reset_active_high ? 0 : 1), kTag, "reset inactive");
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    return ESP_OK;
}

esp_err_t initialize_panel(esp_lcd_panel_t *panel) {
    Context *context = state(panel);
    const uint8_t format = 0x55;
    ESP_RETURN_ON_ERROR(write_parameter(&context->bridge, 0x36, &context->initial_madctl, 1), kTag, "MADCTL");
    ESP_RETURN_ON_ERROR(write_parameter(&context->bridge, 0x3a, &format, 1), kTag, "RGB565");
    for (size_t index = 0; index < context->count; ++index) {
        const auto &command = context->commands[index];
        ESP_RETURN_ON_ERROR(write_parameter(&context->bridge, command.code, command.data, command.length), kTag, "command%02x", command.code);
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}

esp_err_t draw_panel(esp_lcd_panel_t *panel, int x_start, int y_start, int x_end, int y_end, const void *pixels) {
    const auto geometry = state(panel)->geometry;
    if (!pixels || x_start < 0 || y_start < 0 || x_end <= x_start || y_end <= y_start ||
        x_end > geometry.width || y_end > geometry.height) return ESP_ERR_INVALID_ARG;
    return state(panel)->sdk_draw(panel, x_start, y_start, x_end, y_end, pixels);
}
esp_err_t set_gap(esp_lcd_panel_t *panel, int x, int y) {
    Context *context = state(panel);
    if (x < 0 || y < 0 || x > 65536 - context->geometry.width || y > 65536 - context->geometry.height) return ESP_ERR_INVALID_ARG;
    const esp_err_t result = context->sdk_gap(panel, x, y);
    if (result == ESP_OK) { context->geometry.x_gap = x; context->geometry.y_gap = y; }
    return result;
}
esp_err_t mirror_panel(esp_lcd_panel_t *panel, bool x, bool y) {
    if (y) return ESP_ERR_NOT_SUPPORTED;
    return state(panel)->sdk_mirror(panel, x, false);
}
esp_err_t swap_panel(esp_lcd_panel_t *, bool swapped) { return swapped ? ESP_ERR_NOT_SUPPORTED : ESP_OK; }
esp_err_t brightness_panel(esp_lcd_panel_t *panel, int percent) {
    if (percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
    const uint8_t value = percent * 255 / 100;
    return write_parameter(&state(panel)->bridge, 0x51, &value, 1);
}
esp_err_t delete_panel(esp_lcd_panel_t *panel) {
    Context *context = state(panel);
    const esp_err_t result = context->sdk_del(panel);
    const esp_err_t pin_result = context->reset == GPIO_NUM_NC ? ESP_OK : gpio_reset_pin(context->reset);
    delete context;
    return result == ESP_OK ? pin_result : result;
}
}

esp_err_t sh8601_rgb565_create(esp_lcd_panel_io_handle_t raw_io, const esp_lcd_panel_dev_config_t *config,
    Sh8601Geometry geometry, const Sh8601Command *commands, size_t count, esp_lcd_panel_handle_t *panel) {
    if (!panel) return ESP_ERR_INVALID_ARG;
    *panel = nullptr;
    if (!raw_io || !config || !commands || !count || count > 128 || !geometry.width || !geometry.height ||
        geometry.width > 1024 || geometry.height > 1024 || geometry.x_gap > 65536 - geometry.width ||
        geometry.y_gap > 65536 - geometry.height) return ESP_ERR_INVALID_ARG;
    if (config->bits_per_pixel != 16 || config->vendor_config ||
        (config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_RGB && config->rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR) ||
        (config->reset_gpio_num != GPIO_NUM_NC && !GPIO_IS_VALID_OUTPUT_GPIO(config->reset_gpio_num))) return ESP_ERR_NOT_SUPPORTED;
    const uint8_t madctl = config->rgb_ele_order == LCD_RGB_ELEMENT_ORDER_RGB ? 0 : 8;
    for (size_t index = 0; index < count; ++index) {
        const auto &command = commands[index];
        if (command.length > 16 || (command.length && !command.data) || command.delay_ms > 1000) return ESP_ERR_INVALID_ARG;
        if (command.code == 0x36 && (command.length != 1 || command.data[0] != madctl)) return ESP_ERR_NOT_SUPPORTED;
        if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x55)) return ESP_ERR_NOT_SUPPORTED;
    }
    Context *context = new (std::nothrow) Context{};
    if (!context) return ESP_ERR_NO_MEM;
    context->raw = raw_io; context->commands = commands; context->count = count; context->geometry = geometry;
    context->initial_madctl = madctl; context->reset = static_cast<gpio_num_t>(config->reset_gpio_num);
    context->reset_active_high = config->flags.reset_active_high;
    context->bridge.tx_param = write_parameter; context->bridge.tx_color = write_color;
    auto sdk_config = *config;
    sdk_config.reset_gpio_num = GPIO_NUM_NC;
    esp_lcd_panel_handle_t sdk_panel = nullptr;
    esp_err_t result = esp_lcd_new_panel_st7789(&context->bridge, &sdk_config, &sdk_panel);
    if (result != ESP_OK) { delete context; return result; }
    context->sdk_del = sdk_panel->del; context->sdk_draw = sdk_panel->draw_bitmap;
    context->sdk_gap = sdk_panel->set_gap; context->sdk_mirror = sdk_panel->mirror;
    sdk_panel->user_data = context;
    sdk_panel->del = delete_panel; sdk_panel->reset = reset_panel; sdk_panel->init = initialize_panel;
    sdk_panel->draw_bitmap = draw_panel; sdk_panel->set_gap = set_gap; sdk_panel->mirror = mirror_panel;
    sdk_panel->swap_xy = swap_panel; sdk_panel->set_brightness = brightness_panel;
    // The source SH8601 driver exposes no verified sleep implementation.
    sdk_panel->disp_sleep = nullptr;
    if (context->reset != GPIO_NUM_NC) {
        result = gpio_set_level(context->reset, context->reset_active_high ? 0 : 1);
        gpio_config_t reset = {};
        reset.pin_bit_mask = 1ULL << context->reset; reset.mode = GPIO_MODE_OUTPUT;
        if (result == ESP_OK) result = gpio_config(&reset);
    }
    if (result == ESP_OK) result = set_gap(sdk_panel, geometry.x_gap, geometry.y_gap);
    if (result != ESP_OK) { delete_panel(sdk_panel); return result; }
    *panel = sdk_panel;
    return ESP_OK;
}
