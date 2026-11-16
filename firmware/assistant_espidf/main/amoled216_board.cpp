#include "amoled216_board.h"
#include "amoled216_panel.h"
#if defined(ASSISTANT_BOARD_SH8601)
#include "i2c_register_bits.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#if defined(ASSISTANT_BOARD_AMOLED_132)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#elif defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_SH8601)
#include "esp_lcd_touch_ft5x06.h"
#else
#include "cst9217_polling.h"
#endif

#if BSP_CAPS_SDCARD
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#endif
#include "esp_check.h"
#include "esp_log.h"

namespace {
constexpr const char *kTag = "amoled216_board";
i2c_master_bus_handle_t g_bus;
#if defined(ASSISTANT_BOARD_AMOLED_132)
esp_lcd_panel_io_handle_t g_touch_io;
#elif defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_SH8601)
esp_lcd_panel_io_handle_t g_touch_io;
esp_lcd_touch_handle_t g_touch;
#else
Cst9217Polling g_touch;
#endif
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_bus_attempted, g_display_attempted, g_ready, g_wait_release = true, g_touch_failed;

#if defined(ASSISTANT_BOARD_SH8601)
esp_err_t reset_display_peripherals() {
    // Original1.8 board wiring: TCA9554 P0=LCD reset, P1=display power,
    // P2=touch reset. Preserve all other output latches and directions.
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = 0x20; config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t expander = nullptr;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_bus, &config, &expander), kTag, "TCA9554 device");
    constexpr uint8_t pins = 0x07;
    // Latch low before enabling outputs, including after a software restart.
    esp_err_t result = update_i2c_register_bits(expander, 0x01, pins, 0, 100, kTag);
    if (result == ESP_OK) result = update_i2c_register_bits(expander, 0x03, pins, 0, 100, kTag);
    // Match the pinned original board's HIGH100ms/LOW300ms pulse, then
    // allow150ms settling before the SH software reset and touch setup.
    if (result == ESP_OK) result = update_i2c_register_bits(expander, 0x01, pins, pins, 100, kTag);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(100));
    if (result == ESP_OK) result = update_i2c_register_bits(expander, 0x01, pins, 0, 100, kTag);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(300));
    if (result == ESP_OK) result = update_i2c_register_bits(expander, 0x01, pins, pins, 100, kTag);
    if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(150));
    const esp_err_t cleanup = i2c_master_bus_rm_device(expander);
    if (result == ESP_OK) result = cleanup;
    if (result == ESP_OK) ESP_LOGI(kTag, "TCA9554 reset mask=07 HIGH100ms LOW300ms HIGH150ms;register readback only");
    return result;
}
#endif

void cancel_press(lv_indev_t *pointer) {
    lv_indev_reset(pointer, nullptr);
    lv_indev_wait_release(pointer);
    g_wait_release = true;
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    bool pressed = false;
    uint16_t raw_x = 0, raw_y = 0;
#if defined(ASSISTANT_BOARD_AMOLED_132)
    uint8_t packet[5] = {};
    esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    const uint8_t count = packet[0];
    raw_x = ((packet[1] & 0x0f) << 8) | packet[2];
    raw_y = ((packet[3] & 0x0f) << 8) | packet[4];
    if (result == ESP_OK && count > 1) result = ESP_ERR_INVALID_RESPONSE;
    pressed = count == 1;
#elif defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_SH8601)
    uint8_t packet[7] = {};
    esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    const uint8_t count = packet[0] & 0x0f, event = packet[1] >> 6;
    raw_x = ((packet[1] & 0x0f) << 8) | packet[2];
    raw_y = ((packet[3] & 0x0f) << 8) | packet[4];
    if (result == ESP_OK && (count > 1 || (count == 1 && event == 3))) result = ESP_ERR_INVALID_RESPONSE;
    pressed = count == 1 && event != 1;
#else
    const esp_err_t result = g_touch.Read(&pressed, &raw_x, &raw_y);
#endif
    if (result != ESP_OK || (pressed && (raw_x >= amoled_board::width || raw_y >= amoled_board::height))) {
        if (!g_touch_failed) ESP_LOGE(kTag, "touch canceled error=%s raw=%u,%u", esp_err_to_name(result), raw_x, raw_y);
        g_touch_failed = true;
        cancel_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(kTag, "touch reports recovered;waiting for release");
    g_touch_failed = false;
    if (!pressed) { g_wait_release = false; return; }
    if (g_wait_release) return;
    // Reflect around the last valid pixel, not the pixel count.
#if defined(ASSISTANT_BOARD_SH8601) || defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_AMOLED_132)
    data->point.x = raw_x; data->point.y = raw_y;
#elif defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)
    data->point.x = 465 - raw_x; data->point.y = 465 - raw_y;
#else
    // The2.16 public touch layer mirrors Y before swapping XY.
    data->point.x = 479 - raw_y; data->point.y = raw_x;
#endif
    data->state = LV_INDEV_STATE_PRESSED;
}

void round_area(lv_event_t *event) {
    auto *area = static_cast<lv_area_t *>(lv_event_get_param(event));
    area->x1 = (area->x1 >> 1) << 1; area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1; area->y2 = ((area->y2 >> 1) << 1) + 1;
}
}

