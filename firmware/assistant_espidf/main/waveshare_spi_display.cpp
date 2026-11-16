#include "board_profile.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#if defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_K10)
#include "ili9341_default_commands.h"
#endif
#if defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_K10)
#include "esp_lcd_panel_interface.h"
#endif
#if defined(ASSISTANT_BOARD_QUANDONG)
#include "quandong_panel_commands.h"
#endif
#if defined(ASSISTANT_BOARD_P4_SPI35)
#include "esp_lcd_st7796.h"
#endif
#if defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_CHD_LCD35) || defined(ASSISTANT_BOARD_WS_CAM_35)
#include "esp_lcd_panel_interface.h"
#include "st7796_default_commands.h"
#endif
#if defined(ASSISTANT_BOARD_WS_AUDIO)
#include "jd9853_panel.h"
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX2)
#include "atk_box2_panel_commands.h"
#endif
#if defined(ASSISTANT_BOARD_JIUCHUAN)
#include "jiuchuan_panel.h"
#endif
#if defined(ASSISTANT_BOARD_ELECTRON)
#include "gc9a01_default_commands.h"
#include "esp_lcd_panel_interface.h"
#elif defined(ASSISTANT_BOARD_SPOTPEAR_BOX)
#include "gc9a01_default_commands.h"
#include "esp_lcd_panel_interface.h"
#include "spotpear_panel_tuning.h"
#elif defined(ASSISTANT_BOARD_MOVECALL)
#include "gc9a01_default_commands.h"
#include "esp_lcd_panel_interface.h"
#elif defined(ASSISTANT_BOARD_MAGICLICK_25) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
#include "esp_lcd_panel_interface.h"
#elif defined(ASSISTANT_BOARD_SPOTPEAR_MUMA)
#include "spotpear_panel_tuning.h"
#endif
#if defined(ASSISTANT_BOARD_XINGZHI_085)
#include "esp_lcd_panel_interface.h"
#include "nv3023_board_init.h"
#endif
#if defined(ASSISTANT_BOARD_MAGICLICK_24)
#include "esp_lcd_panel_interface.h"
#include "nv3023_default_commands.h"
#endif
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
#include "esp_adc/adc_oneshot.h"
#include "magiclick_panel_commands.h"
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
#include "atom_s3r_panel_commands.h"
#endif
#if BSP_CAPS_TOUCH && !defined(ASSISTANT_BOARD_MVSRLORA)
#if defined(ASSISTANT_BOARD_CHD)
#include "esp_lcd_touch_ft5x06.h"
#include "gt911_polling.h"
#elif defined(ASSISTANT_BOARD_BIGSMART)
#include "gt911_polling.h"
#elif defined(ASSISTANT_BOARD_LICHUANG) || defined(ASSISTANT_BOARD_P4_SPI35)
#include "esp_lcd_touch_ft5x06.h"
#else
#include "esp_lcd_touch_cst816s.h"
#endif
#endif
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(ASSISTANT_BOARD_LCD_183)
#include "driver/sdmmc_host.h"
#include "esp_vfs_fat.h"
#endif

namespace {
constexpr const char *TAG = "waveshare_spi_display";
i2c_master_bus_handle_t g_i2c;
esp_lcd_panel_io_handle_t g_panel_io;
esp_lcd_panel_handle_t g_panel;
#if defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_CHD_LCD35) || defined(ASSISTANT_BOARD_WS_CAM_35)
esp_err_t reset_st7796_default_panel(esp_lcd_panel_t *) {
    if constexpr (spi_board::lcd_reset != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_reset, 0), TAG, "ST7796 reset assert");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_reset, 1), TAG, "ST7796 reset release");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x01, nullptr, 0), TAG, "ST7796 software reset");
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t initialize_st7796_default_panel(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "ST7796 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x05;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &axes, 1), TAG, "ST7796 BGR");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "ST7796 RGB565 format");
    for (const auto &command : ST7796_DEFAULT_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "ST7796 command 0x%02x", command.code);
        vTaskDelay(0);
    }
    return ESP_OK;
}
#endif
#if defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_MAGICLICK_24)
esp_err_t reset_nv3023_panel(esp_lcd_panel_t *) {
    if (spi_board::lcd_reset != GPIO_NUM_NC) {
        ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_reset, 0), TAG, "NV3023 reset assert");
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::lcd_reset, 1), TAG, "NV3023 reset release");
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x01, nullptr, 0), TAG, "NV3023 software reset");
    }
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t initialize_nv3023_panel(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "NV3023 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &axes, 1), TAG, "NV3023 initial BGR");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "NV3023 initial format");
#if defined(ASSISTANT_BOARD_XINGZHI_085)
    const auto &commands = XINGZHI_NV3023_INIT;
    static_assert([] {
        for (const auto &command : XINGZHI_NV3023_INIT) {
            if (command.length > sizeof(command.data) || command.code == 0x36) return false;
            if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x65)) return false;
        }
        return true;
    }(), "Xingzhi NV3023 must preserve the reviewed BGR axes and COLMOD65");
#else
    const auto &commands = NV3023_DEFAULT_COMMANDS;
