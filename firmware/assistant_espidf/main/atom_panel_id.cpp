#include "atom_s3r_board.h"

#include "driver/spi_master.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tft_board.h"

namespace { constexpr const char *TAG = "atom_panel_id"; }

esp_err_t atom_s3r_detect_panel(bool *gc9107) {
    if (!gc9107) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_cs, 1), TAG, "panel ID CS latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_reset, 0), TAG, "panel ID reset latch");
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_dc, 0), TAG, "panel ID command mode");
    gpio_config_t outputs = {};
    outputs.pin_bit_mask = (1ULL << spi_board::lcd_cs) | (1ULL << spi_board::lcd_reset) | (1ULL << spi_board::lcd_dc);
    outputs.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&outputs), TAG, "panel ID GPIOs");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_reset, 1), TAG, "panel ID release reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    spi_device_interface_config_t config = {};
    config.clock_speed_hz = 16000000;
    config.mode = 0;
    config.spics_io_num = GPIO_NUM_NC;
    config.queue_size = 1;
    config.command_bits = 8;
    config.dummy_bits = 1;
    config.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE;
    spi_device_handle_t device = nullptr;
    ESP_RETURN_ON_ERROR(spi_bus_add_device(SPI3_HOST, &config, &device), TAG, "panel ID SPI device");
    // Match the owning read: unselected zero clock, then RDDID04 with one
    // dummy bit and32-bit read on MOSI. The temporary device is always removed.
    spi_transaction_t wake = {};
    wake.length = 8;
    wake.flags = SPI_TRANS_USE_TXDATA;
    esp_err_t result = spi_device_polling_transmit(device, &wake);
    spi_transaction_t read = {};
    read.cmd = 0x04;
    read.rxlength = 32;
    read.flags = SPI_TRANS_USE_RXDATA;
    if (result == ESP_OK) result = gpio_set_level(spi_board::lcd_cs, 0);
    if (result == ESP_OK) result = spi_device_polling_transmit(device, &read);
    const esp_err_t deselect = gpio_set_level(spi_board::lcd_cs, 1);
    const esp_err_t cleanup = spi_bus_remove_device(device);
    if (result == ESP_OK) result = deselect;
    if (result == ESP_OK) result = cleanup;
    ESP_RETURN_ON_ERROR(result, TAG, "panel ID transfer/cleanup");
    const uint32_t identifier = static_cast<uint32_t>(read.rx_data[0]) | (static_cast<uint32_t>(read.rx_data[1]) << 8) |
        (static_cast<uint32_t>(read.rx_data[2]) << 16) | (static_cast<uint32_t>(read.rx_data[3]) << 24);
    if ((identifier & 0xffff) == 0x7683 || (identifier & 0xffff) == 0x897c) *gc9107 = false;
    else if ((identifier & 0xffffff) == 0x079100) *gc9107 = true;
    else { ESP_LOGE(TAG, "unknown_panel_id=0x%08lx; display initialization stopped", static_cast<unsigned long>(identifier)); return ESP_ERR_NOT_SUPPORTED; }
    ESP_LOGI(TAG, "panel_id=0x%08lx panel=%s", static_cast<unsigned long>(identifier), *gc9107 ? "GC9107" : "ST7735");
    return ESP_OK;
}