esp_err_t bsp_i2c_init() {
    if (g_bus_attempted) return g_bus ? ESP_OK : ESP_ERR_INVALID_STATE;
    g_bus_attempted = true;
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 0), kTag, "PA low latch");
    ESP_RETURN_ON_ERROR(gpio_hold_dis(GPIO_NUM_46), kTag, "release PA hold");
    gpio_config_t pa = {};
    pa.pin_bit_mask = 1ULL << GPIO_NUM_46; pa.mode = GPIO_MODE_INPUT_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&pa), kTag, "PA pad");
    ESP_RETURN_ON_ERROR(amoled216_set_amplifier(false), kTag, "PA quiet");
    i2c_master_bus_config_t bus = {};
    bus.i2c_port = I2C_NUM_1; bus.sda_io_num = spi_board::i2c_sda; bus.scl_io_num = spi_board::i2c_scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT; bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_bus);
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_bus; }

esp_err_t amoled216_set_amplifier(bool enabled) {
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, enabled ? 1 : 0), kTag, "PA level");
    return gpio_get_level(GPIO_NUM_46) == (enabled ? 1 : 0) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

lv_display_t *bsp_display_start() {
    if (g_display_attempted) return g_ready ? g_display : nullptr;
    g_display_attempted = true;
    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;
    esp_err_t result = bsp_i2c_init();
#if defined(ASSISTANT_BOARD_SH8601)
    if (result == ESP_OK) result = reset_display_peripherals();
#endif
    if (result == ESP_OK) result = amoled216_panel_create(&io, &panel);
#if defined(ASSISTANT_BOARD_AMOLED_132)
    if (result == ESP_OK) {
        const auto reset = amoled_board::touch_reset;
        result = gpio_set_level(reset, 1);
        gpio_config_t output = {};
        output.pin_bit_mask = 1ULL << reset; output.mode = GPIO_MODE_OUTPUT;
        if (result == ESP_OK) result = gpio_config(&output);
        if (result == ESP_OK) result = gpio_set_level(reset, 0);
        if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
        if (result == ESP_OK) result = gpio_set_level(reset, 1);
        if (result == ESP_OK) vTaskDelay(pdMS_TO_TICKS(200));
        gpio_config_t interrupt = {};
        interrupt.pin_bit_mask = 1ULL << GPIO_NUM_6; interrupt.mode = GPIO_MODE_INPUT;
        if (result == ESP_OK) result = gpio_config(&interrupt);
        esp_lcd_panel_io_i2c_config_t touch_io = {};
        touch_io.dev_addr = 0x15; touch_io.scl_speed_hz = 400000;
        touch_io.lcd_cmd_bits = 8; touch_io.lcd_param_bits = 8; touch_io.control_phase_bytes = 1;
        touch_io.flags.disable_control_phase = true; touch_io.transaction_timeout_ms = 100;
        if (result == ESP_OK) result = esp_lcd_new_panel_io_i2c(g_bus, &touch_io, &g_touch_io);
        uint8_t identity = 0;
        if (result == ESP_OK) result = esp_lcd_panel_io_rx_param(g_touch_io, 0xa7, &identity, 1);
        if (result == ESP_OK) ESP_LOGI(kTag, "CST820 observed ID=%02x;physical identity unconfirmed", identity);
    }
#elif defined(ASSISTANT_BOARD_AMOLED_206) || defined(ASSISTANT_BOARD_SH8601)
    if (result == ESP_OK) {
        esp_lcd_panel_io_i2c_config_t touch_io = {};
        touch_io.dev_addr = 0x38; touch_io.scl_speed_hz = 400000;
        touch_io.lcd_cmd_bits = 8; touch_io.lcd_param_bits = 8; touch_io.control_phase_bytes = 1;
        touch_io.flags.disable_control_phase = true; touch_io.transaction_timeout_ms = 100;
        result = esp_lcd_new_panel_io_i2c(g_bus, &touch_io, &g_touch_io);
        if (result == ESP_OK) {
            esp_lcd_touch_config_t touch = {};
            touch.x_max = amoled_board::width; touch.y_max = amoled_board::height;
            touch.rst_gpio_num = amoled_board::touch_reset;
#if defined(ASSISTANT_BOARD_SH8601)
            touch.int_gpio_num = GPIO_NUM_21;
#else
            touch.int_gpio_num = GPIO_NUM_38;
#endif
            result = esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch);
        }
    }