#endif
    // Preserve each original board's one-time startup, including COLMOD05/65.
    // Both original adapters keep a16-bit framebuffer after that table write.
    for (const auto &command : commands) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "NV3023 command 0x%02x", command.code);
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}
#endif
#if defined(ASSISTANT_BOARD_ELECTRON) || defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL)
esp_err_t initialize_gc9a01_default_panel(esp_lcd_panel_t *) {
    // Preserve GC9A01 2.0.4's default sequence; do not call the SDK default init.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "GC9A01 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &axes, 1), TAG, "GC9A01 BGR");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "GC9A01 RGB565");
    for (const auto &command : GC9A01_DEFAULT_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "GC9A01 command 0x%02x", command.code);
        vTaskDelay(0);
    }
    return ESP_OK;
}
#endif
#if defined(ASSISTANT_BOARD_QUANDONG)
esp_err_t initialize_quandong_panel(esp_lcd_panel_t *) {
    // Preserve the ILI9341 2.1.0 preamble and this board's exact command list.
    // The SDK default ST7789 init would add an unrelated RAMCTRL command.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "Quandong sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t initial_axes = 0x00, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &initial_axes, 1), TAG, "Quandong initial axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "Quandong initial format");
    for (const auto &command : QUANDONG_PANEL_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "Quandong command 0x%02x", command.code);
        vTaskDelay(0); // The previous vendor adapter yielded after every zero-delay row.
    }
    return ESP_OK;
}
#endif
#if defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_K10)
esp_err_t initialize_ili9341_default_panel(esp_lcd_panel_t *) {
    // Both reviewed boards select BGR/RGB565 and the ILI9341 default table.
    // Use the SDK's DCS operations without its default RAMCTRL init write.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "ILI9341 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t axes = 0x08, format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &axes, 1), TAG, "ILI9341 BGR axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "ILI9341 RGB565");
    for (const auto &command : ILI9341_DEFAULT_COMMANDS) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "ILI9341 command 0x%02x", command.code);
        vTaskDelay(0);
    }
    return ESP_OK;
}
#endif
#if defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_K10)
esp_err_t sleep_ili9341_panel(esp_lcd_panel_t *, bool sleeping) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, sleeping ? 0x10 : 0x11, nullptr, 0), TAG, "ILI9341 sleep");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}
#endif
#if BSP_CAPS_TOUCH && !defined(ASSISTANT_BOARD_MVSRLORA)
esp_lcd_panel_io_handle_t g_touch_io;
#if defined(ASSISTANT_BOARD_BIGSMART)
Gt911Polling g_touch;
#else
esp_lcd_touch_handle_t g_touch;
#endif
#if defined(ASSISTANT_BOARD_SPOTPEAR)
i2c_master_bus_handle_t g_touch_i2c;
#endif
#endif
lv_display_t *g_display;
lv_indev_t *g_pointer;
bool g_display_started;
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
bool g_atom_s3r_gc9107, g_atom_s3r_mirror;
#endif
#if defined(ASSISTANT_BOARD_CHD)
bool g_chd_gt911, g_chd_gt911_ready;
Gt911Polling g_chd_touch;
bool chd_touch_present() { return g_touch || g_chd_gt911_ready; }
#endif
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
bool g_magiclick_gc9107;

esp_err_t detect_magiclick_panel() {
    adc_oneshot_unit_init_cfg_t unit = {};
    unit.unit_id = ADC_UNIT_1;
    adc_oneshot_unit_handle_t adc = nullptr;
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit, &adc), TAG, "Magiclick revision ADC");
    adc_oneshot_chan_cfg_t channel = {};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_12;
    esp_err_t result = adc_oneshot_config_channel(adc, ADC_CHANNEL_2, &channel);
    int total = 0;
    for (int index = 0; index < 10 && result == ESP_OK; ++index) {
        int raw = 0;
        result = adc_oneshot_read(adc, ADC_CHANNEL_2, &raw);
        if (result == ESP_OK) total += raw;
    }
    const esp_err_t cleanup = adc_oneshot_del_unit(adc);
    if (result == ESP_OK) result = cleanup;
    ESP_RETURN_ON_ERROR(result, TAG, "Magiclick revision read/cleanup");
    const int average = total / 10;
    // Exact raw equivalents of the fixed board's nominal3300/4095 conversion:
    // <100 selects2.5A, >3200 selects2.5A1. These are not calibrated mV.
    if (average > 124 && average < 3973) {
        ESP_LOGE(TAG, "Magiclick unknown_revision raw=%d; display initialization stopped", average);
        return ESP_ERR_NOT_SUPPORTED;
    }
    g_magiclick_gc9107 = average <= 124;
    ESP_LOGI(TAG, "Magiclick revision raw=%d panel=%s", average, g_magiclick_gc9107 ? "GC9107" : "ST7735");
    return ESP_OK;
}
#endif
#if defined(ASSISTANT_BOARD_MAGICLICK_25) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
esp_err_t initialize_gc9107_panel(esp_lcd_panel_t *) {
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x11, nullptr, 0), TAG, "GC9107 sleep out");
    vTaskDelay(pdMS_TO_TICKS(100));
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    const uint8_t axes = 0x08;
    const auto &commands = ATOM_S3R_GC9107_INIT;
#else
    const uint8_t axes = 0x00;
    const auto &commands = MAGICLICK_GC9107_INIT;
#endif
    const uint8_t format = 0x55;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x36, &axes, 1), TAG, "GC9107 initial axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &format, 1), TAG, "GC9107 initial format");
    // Match the reviewed one-time board startup. Magiclick's table then writes
    // COLMOD05; pixel buffers remain16-bit, as in the original GC9A01 adapter.
    for (const auto &command : commands) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length), TAG, "GC9107 command 0x%02x", command.code);
        vTaskDelay(pdMS_TO_TICKS(command.delay_ms));
    }
    return ESP_OK;
}
#endif
#if !defined(ASSISTANT_BOARD_ATK_DEV) && !defined(ASSISTANT_BOARD_ATK_BOX) && !defined(ASSISTANT_BOARD_ATK_BOX3) && !defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY) && !defined(ASSISTANT_BOARD_KORVO2) && !defined(ASSISTANT_BOARD_WS_CAM) && !defined(ASSISTANT_BOARD_K10)
bool g_pwm_ready;
#endif
#if BSP_CAPS_TOUCH
bool g_touch_failed;
#if defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_BIGSMART) || defined(ASSISTANT_BOARD_CHD)
bool g_wait_for_release = true;
#endif

void cancel_touch_press(lv_indev_t *pointer) {
    // LVGL9.6 sends INDEV_RESET to the pressed object without CLICKED. The
    // manual talk button handles that event to stop capture as well.
    lv_indev_reset(pointer, nullptr);
}

