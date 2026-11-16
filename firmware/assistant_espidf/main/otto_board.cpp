#include "otto_board.h"

#include "board_profile.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace {
constexpr const char *TAG = "otto_board";
bool g_ready, g_camera_board;
}

esp_err_t otto_board_init() {
    if (g_ready) return ESP_OK;
    gpio_config_t detection = {};
    detection.pin_bit_mask = (1ULL << GPIO_NUM_15) | (1ULL << GPIO_NUM_16);
    detection.mode = GPIO_MODE_INPUT;
    detection.pull_down_en = GPIO_PULLDOWN_ENABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&detection), TAG, "hardware detection inputs");
    esp_rom_delay_us(5000);
    bool camera_board = true;
    for (int sample = 0; sample < 8; ++sample) {
        if (!gpio_get_level(GPIO_NUM_15) || !gpio_get_level(GPIO_NUM_16)) {
            camera_board = false;
            break;
        }
        if (sample < 7) esp_rom_delay_us(1000);
    }
    // On non-camera hardware these are speaker clocks. Release the detection
    // pulls and input routing before assigning either I2S or unused camera I2C.
    gpio_config_t released = {};
    released.pin_bit_mask = detection.pin_bit_mask;
    released.mode = GPIO_MODE_DISABLE;
    ESP_RETURN_ON_ERROR(gpio_config(&released), TAG, "release detection pins");
    if (camera_board) {
        spi_board::lcd_mosi = GPIO_NUM_45;
        spi_board::lcd_clock = GPIO_NUM_48;
        spi_board::lcd_dc = GPIO_NUM_47;
        spi_board::lcd_cs = GPIO_NUM_NC;
        spi_board::lcd_reset = GPIO_NUM_1;
        spi_board::backlight = GPIO_NUM_38;
        digital_audio_pins::microphone_bclk = GPIO_NUM_42;
        digital_audio_pins::microphone_clock = GPIO_NUM_40;
        digital_audio_pins::microphone_data = GPIO_NUM_41;
        digital_audio_pins::speaker_bclk = GPIO_NUM_42;
        digital_audio_pins::speaker_word_select = GPIO_NUM_40;
        digital_audio_pins::speaker_data = GPIO_NUM_39;
        board_profile::speaker_sample_rate = 16000;
    } else {
        spi_board::lcd_mosi = GPIO_NUM_10;
        spi_board::lcd_clock = GPIO_NUM_9;
        spi_board::lcd_dc = GPIO_NUM_46;
        spi_board::lcd_cs = GPIO_NUM_12;
        spi_board::lcd_reset = GPIO_NUM_11;
        spi_board::backlight = GPIO_NUM_3;
        digital_audio_pins::microphone_bclk = GPIO_NUM_5;
        digital_audio_pins::microphone_clock = GPIO_NUM_4;
        digital_audio_pins::microphone_data = GPIO_NUM_6;
        digital_audio_pins::speaker_bclk = GPIO_NUM_15;
        digital_audio_pins::speaker_word_select = GPIO_NUM_16;
        digital_audio_pins::speaker_data = GPIO_NUM_7;
        board_profile::speaker_sample_rate = 24000;
    }
    g_camera_board = camera_board;
    g_ready = true;
    ESP_LOGI(TAG, "hardware_mapping=%s speaker_rate=%lu LCD_MOSI=%d CLK=%d CS=%d reset=%d; camera/servo initialization omitted",
        camera_board ? "camera" : "non_camera", static_cast<unsigned long>(board_profile::speaker_sample_rate),
        spi_board::lcd_mosi, spi_board::lcd_clock, spi_board::lcd_cs, spi_board::lcd_reset);
    return ESP_OK;
}

bool otto_board_ready() { return g_ready; }
bool otto_camera_board() { return g_ready && g_camera_board; }
