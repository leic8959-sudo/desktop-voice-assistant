#include "i80_panel_io.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_io_i80.h"
#include "esp_log.h"

esp_err_t create_i80_panel_io(const I80PanelIoConfig &config, esp_lcd_panel_io_handle_t *io) {
    constexpr const char *TAG = "i80_panel_io";
    if (!io) return ESP_ERR_INVALID_ARG;
    *io = nullptr;
    ESP_RETURN_ON_ERROR(gpio_set_level(static_cast<gpio_num_t>(config.read), 1), TAG, "LCD RD latch high");
    gpio_config_t read_line = {};
    read_line.pin_bit_mask = 1ULL << config.read;
    read_line.mode = GPIO_MODE_OUTPUT;
    read_line.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&read_line), TAG, "LCD RD output high");
    esp_lcd_i80_bus_config_t bus = {};
    bus.dc_gpio_num = config.command_data;
    bus.wr_gpio_num = config.write;
    bus.clk_src = LCD_CLK_SRC_DEFAULT;
    for (size_t index = 0; index < 8; ++index) bus.data_gpio_nums[index] = config.data[index];
    bus.bus_width = 8;
    bus.max_transfer_bytes = config.width * 20 * sizeof(uint16_t);
    bus.dma_burst_size = 64;
    esp_lcd_i80_bus_handle_t handle = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_i80_bus(&bus, &handle), TAG, "I80 bus");
    esp_lcd_panel_io_i80_config_t panel = {};
    panel.cs_gpio_num = config.select;
    panel.pclk_hz = config.clock_hz;
    panel.trans_queue_depth = config.queue_depth;
    panel.lcd_cmd_bits = 8;
    panel.lcd_param_bits = 8;
    panel.dc_levels.dc_idle_level = config.command_data_idle_high;
    panel.dc_levels.dc_data_level = 1;
    const esp_err_t result = esp_lcd_new_panel_io_i80(handle, &panel, io);
    if (result != ESP_OK) {
        const esp_err_t cleanup = esp_lcd_del_i80_bus(handle);
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "I80 cleanup_failed error=%s", esp_err_to_name(cleanup));
    }
    return result;
}