void read_touch(lv_indev_t *pointer, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
#if defined(ASSISTANT_BOARD_MVSRLORA)
    bool pressed = false;
    uint16_t x = 0, y = 0;
    const esp_err_t result = mvsrlora_read_touch(&pressed, &x, &y);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "CST226 report/sync invalid:%s", esp_err_to_name(result));
        g_touch_failed = true;
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "CST226 recovered;wait for release");
    g_touch_failed = false;
    if (!pressed) { g_wait_for_release = false; return; }
    if (x >= spi_board::width || y >= spi_board::height) {
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    // Match the display's mirrorX with zero-based endpoints. The49pixelRAM
    // window offset is not subtracted from native touch coordinates.
    data->point.x = spi_board::width - 1 - x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
#elif defined(ASSISTANT_BOARD_BIGSMART)
    Gt911Frame frame{};
    const esp_err_t result = g_touch.Read(&frame);
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "BigSmart touch read failed:%s", esp_err_to_name(result));
        g_touch_failed = true;
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "BigSmart touch recovered;wait for release");
    g_touch_failed = false;
    if (!frame.count) { g_wait_for_release = false; return; }
    if (frame.count != 1 || frame.points[0].x >= 240 || frame.points[0].y >= 320) {
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    // Keep the original portrait mirrorX, then swapXY with zero-based ends.
    data->point.x = frame.points[0].y;
    data->point.y = 239 - frame.points[0].x;
    data->state = LV_INDEV_STATE_PRESSED;
#elif defined(ASSISTANT_BOARD_CHD)
    static_assert(CONFIG_ESP_LCD_TOUCH_MAX_POINTS >= 2, "CHD FT touch must retain multi-contact detection");
    uint8_t count = 0;
    int32_t x = 0, y = 0;
    esp_err_t result;
    if (g_chd_gt911) {
        Gt911Frame frame{};
        result = g_chd_touch.Read(&frame);
        count = frame.count;
        if (count) { x = frame.points[0].x; y = frame.points[0].y; }
    } else {
        result = esp_lcd_touch_read_data(g_touch);
        esp_lcd_touch_point_data_t points[2]{};
        const esp_err_t coordinates = esp_lcd_touch_get_data(g_touch, points, &count, 2);
        if (result == ESP_OK) result = coordinates;
        if (count) { x = points[0].x; y = points[0].y; }
    }
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "CHD touch read failed:%s", esp_err_to_name(result));
        g_touch_failed = true; g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "CHD touch recovered;wait for release");
    g_touch_failed = false;
    if (!count) { g_wait_for_release = false; return; }
    if (count != 1) { g_wait_for_release = true; cancel_touch_press(pointer); return; }
    if (g_chd_gt911) {
        // Preserve the fixed board's rawX+80, then mirror/swap sequence.
        x += 80;
#if !defined(ASSISTANT_BOARD_CHD_LCD35)
        x = spi_board::width - 1 - x;
#endif
    } else {
        if (x >= spi_board::height || y >= spi_board::width) { g_wait_for_release = true; cancel_touch_press(pointer); return; }
#if defined(ASSISTANT_BOARD_CHD_LCD35)
        x = spi_board::height - 1 - x;
#endif
    }
    const int32_t display_x = y, display_y = x;
    if (display_x < 0 || display_y < 0 || display_x >= spi_board::width || display_y >= spi_board::height) {
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = display_x; data->point.y = display_y;
    data->state = LV_INDEV_STATE_PRESSED;
#elif defined(ASSISTANT_BOARD_TCAMERA)
    // Raw CST816 packet keeps multi-contact and event bits that the generic
    // coordinate API drops. Native240x240touchorigin stillneedsphysicalQA.
    uint8_t packet[6]{};
    const esp_err_t result = esp_lcd_panel_io_rx_param(g_touch_io, 0x02, packet, sizeof(packet));
    if (result != ESP_OK) {
        if (!g_touch_failed) ESP_LOGE(TAG, "T-CameraPlus touch read failed:%s", esp_err_to_name(result));
        g_touch_failed = true;
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "T-CameraPlus touch recovered;wait for release");
    g_touch_failed = false;
    const uint8_t count = packet[0] & 0x0f, event = packet[1] >> 6;
    const uint16_t x = static_cast<uint16_t>((packet[1] & 0x0f) << 8) | packet[2];
    const uint16_t y = static_cast<uint16_t>((packet[3] & 0x0f) << 8) | packet[4];
    if (!count || (count == 1 && event == 1)) { g_wait_for_release = false; return; }
    if (count != 1 || (event != 0 && event != 2) || x >= spi_board::width || y >= spi_board::height) {
        g_wait_for_release = true;
        cancel_touch_press(pointer);
        return;
    }
    if (g_wait_for_release) return;
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
#else
    const esp_err_t result = esp_lcd_touch_read_data(g_touch);
    if (result != ESP_OK) {
        cancel_touch_press(pointer);
        if (!g_touch_failed) ESP_LOGE(TAG, "touch_read failed: %s", esp_err_to_name(result));
        g_touch_failed = true;
        return;
    }
    if (g_touch_failed) ESP_LOGI(TAG, "touch_read recovered");
    g_touch_failed = false;
    esp_lcd_touch_point_data_t point = {};
    uint8_t count = 0;
    const esp_err_t coordinates = esp_lcd_touch_get_data(g_touch, &point, &count, 1);
    if (coordinates != ESP_OK) {
        cancel_touch_press(pointer);
        ESP_LOGE(TAG, "touch_data failed: %s", esp_err_to_name(coordinates));
        return;
    }
    if (!count) return;
#if defined(ASSISTANT_BOARD_PCA_BOX)
    // Owning touch axes are240x320, mirrorX then swapXY. Use zero-based
    // endpoints explicitly; the common touch helper mirrors with x_max-x.
    if (point.x >= 240 || point.y >= 320) { cancel_touch_press(pointer); return; }
    const uint16_t portrait_x = point.x;
    point.x = point.y;
    point.y = 239 - portrait_x;
#endif
    if (point.x >= spi_board::width || point.y >= spi_board::height) {
        cancel_touch_press(pointer);
        return;
    }
    data->point.x = point.x; data->point.y = point.y;
    data->state = LV_INDEV_STATE_PRESSED;
#endif
}
#endif

esp_err_t initialize_display() {
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
    ESP_RETURN_ON_ERROR(detect_magiclick_panel(), TAG, "Magiclick panel selection");
#endif
#if !defined(ASSISTANT_BOARD_TFT_DIGITAL)
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "display I2C");
#endif
#if defined(ASSISTANT_BOARD_FREENOVE)
    ESP_RETURN_ON_ERROR(freenove_keys_init(), TAG, "Freenove touch key");
#endif
#if defined(ASSISTANT_BOARD_XINGZHI_METAL)
    ESP_RETURN_ON_ERROR(metal_keys_init(), TAG, "discrete touch keys");