#else
    if (result == ESP_OK) result = g_touch.Init(g_bus, amoled_board::touch_reset, GPIO_NUM_11);
#endif
    if (result == ESP_OK) {
        lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
        port.task_affinity = 1; port.task_priority = 2;
        result = lvgl_port_init(&port);
    }
    if (result == ESP_OK) {
        if (!lvgl_port_lock(1000)) result = ESP_ERR_TIMEOUT;
        else {
            lvgl_port_display_cfg_t display = {};
            display.io_handle = io; display.panel_handle = panel;
            display.hres = amoled_board::width; display.vres = amoled_board::height;
            display.buffer_size = amoled_board::width * 20; display.double_buffer = true;
            display.color_format = LV_COLOR_FORMAT_RGB565;
#if defined(ASSISTANT_BOARD_AMOLED_216)
            // LVGL port reapplies these values at registration. Match A0
            // (MV|MY) from the panel table instead of clearing it to zero.
            display.rotation.swap_xy = true; display.rotation.mirror_y = true;
#endif
            display.flags.buff_dma = true; display.flags.swap_bytes = true;
            g_display = lvgl_port_add_disp(&display);
            if (g_display) {
                lv_display_add_event_cb(g_display, round_area, LV_EVENT_INVALIDATE_AREA, nullptr);
                g_pointer = lv_indev_create();
                if (g_pointer) {
                    lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
                    lv_indev_set_display(g_pointer, g_display);
                    lv_indev_set_read_cb(g_pointer, read_touch);
                }
            }
            result = g_display && g_pointer ? ESP_OK : ESP_ERR_NO_MEM;
            lvgl_port_unlock();
        }
    }
    if (result != ESP_OK) {
        const esp_err_t dark = amoled216_panel_set_brightness(0);
        const esp_err_t mute = amoled216_set_amplifier(false);
        ESP_LOGE(kTag, "display stopped error=%s dark=%s PA=%s;restart required", esp_err_to_name(result), esp_err_to_name(dark), esp_err_to_name(mute));
        // Preserve partial objects retained by panel or LVGL callbacks.
        return nullptr;
    }
    g_ready = true;
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) { return amoled216_panel_set_brightness(percent); }

#if BSP_CAPS_SDCARD
esp_err_t bsp_sdcard_mount() {
    static sdmmc_card_t *card;
    if (card) return ESP_OK;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_2; slot.cmd = GPIO_NUM_1; slot.d0 = GPIO_NUM_3; slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false; mount.max_files = 5; mount.allocation_unit_size = 16 * 1024;
    return esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot, &mount, &card);
}
#endif