#endif
#if !defined(ASSISTANT_BOARD_ATK_DEV) && !defined(ASSISTANT_BOARD_ATK_BOX) && !defined(ASSISTANT_BOARD_ATK_BOX3) && !defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY) && !defined(ASSISTANT_BOARD_KORVO2) && !defined(ASSISTANT_BOARD_WS_CAM) && !defined(ASSISTANT_BOARD_K10) && !defined(ASSISTANT_BOARD_SPARKBOT)
    ledc_timer_config_t timer = {};
    timer.speed_mode = LEDC_LOW_SPEED_MODE;
    timer.duty_resolution = LEDC_TIMER_10_BIT;
    timer.timer_num = LEDC_TIMER_0;
    timer.freq_hz = 5000;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer");
    ledc_channel_config_t backlight = {};
    backlight.gpio_num = spi_board::backlight;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_0;
    backlight.timer_sel = LEDC_TIMER_0;
#if defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_MAGICLICK_25) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
#if defined(ASSISTANT_BOARD_P4_SPI35)
    backlight.duty = 0;
#else
    backlight.duty = 1023;
#endif
#endif
    ESP_RETURN_ON_ERROR(ledc_channel_config(&backlight), TAG, "backlight channel");
    g_pwm_ready = true;
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX2)
    ESP_RETURN_ON_ERROR(create_i80_panel_io(spi_board::panel_io, &g_panel_io), TAG, "ATK I80 IO");
#else
    spi_bus_config_t bus = {};
    bus.mosi_io_num = spi_board::lcd_mosi;
#if defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_FREENOVE)
    bus.miso_io_num = spi_board::lcd_miso;
#else
    bus.miso_io_num = GPIO_NUM_NC;
#endif
    bus.sclk_io_num = spi_board::lcd_clock;
    bus.quadwp_io_num = GPIO_NUM_NC;
    bus.quadhd_io_num = GPIO_NUM_NC;
    bus.max_transfer_sz = spi_board::width * 20 * sizeof(uint16_t);
#if defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_P4_SPI35)
    constexpr spi_host_device_t display_host = SPI2_HOST;
#else
    constexpr spi_host_device_t display_host = SPI3_HOST;
#endif
    ESP_RETURN_ON_ERROR(spi_bus_initialize(display_host, &bus, SPI_DMA_CH_AUTO), TAG, "display SPI");
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    ESP_RETURN_ON_ERROR(atom_s3r_detect_panel(&g_atom_s3r_gc9107), TAG, "AtomS3R panel selection");
    g_atom_s3r_mirror = g_atom_s3r_gc9107 ? spi_board::gc9107_mirror : !spi_board::gc9107_mirror;
#endif
    esp_lcd_panel_io_spi_config_t panel_io = {};
    panel_io.cs_gpio_num = spi_board::lcd_cs;
    panel_io.dc_gpio_num = spi_board::lcd_dc;
    panel_io.spi_mode = spi_board::spi_mode;
    panel_io.pclk_hz = spi_board::spi_clock_hz;
    panel_io.trans_queue_depth = 10;
#if defined(ASSISTANT_BOARD_LABPLUS)
    panel_io.trans_queue_depth = 7;
#endif
    panel_io.lcd_cmd_bits = 8;
    panel_io.lcd_param_bits = 8;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(display_host, &panel_io, &g_panel_io), TAG, "panel IO");
#endif
    esp_lcd_panel_dev_config_t panel = {};
    panel.reset_gpio_num = spi_board::lcd_reset;
#if defined(ASSISTANT_BOARD_MAGICLICK_25)
    panel.rgb_ele_order = g_magiclick_gc9107 ? LCD_RGB_ELEMENT_ORDER_RGB : LCD_RGB_ELEMENT_ORDER_BGR;
#elif defined(ASSISTANT_BOARD_LCD_085) || defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_MAGICLICK_24) || defined(ASSISTANT_BOARD_CHD_LCD35) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY) || defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_WS_CAM_35) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_ELECTRON) || defined(ASSISTANT_BOARD_P4_SPI35)
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
#else
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
#endif
    panel.bits_per_pixel = 16;
#if defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX0) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_LABPLUS)
    panel.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    if (g_atom_s3r_gc9107) {
        static_assert([] {
            for (const auto &command : ATOM_S3R_GC9107_INIT) {
                if (command.length > sizeof(command.data) || command.code == 0x36 || command.code == 0x3a) return false;
            }
            return true;
        }(), "AtomS3R GC9107 table must preserve BGR/RGB565 cached state");
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "AtomS3R GC9107 DCS operations");
        g_panel->init = initialize_gc9107_panel;
        g_panel->disp_sleep = nullptr;
        g_panel->set_brightness = nullptr;
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "AtomS3R ST7735 compatible panel");
    }
#elif defined(ASSISTANT_BOARD_QUANDONG)
    // The board table overrides the original RGB constructor to BGR. Seed the
    // SDK MADCTL cache with that final state; the init callback keeps the old
    // on-wire RGB preamble. Drawing stays RGB565 with the existing byte swap.
    static_assert([] {
        for (const auto &command : QUANDONG_PANEL_COMMANDS) {
            if (command.code == 0x36 && (command.length != 1 || command.data[0] != 0x08)) return false;
            if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x55)) return false;
        }
        return QUANDONG_PANEL_COMMANDS[10].code == 0x36;
    }(), "Quandong table must match the SDK BGR/RGB565 cached state");
    panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "Quandong DCS operations");
    g_panel->init = initialize_quandong_panel;
    g_panel->disp_sleep = sleep_ili9341_panel;
    g_panel->set_brightness = nullptr; // The board owns its GPIO45 PWM backlight.
#elif defined(ASSISTANT_BOARD_FREENOVE) || defined(ASSISTANT_BOARD_K10)
    if (panel.rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR || panel.bits_per_pixel != 16 || panel.vendor_config) return ESP_ERR_NOT_SUPPORTED;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "ILI9341 DCS operations");
    g_panel->init = initialize_ili9341_default_panel;
    g_panel->disp_sleep = sleep_ili9341_panel;
    g_panel->set_brightness = nullptr; // PWM/expander backlight remains board-owned.
#elif defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_CHD_LCD35) || defined(ASSISTANT_BOARD_WS_CAM_35)
    if (panel.rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR || panel.bits_per_pixel != 16 || panel.vendor_config || panel.flags.reset_active_high) return ESP_ERR_NOT_SUPPORTED;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "ST7796 default DCS operations");
    g_panel->reset = reset_st7796_default_panel;
    g_panel->init = initialize_st7796_default_panel;
    g_panel->disp_sleep = nullptr;
    g_panel->set_brightness = nullptr;
#elif defined(ASSISTANT_BOARD_P4_SPI35)
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7796(g_panel_io, &panel, &g_panel), TAG, "CHD ST7796");
#elif defined(ASSISTANT_BOARD_MAGICLICK_24)
    if (panel.rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR || panel.bits_per_pixel != 16 || panel.vendor_config || panel.flags.reset_active_high) return ESP_ERR_NOT_SUPPORTED;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "Magiclick NV3023 DCS operations");
    g_panel->reset = reset_nv3023_panel;
    g_panel->init = initialize_nv3023_panel;
    g_panel->disp_sleep = nullptr;
    g_panel->set_brightness = nullptr;
#elif defined(ASSISTANT_BOARD_MAGICLICK_25)
    if (g_magiclick_gc9107) {
        static_assert([] {
            for (const auto &command : MAGICLICK_GC9107_INIT) {
                if (command.length > sizeof(command.data) || command.code == 0x36) return false;
                if (command.code == 0x3a && (command.length != 1 || command.data[0] != 0x05)) return false;
            }
            return true;
        }(), "Magiclick GC9107 table must preserve RGB axes and its COLMOD05 override");
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "Magiclick GC9107 DCS operations");
        g_panel->init = initialize_gc9107_panel;
        g_panel->disp_sleep = nullptr;
        g_panel->set_brightness = nullptr;
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "Magiclick ST7735 compatible panel");
    }
#elif defined(ASSISTANT_BOARD_WS_AUDIO)
    ESP_RETURN_ON_ERROR(jd9853_panel_create(g_panel_io, &panel, &g_panel), TAG, "JD9853");
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
    ESP_RETURN_ON_ERROR(jiuchuan_panel_create(g_panel_io, &panel, &g_panel), TAG, "Jiuchuan 240x296 panel");
#elif defined(ASSISTANT_BOARD_XINGZHI_085)
    if (panel.rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR || panel.bits_per_pixel != 16 || panel.vendor_config || panel.flags.reset_active_high) return ESP_ERR_NOT_SUPPORTED;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "Xingzhi NV3023 DCS operations");
    g_panel->reset = reset_nv3023_panel;
    g_panel->init = initialize_nv3023_panel;
    g_panel->disp_sleep = nullptr;
    g_panel->set_brightness = nullptr;
#elif defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_ELECTRON)
    if (panel.rgb_ele_order != LCD_RGB_ELEMENT_ORDER_BGR || panel.bits_per_pixel != 16 || panel.vendor_config) return ESP_ERR_NOT_SUPPORTED;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "GC9A01 DCS operations");
    g_panel->init = initialize_gc9a01_default_panel;
    g_panel->disp_sleep = nullptr; // The original GC9A01 factory has no sleep callback.
    g_panel->set_brightness = nullptr; // Preserve board-owned backlight control.
#else
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(g_panel_io, &panel, &g_panel), TAG, "ST7789");
#endif
#if defined(ASSISTANT_BOARD_LEDONG)
    // STM8 accepts a single command byte, not a register/value pair. This is
    // LCD reset only; no motor, camera or other STM8 command is sent.
    i2c_device_config_t reset_config = {};
    reset_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    reset_config.device_address = 0x11;
    reset_config.scl_speed_hz = 400000;
    i2c_master_dev_handle_t reset_device = nullptr;
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(g_i2c, &reset_config, &reset_device), TAG, "Ledong STM8 LCD reset device");
    const uint8_t reset_command = 0x04;
    esp_err_t reset_result = i2c_master_transmit(reset_device, &reset_command, 1, 100);
    const esp_err_t reset_cleanup = i2c_master_bus_rm_device(reset_device);
    if (reset_result == ESP_OK) reset_result = reset_cleanup;
    ESP_RETURN_ON_ERROR(reset_result, TAG, "Ledong STM8 LCD reset/cleanup");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(g_panel), TAG, "panel reset");
#if defined(ASSISTANT_BOARD_LABPLUS)
    vTaskDelay(pdMS_TO_TICKS(100));
#endif
#if defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    ESP_RETURN_ON_ERROR(pca9557_box_set_panel_control(false), TAG, "PCA9557 LCD control low");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(g_panel), TAG, "panel init");
#if defined(ASSISTANT_BOARD_ATK_BOX)
    // Owning parallel panel sets RGB565 COLMOD0x65 after standard init.
    const uint8_t pixel_format = 0x65;
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(g_panel_io, 0x3a, &pixel_format, 1), TAG, "I80 RGB565 pixel format");
#endif
#if defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, !g_atom_s3r_gc9107), TAG, "AtomS3R panel invert");
#elif defined(ASSISTANT_BOARD_MAGICLICK_25)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, !g_magiclick_gc9107), TAG, "Magiclick panel invert");
#elif defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_MAGICLICK_24) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_K10) || (defined(ASSISTANT_BOARD_CHD) && !defined(ASSISTANT_BOARD_CHD_LCD35))
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, false), TAG, "panel invert");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(g_panel, true), TAG, "panel invert");
#endif
#if defined(ASSISTANT_BOARD_ATK_BOX2)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "BOX2 initial gap");
    for (const auto &command : ATK_BOX2_PANEL_COMMANDS) {
        const esp_err_t result = esp_lcd_panel_io_tx_param(g_panel_io, command.code, command.data, command.length);
        if (result != ESP_OK) {
            ESP_LOGE(TAG, "BOX2 panel_command_failed command=0x%02x error=%s", command.code, esp_err_to_name(result));
            return result;
        }
    }
#endif
#if defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "panel gap");
#elif defined(ASSISTANT_BOARD_FREENOVE)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "Freenove panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "Freenove panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "Freenove panel gap");
#elif defined(ASSISTANT_BOARD_CHD)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "CHD panel axes");
#if defined(ASSISTANT_BOARD_CHD_LCD35)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "CHD3.5 mirror");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "CHD2.8 mirror");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "CHD panel gap");
#elif defined(ASSISTANT_BOARD_LABPLUS)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "Labplus panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, true), TAG, "Labplus panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 34), TAG, "Labplus visible window");
#elif defined(ASSISTANT_BOARD_MINSI)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "K08 panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), TAG, "K08 panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 80, 0), TAG, "K08 visible window");
#elif defined(ASSISTANT_BOARD_KORVO2)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "Korvo2 panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), TAG, "Korvo2 panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 20, 0), TAG, "Korvo2 visible window");
#elif defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_QUANDONG)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, true), TAG, "panel axes");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), TAG, "panel mirror");
#if defined(ASSISTANT_BOARD_WS_AUDIO)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 34), TAG, "JD9853 visible window");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "panel gap");
#endif
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(g_panel, false), TAG, "panel axes");
#if defined(ASSISTANT_BOARD_ELECTRON)
    // Sourceconstructor setsmirrorX thenSpiLcdDisplay reappliesconfigmirrorY.
    // Keep the finalconfigorientation consistentlyinpanelandLVGLport.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), TAG, "electronBot finalmirrorY");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "electronBot visiblewindow");
#elif defined(ASSISTANT_BOARD_K10)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), TAG, "K10mirrorY");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "K10visiblewindow");
#elif defined(ASSISTANT_BOARD_MVSRLORA)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "MVSRLoRa mirrorX");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 49, 0), TAG, "MVSRLoRa222column window");
#elif defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, g_atom_s3r_mirror, g_atom_s3r_mirror), TAG, "AtomS3R panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, g_atom_s3r_gc9107 ? 0 : 2, g_atom_s3r_gc9107 ? spi_board::gc9107_gap_y : 3), TAG, "AtomS3R panel gap");
#elif defined(ASSISTANT_BOARD_MAGICLICK_25)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, !g_magiclick_gc9107, !g_magiclick_gc9107), TAG, "Magiclick panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, g_magiclick_gc9107 ? 0 : 2, g_magiclick_gc9107 ? 0 : 3), TAG, "Magiclick panel gap");
#elif defined(ASSISTANT_BOARD_LCD_085) || defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_MIXGO)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, true), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 2, 1), TAG, "panel gap");
#elif defined(ASSISTANT_BOARD_WS_CAM_35) || defined(ASSISTANT_BOARD_P4_SPI35)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "CAM3.5 mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "CAM3.5 gap");
#elif defined(ASSISTANT_BOARD_STICK_S3)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "StickS3 panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 52, 40), TAG, "StickS3 visible window");
#elif defined(ASSISTANT_BOARD_KEVIN_SPV4)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 20), TAG, "panel gap");
#elif defined(ASSISTANT_BOARD_JIUCHUAN)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "panel gap");
#elif defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_MAGICLICK_24)
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, true), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "panel gap");
#elif defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL)
    // BGR + MX yields MADCTL0x48 through the driver, keeping its cached value
    // consistent when LVGL reapplies rotation later.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, true, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "panel gap");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(g_panel, false, false), TAG, "panel mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(g_panel, 0, 0), TAG, "panel gap");
#endif
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(g_panel, true), TAG, "panel on");
#if defined(ASSISTANT_BOARD_SPARKBOT)
    // Do not attach LEDC46:brightnessPWM would also modulate the amplifier.
    // Both pinnedboard and manufacturer examples share this enable line.
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_46, 1), TAG, "SparkBot sharedBL/PA enable");
#endif
#if defined(ASSISTANT_BOARD_SPOTPEAR)
    ESP_RETURN_ON_ERROR(spotpear_panel_tune(g_panel_io), TAG, "Spotpear panel tuning");
#endif
#if BSP_CAPS_TOUCH
#if defined(ASSISTANT_BOARD_MVSRLORA)
    ESP_RETURN_ON_ERROR(mvsrlora_touch_init(g_i2c), TAG, "CST226SE polling init");
#else
    i2c_master_bus_handle_t touch_bus = g_i2c;
#if defined(ASSISTANT_BOARD_CHD)
    int touch_address = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    esp_err_t probe = i2c_master_probe(touch_bus, touch_address, 100);
    if (probe == ESP_ERR_NOT_FOUND) {
        g_chd_gt911 = true;
        touch_address = 0x5d;
        probe = i2c_master_probe(touch_bus, touch_address, 100);
        if (probe == ESP_ERR_NOT_FOUND) {
            touch_address = 0x14;
            probe = i2c_master_probe(touch_bus, touch_address, 100);
        }
    }
    if (probe == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "CHD optional touch absent; BOOT cards remain available");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(probe, TAG, "CHD optional touch probe");
    ESP_LOGI(TAG, "CHD touch controller=%s address=0x%02x", g_chd_gt911 ? "GT911" : "FT6336", touch_address);
#endif
#if defined(ASSISTANT_BOARD_SPOTPEAR)
    gpio_config_t reset = {};
    reset.pin_bit_mask = 1ULL << spi_board::touch_reset;
    reset.mode = GPIO_MODE_OUTPUT;
    ESP_RETURN_ON_ERROR(gpio_config(&reset), TAG, "touch reset GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::touch_reset, 0), TAG, "touch reset low");
    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_RETURN_ON_ERROR(gpio_set_level(spi_board::touch_reset, 1), TAG, "touch reset high");
    vTaskDelay(pdMS_TO_TICKS(50));
    i2c_master_bus_config_t touch_config = {};
    touch_config.i2c_port = I2C_NUM_1;
    touch_config.sda_io_num = GPIO_NUM_11;
    touch_config.scl_io_num = GPIO_NUM_7;
    touch_config.clk_source = I2C_CLK_SRC_DEFAULT;
    touch_config.glitch_ignore_cnt = 7;
    touch_config.flags.enable_internal_pullup = true;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&touch_config, &g_touch_i2c), TAG, "touch I2C1");
    const esp_err_t probe = i2c_master_probe(g_touch_i2c, ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS, 100);
    if (probe == ESP_ERR_NOT_FOUND) {
        ESP_RETURN_ON_ERROR(i2c_del_master_bus(g_touch_i2c), TAG, "release unused touch I2C1");
        g_touch_i2c = nullptr;
        ESP_LOGI(TAG, "optional touch absent; BOOT input remains available");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(probe, TAG, "optional touch probe");
    touch_bus = g_touch_i2c;
#endif
    // The component macro's C designated-initializer order is not C++ compatible
    // with this SDK. Assign the same public I2C fields explicitly.
    esp_lcd_panel_io_i2c_config_t touch_io = {};
#if defined(ASSISTANT_BOARD_CHD)
    touch_io.dev_addr = touch_address;
#elif defined(ASSISTANT_BOARD_BIGSMART)
    touch_io.dev_addr = 0x5d; // Fixed owning-board default; no borrowed reset/INT GPIOs.
#elif defined(ASSISTANT_BOARD_LICHUANG) || defined(ASSISTANT_BOARD_P4_SPI35)
    touch_io.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
#else
    touch_io.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
#endif
#if defined(ASSISTANT_BOARD_CHD)
    touch_io.scl_speed_hz = 400000;
#else
    touch_io.scl_speed_hz = spi_board::touch_clock_hz;
#endif
    touch_io.control_phase_bytes = 1;
#if defined(ASSISTANT_BOARD_CHD)
    touch_io.lcd_cmd_bits = g_chd_gt911 ? 16 : 8;
#elif defined(ASSISTANT_BOARD_BIGSMART)
    touch_io.lcd_cmd_bits = 16;
#else
    touch_io.lcd_cmd_bits = 8;
#endif
    touch_io.flags.disable_control_phase = true;
#if defined(ASSISTANT_BOARD_TCAMERA) || defined(ASSISTANT_BOARD_BIGSMART) || defined(ASSISTANT_BOARD_CHD)
    touch_io.transaction_timeout_ms = 100;
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(touch_bus, &touch_io, &g_touch_io), TAG, "touch IO");
#if defined(ASSISTANT_BOARD_BIGSMART)
    static_assert(spi_board::touch_reset == GPIO_NUM_NC && spi_board::touch_interrupt == GPIO_NUM_NC,
        "BigSmart polling requires the reviewed default-address path without reset/INT GPIOs");
    ESP_RETURN_ON_ERROR(g_touch.Init(g_touch_io), TAG, "BigSmart GT911 default0x5D polling");
#else
    esp_lcd_touch_config_t touch = {};
    touch.x_max = spi_board::width;
    touch.y_max = spi_board::height;
#if defined(ASSISTANT_BOARD_CHD)
    // Read raw coordinates. read_touch applies the owning controller-specific
    // landscape transform explicitly, without the common helper's x_max-x.
    touch.rst_gpio_num = GPIO_NUM_NC;
    touch.int_gpio_num = GPIO_NUM_NC;
    touch.x_max = g_chd_gt911 ? spi_board::width : spi_board::height;
    touch.y_max = g_chd_gt911 ? spi_board::height : spi_board::width;
    if (g_chd_gt911) {
        ESP_RETURN_ON_ERROR(g_chd_touch.Init(g_touch_io), TAG, "CHD GT911 selected-address polling");
        g_chd_gt911_ready = true;
    } else {
        ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch), TAG, "CHD FT6336");
    }
#else
    touch.rst_gpio_num = spi_board::touch_reset;
    touch.int_gpio_num = spi_board::touch_interrupt;
#if defined(ASSISTANT_BOARD_P4_SPI35)
    touch.flags.swap_xy = true;
    touch.flags.mirror_x = true;
    touch.flags.mirror_y = false;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch), TAG, "P4 3.5 FT5x06");
#elif defined(ASSISTANT_BOARD_PCA_BOX)
    touch.x_max = 240;
    touch.y_max = 320;
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_ft5x06(g_touch_io, &touch, &g_touch), TAG, "FT5x06");
#else
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(g_touch_io, &touch, &g_touch), TAG, "CST816S");
#endif
#endif
#endif
#endif
#endif
    return ESP_OK;
}
}

esp_err_t bsp_i2c_init() {
#if defined(ASSISTANT_BOARD_TFT_DIGITAL)
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (g_i2c) return ESP_OK;
    i2c_master_bus_config_t bus = {};
#if defined(ASSISTANT_BOARD_KEVIN_SPV4) || defined(ASSISTANT_BOARD_JIUCHUAN) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_CHD) || defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY) || defined(ASSISTANT_BOARD_CHD_EYE) || defined(ASSISTANT_BOARD_KORVO2) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_P4_SPI35)
    bus.i2c_port = I2C_NUM_1;
#else
    bus.i2c_port = I2C_NUM_0;
#endif
    bus.sda_io_num = spi_board::i2c_sda;
    bus.scl_io_num = spi_board::i2c_scl;
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true;
    return i2c_new_master_bus(&bus, &g_i2c);
#endif
}
i2c_master_bus_handle_t bsp_i2c_get_handle() { return g_i2c; }

lv_display_t *bsp_display_start() {
#if defined(ASSISTANT_BOARD_CHD)
    if (g_display_started) return (!chd_touch_present() || g_pointer) ? g_display : nullptr;
#elif defined(ASSISTANT_BOARD_SPOTPEAR)
    if (g_display_started) return (!g_touch || g_pointer) ? g_display : nullptr;
#else
    if (g_display_started) return (!BSP_CAPS_TOUCH || g_pointer) ? g_display : nullptr;
#endif
    g_display_started = true;
    if (initialize_display() != ESP_OK) return nullptr;
    lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
    port.task_affinity = 1;
    port.task_priority = 2;
    if (lvgl_port_init(&port) != ESP_OK) return nullptr;
    lvgl_port_display_cfg_t display = {};
    display.io_handle = g_panel_io;
    display.panel_handle = g_panel;
    display.hres = spi_board::width;
    display.vres = spi_board::height;
    display.buffer_size = spi_board::width * 20;
    display.double_buffer = true;
    display.color_format = LV_COLOR_FORMAT_RGB565;
#if defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_AIPI) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    display.rotation.swap_xy = true;
    display.rotation.mirror_x = true;
#elif defined(ASSISTANT_BOARD_FREENOVE)
    display.rotation.swap_xy = true;
#elif defined(ASSISTANT_BOARD_CHD)
    display.rotation.swap_xy = true;
#if !defined(ASSISTANT_BOARD_CHD_LCD35)
    display.rotation.mirror_x = true;
#endif
#elif defined(ASSISTANT_BOARD_LABPLUS)
    display.rotation.swap_xy = true;
    display.rotation.mirror_x = true;
    display.rotation.mirror_y = true;
#elif defined(ASSISTANT_BOARD_MINSI)
    display.rotation.swap_xy = true;
    display.rotation.mirror_y = true;
#elif defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_QUANDONG) || defined(ASSISTANT_BOARD_KORVO2)
    display.rotation.swap_xy = true;
    display.rotation.mirror_y = true;
#elif defined(ASSISTANT_BOARD_K10) || defined(ASSISTANT_BOARD_ELECTRON)
    display.rotation.mirror_y = true;
#elif defined(ASSISTANT_BOARD_WS_CAM_35) || defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_P4_SPI35)
    display.rotation.mirror_x = true;
#elif defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    display.rotation.mirror_x = g_atom_s3r_mirror;
    display.rotation.mirror_y = g_atom_s3r_mirror;
#elif defined(ASSISTANT_BOARD_MAGICLICK_25)
    display.rotation.mirror_x = !g_magiclick_gc9107;
    display.rotation.mirror_y = !g_magiclick_gc9107;
#elif defined(ASSISTANT_BOARD_LCD_085) || defined(ASSISTANT_BOARD_DUCHATX) || defined(ASSISTANT_BOARD_MIXGO)
    // LVGL reapplies its configured hardware rotation during registration.
    display.rotation.mirror_x = true;
    display.rotation.mirror_y = true;
#elif defined(ASSISTANT_BOARD_XINGZHI_085) || defined(ASSISTANT_BOARD_MAGICLICK_24)
    display.rotation.mirror_y = true;
#elif defined(ASSISTANT_BOARD_SPOTPEAR_BOX) || defined(ASSISTANT_BOARD_MOVECALL) || defined(ASSISTANT_BOARD_JIUCHUAN)
    display.rotation.mirror_x = true;
#endif
    display.flags.buff_dma = true;
    display.flags.swap_bytes = true;
    if (!lvgl_port_lock(1000)) return nullptr;
    g_display = lvgl_port_add_disp(&display);
    if (g_display) {
#if BSP_CAPS_TOUCH
#if defined(ASSISTANT_BOARD_MVSRLORA) || defined(ASSISTANT_BOARD_BIGSMART)
        {
#elif defined(ASSISTANT_BOARD_CHD)
        if (chd_touch_present()) {
#else
        if (g_touch) {
#endif
            g_pointer = lv_indev_create();
            if (g_pointer) {
                lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
                lv_indev_set_display(g_pointer, g_display);
                lv_indev_set_read_cb(g_pointer, read_touch);
            }
        }
#endif
    }
    lvgl_port_unlock();
#if defined(ASSISTANT_BOARD_CHD)
    const bool pointer_failed = chd_touch_present() && !g_pointer;
#elif defined(ASSISTANT_BOARD_SPOTPEAR)
    const bool pointer_failed = g_touch && !g_pointer;
#else
    const bool pointer_failed = BSP_CAPS_TOUCH && !g_pointer;
#endif
    if (!g_display || pointer_failed) {
        ESP_LOGE(TAG, "LVGL display/touch allocation failed");
        return nullptr;
    }
    return g_display;
}
bool bsp_display_lock(uint32_t timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_display_unlock() { lvgl_port_unlock(); }
esp_err_t bsp_display_brightness_set(int percent) {
#if defined(ASSISTANT_BOARD_K10)
    if (percent != 0 && percent != 100) return ESP_ERR_NOT_SUPPORTED;
    return k10_set_backlight(percent == 100);
#elif defined(ASSISTANT_BOARD_WS_CAM)
    return waveshare_cam_set_brightness(percent);
#elif defined(ASSISTANT_BOARD_KORVO2)
    return korvo2_set_backlight(percent);
#elif defined(ASSISTANT_BOARD_ATOM_S3R_DISPLAY)
    return atom_s3r_set_brightness(percent);
#elif defined(ASSISTANT_BOARD_ATK_BOX) || defined(ASSISTANT_BOARD_SPARKBOT)
    if (!g_panel) return ESP_ERR_INVALID_STATE;
    if (percent != 0 && percent != 100) return ESP_ERR_NOT_SUPPORTED;
    return esp_lcd_panel_disp_on_off(g_panel, percent == 100);
#elif defined(ASSISTANT_BOARD_ATK_BOX3)
    if (percent != 0 && percent != 100) return ESP_ERR_NOT_SUPPORTED;
    return atk_box3_set_backlight(percent == 100);
#elif defined(ASSISTANT_BOARD_ATK_DEV)
    if (percent != 0 && percent != 100) return ESP_ERR_NOT_SUPPORTED;
    return atk_xl9555_set_backlight(percent == 100);
#else
    if (!g_pwm_ready || percent < 0 || percent > 100) return ESP_ERR_INVALID_ARG;
#if defined(ASSISTANT_BOARD_SPOTPEAR) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_MAGICLICK_25) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM)
    percent = 100 - percent;
#endif
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023 / 100), TAG, "backlight duty");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

#if defined(ASSISTANT_BOARD_SPOTPEAR)
bool bsp_display_touch_available() { return g_pointer != nullptr; }
#endif

#if defined(ASSISTANT_BOARD_LCD_183)
esp_err_t bsp_sdcard_mount() {
    // Vendor1.83 pinout: one-bit SDMMC on CLK2/CMD1/D0=3. Never format an
    // existing card; a missing/unreadable card must not prevent voice startup.
    static sdmmc_card_t *card;
    if (card) return ESP_OK;
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = GPIO_NUM_2;
    slot.cmd = GPIO_NUM_1;
    slot.d0 = GPIO_NUM_3;
    slot.width = 1;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_vfs_fat_sdmmc_mount_config_t mount = {};
    mount.format_if_mount_failed = false;
    mount.max_files = 5;
    mount.allocation_unit_size = 16 * 1024;
    return esp_vfs_fat_sdmmc_mount(BSP_SD_MOUNT_POINT, &host, &slot, &mount, &card);
}
#endif
