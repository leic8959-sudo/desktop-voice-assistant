#include "board_profile.h"

#if defined(ASSISTANT_BOARD_WS_RGB_7C) || defined(ASSISTANT_BOARD_S31_KORVO)
#include <algorithm>
#include <array>
#endif
#include <cstring>
#include "driver/i2s_std.h"
#if defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || (defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX))) || (defined(ASSISTANT_BOARD_P4_MIPI) && !((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4)
#include "driver/i2s_tdm.h"
#endif
#include "esp_check.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
namespace audio_pins = kevin_board;
constexpr i2c_port_t kCodecI2cPort = I2C_NUM_1;
#else
namespace audio_pins = spi_board;
#if defined(ASSISTANT_BOARD_SENSECAP) || (defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX))
constexpr i2c_port_t kCodecI2cPort = I2C_NUM_0;
#elif (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C))))
constexpr i2c_port_t kCodecI2cPort = I2C_NUM_0;
#elif defined(ASSISTANT_BOARD_CORES3) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_CHD) || defined(ASSISTANT_BOARD_KORVO2) || defined(ASSISTANT_BOARD_WS_CAM) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || defined(ASSISTANT_BOARD_MAZA_S31)
constexpr i2c_port_t kCodecI2cPort = I2C_NUM_1;
#else
constexpr i2c_port_t kCodecI2cPort = I2C_NUM_0;
#endif
#endif
constexpr const char *TAG = "waveshare_spi_audio";
i2s_chan_handle_t g_output_channel, g_input_channel;
const audio_codec_data_if_t *g_data;
const audio_codec_ctrl_if_t *g_output_control, *g_input_control;
const audio_codec_if_t *g_output_codec, *g_input_codec;
const audio_codec_gpio_if_t *g_gpio;
audio_codec_data_if_t g_checked_data{};
int g_input_error, g_output_error;

void remember_error(esp_codec_dev_type_t type, int result) {
    if (result == ESP_CODEC_DEV_OK) return;
    if (type & ESP_CODEC_DEV_TYPE_IN) g_input_error = result;
    if (type & ESP_CODEC_DEV_TYPE_OUT) g_output_error = result;
    ESP_LOGE(TAG, "data_setup_failed direction=%d result=%d", type, result);
}
bool data_is_open(const audio_codec_data_if_t *) { return g_data->is_open(g_data); }
int data_format(const audio_codec_data_if_t *, esp_codec_dev_type_t type, esp_codec_dev_sample_info_t *format) {
    // esp_codec_dev1.5.11 ignores set_fmt/enable errors. Record the underlying
    // driver's result and check it after both codec handles have opened.
    if (type & ESP_CODEC_DEV_TYPE_IN) g_input_error = ESP_CODEC_DEV_OK;
    if (type & ESP_CODEC_DEV_TYPE_OUT) g_output_error = ESP_CODEC_DEV_OK;
    const int result = g_data->set_fmt(g_data, type, format);
    remember_error(type, result);
    return result;
}
int data_enable(const audio_codec_data_if_t *, esp_codec_dev_type_t type, bool enabled) {
    const int result = g_data->enable(g_data, type, enabled);
    remember_error(type, result);
    return result;
}
int transfer(uint8_t *buffer, int size, bool input) {
    if (!buffer || size <= 0 || size % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    const int64_t deadline = esp_timer_get_time() + 1000000;
    size_t completed = 0;
    esp_err_t result = ESP_OK;
    while (completed < static_cast<size_t>(size)) {
        const int64_t remaining = deadline - esp_timer_get_time();
        if (remaining <= 0) { result = ESP_ERR_TIMEOUT; break; }
        const uint32_t timeout_ms = static_cast<uint32_t>((remaining + 999) / 1000);
        size_t transferred = 0;
        result = input ? i2s_channel_read(g_input_channel, buffer + completed, size - completed, &transferred, timeout_ms)
            : i2s_channel_write(g_output_channel, buffer + completed, size - completed, &transferred, timeout_ms);
        completed += transferred;
        if (result != ESP_OK) break;
        if (!transferred) { result = ESP_ERR_TIMEOUT; break; }
    }
    if (result != ESP_OK || completed != static_cast<size_t>(size)) {
        if (input) std::memset(buffer, 0, size);
        ESP_LOGE(TAG, "transfer input=%d bytes=%u/%d error=%s", input, static_cast<unsigned>(completed), size, esp_err_to_name(result));
        return ESP_CODEC_DEV_DRV_ERR;
    }
    return ESP_CODEC_DEV_OK;
}
int data_read(const audio_codec_data_if_t *, uint8_t *buffer, int size) { return transfer(buffer, size, true); }
int data_write(const audio_codec_data_if_t *, uint8_t *buffer, int size) { return transfer(buffer, size, false); }
int data_close(const audio_codec_data_if_t *) { return g_data->close(g_data); }

void release_audio(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone) {
#if ((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))
    if (*microphone == *speaker) *microphone = nullptr;
#endif
    if (*speaker) { esp_codec_dev_delete(*speaker); *speaker = nullptr; }
    if (*microphone) { esp_codec_dev_delete(*microphone); *microphone = nullptr; }
    if (g_output_codec) audio_codec_delete_codec_if(g_output_codec);
    if (g_input_codec && g_input_codec != g_output_codec) audio_codec_delete_codec_if(g_input_codec);
    if (g_output_control) audio_codec_delete_ctrl_if(g_output_control);
    if (g_input_control) audio_codec_delete_ctrl_if(g_input_control);
    if (g_gpio) audio_codec_delete_gpio_if(g_gpio);
    if (g_data) audio_codec_delete_data_if(g_data);
    if (g_output_channel) i2s_del_channel(g_output_channel);
    if (g_input_channel) i2s_del_channel(g_input_channel);
    g_output_codec = g_input_codec = nullptr;
    g_output_control = g_input_control = nullptr;
    g_gpio = nullptr; g_data = nullptr;
    g_output_channel = g_input_channel = nullptr;
}

esp_err_t initialize_audio(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone) {
#if defined(ASSISTANT_BOARD_CORES3)
    ESP_RETURN_ON_ERROR(cores3_prepare_audio(), TAG, "CoreS3 codec power/reset");
#endif
#if defined(ASSISTANT_BOARD_VOCAT)
    if (!vocat_board_ready()) return ESP_ERR_INVALID_STATE;
#endif
#if defined(ASSISTANT_BOARD_KEVIN_BOX2)
    ESP_RETURN_ON_ERROR(bsp_codec_i2c_init(), TAG, "audio I2C1");
    const auto codec_bus = bsp_codec_i2c_get_handle();
#else
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "audio I2C");
    const auto codec_bus = bsp_i2c_get_handle();
#endif
    i2s_chan_config_t channels = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channels.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channels, &g_output_channel, &g_input_channel), TAG, "audio I2S");
    i2s_std_config_t format = {};
    static_assert(board_profile::speaker_sample_rate == board_profile::microphone_sample_rate,
        "Shared codec clocks require matching physical input/output rates");
    static_assert(board_profile::speaker_sample_rate == 16000 || board_profile::speaker_sample_rate == 24000,
        "Shared codec backend supports native16kHz or24kHz");
    format.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(board_profile::speaker_sample_rate);
    format.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#if ((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))
    format.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
#endif
    format.gpio_cfg = {.mclk = audio_pins::mclk, .bclk = audio_pins::bclk, .ws = audio_pins::word_select,
        .dout = audio_pins::audio_output, .din = audio_pins::audio_input,
        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}};
#if defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || (defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX))) || (defined(ASSISTANT_BOARD_P4_MIPI) && !((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4)
    format.gpio_cfg.din = I2S_GPIO_UNUSED;
    // TDM RX has four16-bit slots (64 clocks/frame). Match that frame from
    // initial TX setup, before the codec data interface opens either side.
    format.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    format.slot_cfg.ws_width = 32;
#endif
#if defined(ASSISTANT_BOARD_SENSECAP)
    format.gpio_cfg.din = I2S_GPIO_UNUSED;
#endif
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_output_channel, &format), TAG, "I2S output");
#if defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || (defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX))) || (defined(ASSISTANT_BOARD_P4_MIPI) && !((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4)
    i2s_tdm_config_t input = {};
    input.clk_cfg = I2S_TDM_CLK_DEFAULT_CONFIG(board_profile::microphone_sample_rate);
    input.slot_cfg = I2S_TDM_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO,
        static_cast<i2s_tdm_slot_mask_t>(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3));
    input.slot_cfg.total_slot = 4;
    input.gpio_cfg = {.mclk = audio_pins::mclk, .bclk = audio_pins::bclk, .ws = audio_pins::word_select,
        .dout = I2S_GPIO_UNUSED, .din = audio_pins::audio_input,
        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}};
    ESP_RETURN_ON_ERROR(i2s_channel_init_tdm_mode(g_input_channel, &input), TAG, "TDM4 input");
#else
#if defined(ASSISTANT_BOARD_SENSECAP)
    format.gpio_cfg.din = audio_pins::audio_input; format.gpio_cfg.dout = I2S_GPIO_UNUSED;
    format.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
#endif
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_input_channel, &format), TAG, "I2S input");
#endif
    audio_codec_i2s_cfg_t data = {};
    data.port = I2S_NUM_0;
    data.rx_handle = g_input_channel;
    data.tx_handle = g_output_channel;
    g_data = audio_codec_new_i2s_data(&data);
    g_gpio = audio_codec_new_gpio();
    if (!g_data || !g_gpio) return ESP_ERR_NO_MEM;
    g_checked_data.is_open = data_is_open;
    g_checked_data.set_fmt = data_format;
    g_checked_data.enable = data_enable;
    g_checked_data.read = data_read;
    g_checked_data.write = data_write;
    g_checked_data.close = data_close;
#if ((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))
#if defined(ASSISTANT_BOARD_MOSAICO)
    constexpr int codec_address = 0x19 << 1; // codec_dev takes the8-bit form
#else
    constexpr int codec_address = ES8311_CODEC_DEFAULT_ADDR;
#endif
    audio_codec_i2c_cfg_t control = {.port = kCodecI2cPort, .addr = codec_address, .bus_handle = codec_bus};
    g_output_control = audio_codec_new_i2c_ctrl(&control);
    if (!g_output_control) return ESP_ERR_NO_MEM;
    es8311_codec_cfg_t codec = {};
    codec.ctrl_if = g_output_control; codec.gpio_if = g_gpio;
    codec.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
#if defined(ASSISTANT_BOARD_SPOT_C5)
    codec.use_mclk = false; // Spot supplies BCLK/WS only; derive codec clock.
#else
    codec.use_mclk = true;
#endif
    // PA53 stays INPUT_OUTPUT for the common checked mute/readback path.
    codec.pa_pin = GPIO_NUM_NC;
    codec.hw_gain.pa_voltage = 5.0f; codec.hw_gain.codec_dac_voltage = 3.3f;
    g_output_codec = es8311_codec_new(&codec); g_input_codec = g_output_codec;
    if (!g_output_codec) return ESP_ERR_NO_MEM;
#elif defined(ASSISTANT_BOARD_MIXGO)
    audio_codec_i2c_cfg_t control = {.port = kCodecI2cPort, .addr = ES8374_CODEC_DEFAULT_ADDR, .bus_handle = codec_bus};
    g_output_control = audio_codec_new_i2c_ctrl(&control);
    if (!g_output_control) return ESP_ERR_NO_MEM;
    es8374_codec_cfg_t codec = {};
    codec.ctrl_if = g_output_control;
    codec.gpio_if = g_gpio;
    codec.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec.master_mode = false;
    codec.pa_pin = GPIO_NUM_NC;
    g_output_codec = es8374_codec_new(&codec);
    g_input_codec = g_output_codec;
    if (!g_output_codec) return ESP_ERR_NO_MEM;
#elif (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO))
    audio_codec_i2c_cfg_t output_control = {.port = kCodecI2cPort, .addr = ES8389_CODEC_DEFAULT_ADDR, .bus_handle = codec_bus};
    g_output_control = audio_codec_new_i2c_ctrl(&output_control);
    if (!g_output_control) return ESP_ERR_NO_MEM;
    es8389_codec_cfg_t codec = {};
    codec.ctrl_if = g_output_control;
    codec.gpio_if = g_gpio;
    codec.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    codec.pa_pin = GPIO_NUM_NC;
#if defined(ASSISTANT_BOARD_S31_KORVO)
    codec.use_mclk = false;
#else
    codec.use_mclk = true;
#endif
    codec.master_mode = false;
    codec.hw_gain.pa_voltage = 5.0f;
    codec.hw_gain.codec_dac_voltage = 3.3f;
    g_output_codec = es8389_codec_new(&codec);
    g_input_codec = g_output_codec;
    if (!g_output_codec) return ESP_ERR_NO_MEM;
#elif defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_LABPLUS)
    audio_codec_i2c_cfg_t output_control = {.port = kCodecI2cPort, .addr = ES8388_CODEC_DEFAULT_ADDR, .bus_handle = codec_bus};
    g_output_control = audio_codec_new_i2c_ctrl(&output_control);
    if (!g_output_control) return ESP_ERR_NO_MEM;
    es8388_codec_cfg_t codec = {};
    codec.ctrl_if = g_output_control;
    codec.gpio_if = g_gpio;
    codec.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    // ESP I2S owns BCLK/WS. The fixed board class sets both sides master;
    // use the codec's public slave mode to avoid two active clock sources.
    codec.master_mode = false;
    codec.pa_pin = audio_pins::amplifier;
    codec.hw_gain.pa_voltage = 5.0f;
    codec.hw_gain.codec_dac_voltage = 3.3f;
    g_output_codec = es8388_codec_new(&codec);
    g_input_codec = g_output_codec;
    if (!g_output_codec) return ESP_ERR_NO_MEM;
#else
#if defined(ASSISTANT_BOARD_CORES3)
    constexpr int output_address = AW88298_CODEC_DEFAULT_ADDR;
#elif defined(ASSISTANT_BOARD_BOXLITE)
    constexpr int output_address = ES8156_CODEC_DEFAULT_ADDR;
#elif defined(ASSISTANT_BOARD_M5_TAB5)
    constexpr int output_address = ES8388_CODEC_DEFAULT_ADDR;
#elif defined(ASSISTANT_BOARD_WS_RGB_7C)
    constexpr int output_address = ES8389_CODEC_DEFAULT_ADDR;
#else
    constexpr int output_address = ES8311_CODEC_DEFAULT_ADDR;
#endif
    audio_codec_i2c_cfg_t output_control = {.port = kCodecI2cPort, .addr = output_address, .bus_handle = codec_bus};
#if defined(ASSISTANT_BOARD_BOXLITE)
    constexpr int adc_address = ES7243E_CODEC_DEFAULT_ADDR;
#elif defined(ASSISTANT_BOARD_SENSECAP)
    constexpr int adc_address = 0x28; // physical0x14,codec control API expects8-bit address
#elif defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_LCD_EV)
    constexpr int adc_address = 0x82;
#else
    constexpr int adc_address = ES7210_CODEC_DEFAULT_ADDR;
#endif
    audio_codec_i2c_cfg_t input_control = {.port = kCodecI2cPort, .addr = adc_address, .bus_handle = codec_bus};
    g_output_control = audio_codec_new_i2c_ctrl(&output_control);
    g_input_control = audio_codec_new_i2c_ctrl(&input_control);
    if (!g_output_control || !g_input_control) return ESP_ERR_NO_MEM;
#if defined(ASSISTANT_BOARD_CORES3)
    aw88298_codec_cfg_t output_codec = {};
#elif defined(ASSISTANT_BOARD_BOXLITE)
    es8156_codec_cfg_t output_codec = {};
#elif defined(ASSISTANT_BOARD_M5_TAB5)
    es8388_codec_cfg_t output_codec = {};
#elif defined(ASSISTANT_BOARD_WS_RGB_7C)
    es8389_codec_cfg_t output_codec = {};
#else
    es8311_codec_cfg_t output_codec = {};
#endif
    output_codec.ctrl_if = g_output_control;
    output_codec.gpio_if = g_gpio;
#if defined(ASSISTANT_BOARD_CORES3)
    output_codec.reset_pin = GPIO_NUM_NC;
    output_codec.hw_gain.pa_gain = 15;
#elif defined(ASSISTANT_BOARD_BOXLITE)
    output_codec.pa_pin = GPIO_NUM_NC;
    output_codec.hw_gain.pa_voltage = 5.0f;
    output_codec.hw_gain.codec_dac_voltage = 3.3f;
#else
    output_codec.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
#if (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4)
    // The board owns PA53 readback or the Tab5 expander. Let the common
    // checked digital-mute path gate it; codec GPIO setup would bypass that
    // ownership and disable the input buffer used by GPIO readback.
    output_codec.pa_pin = GPIO_NUM_NC;
#else
    output_codec.pa_pin = audio_pins::amplifier;
#endif
#if defined(ASSISTANT_BOARD_M5_TAB5)
    // ESP owns BCLK/WS; the pinned Tab5 class also made the DAC master.
    // Select the public slave mode so only one device drives the clocks.
    output_codec.master_mode = false;
#else
    output_codec.use_mclk = true;
#endif
    output_codec.hw_gain.pa_voltage = 5.0f;
    output_codec.hw_gain.codec_dac_voltage = 3.3f;
#endif
#if defined(ASSISTANT_BOARD_SENSECAP) || defined(ASSISTANT_BOARD_BOXLITE)
    es7243e_codec_cfg_t input_codec = {};
    input_codec.ctrl_if = g_input_control;
#if defined(ASSISTANT_BOARD_BOXLITE)
    g_output_codec = es8156_codec_new(&output_codec);
#else
    g_output_codec = es8311_codec_new(&output_codec);
#endif
    g_input_codec = es7243e_codec_new(&input_codec);
#else
    es7210_codec_cfg_t input_codec = {};
    input_codec.ctrl_if = g_input_control;
    // Match each board's ADC mask; the common pipeline selects MIC1 and uses
    // a software reference. No hardware AEC/beamforming is claimed.
#if defined(ASSISTANT_BOARD_ECHO_PYRAMID)
    input_codec.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC3;
#elif defined(ASSISTANT_BOARD_KEVIN_BOX2) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || (defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX))) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4)
    // The pinned BoxAudioCodec enables all four physical ADCs. Select only
    // TDM slot0/MIC1 for the common mono software-AEC path during open.
    input_codec.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
#else
    input_codec.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2;
#endif
#if defined(ASSISTANT_BOARD_CORES3)
    g_output_codec = aw88298_codec_new(&output_codec);
#elif defined(ASSISTANT_BOARD_M5_TAB5)
    g_output_codec = es8388_codec_new(&output_codec);
#elif defined(ASSISTANT_BOARD_WS_RGB_7C)
    g_output_codec = es8389_codec_new(&output_codec);
#else
    g_output_codec = es8311_codec_new(&output_codec);
#endif
    g_input_codec = es7210_codec_new(&input_codec);
#endif
    if (!g_output_codec || !g_input_codec) return ESP_ERR_NO_MEM;
#endif
#if ((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))
    esp_codec_dev_cfg_t device = {.dev_type = ESP_CODEC_DEV_TYPE_IN_OUT, .codec_if = g_output_codec, .data_if = &g_checked_data};
    *speaker = esp_codec_dev_new(&device); *microphone = *speaker;
#else
    esp_codec_dev_cfg_t speaker_config = {.dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = g_output_codec, .data_if = &g_checked_data};
    esp_codec_dev_cfg_t microphone_config = {.dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = g_input_codec, .data_if = &g_checked_data};
    *speaker = esp_codec_dev_new(&speaker_config);
    *microphone = esp_codec_dev_new(&microphone_config);
#endif
#if defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_LABPLUS) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_WS_RGB_7C) || defined(ASSISTANT_BOARD_SENSECAP)
    if (*speaker && *microphone) {
        // Preserve the board's continuous shared-clock lifecycle. Some boards
        // share one codec;7C keeps both its DAC and ADC enabled like its source.
        // The existing audio state lock serializes their lifecycle.
        if (esp_codec_set_disable_when_closed(*speaker, false) != ESP_CODEC_DEV_OK ||
            esp_codec_set_disable_when_closed(*microphone, false) != ESP_CODEC_DEV_OK) return ESP_FAIL;
    }
#endif
    return *speaker && *microphone ? ESP_OK : ESP_ERR_NO_MEM;
}
}

esp_err_t waveshare_spi_audio_open_status() {
    if (g_input_error != ESP_CODEC_DEV_OK || g_output_error != ESP_CODEC_DEV_OK) return ESP_FAIL;
#if defined(ASSISTANT_BOARD_ECHO_PYRAMID)
    ESP_RETURN_ON_ERROR(echo_pyramid_audio_status(), TAG, "Pyramid external clock/PA status");
#endif
#if defined(ASSISTANT_BOARD_MIXGO)
    // The fixed MixGo README requires input select0x24 instead of the driver's
    // default0x14. Apply it after open, preserving the other register bits and
    // leaving the shared installed codec source untouched.
    uint8_t previous = 0, actual = 0;
    const int read = g_output_control->read_reg(g_output_control, 0x21, 1, &previous, 1);
    uint8_t selected = (previous & 0xcf) | 0x24;
    const int write = read == ESP_CODEC_DEV_OK ? g_output_control->write_reg(g_output_control, 0x21, 1, &selected, 1) : read;
    const int verify = write == ESP_CODEC_DEV_OK ? g_output_control->read_reg(g_output_control, 0x21, 1, &actual, 1) : write;
    if (read != ESP_CODEC_DEV_OK || write != ESP_CODEC_DEV_OK || verify != ESP_CODEC_DEV_OK || actual != selected) {
        ESP_LOGE(TAG, "ES8374 input select failed read=%d write=%d verify=%d actual=0x%02x", read, write, verify, actual);
        return ESP_FAIL;
    }
#endif
#if defined(ASSISTANT_BOARD_LICHUANG)
    // Physical MIC3 gain mask2 differs from TDM reference slot1. The current
    // mono pipeline reads MIC1/slot0; preserve the owning reference gain0dB.
    if (!g_input_codec->set_mic_channel_gain || g_input_codec->set_mic_channel_gain(g_input_codec, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(2), 0.0f) != ESP_CODEC_DEV_OK) return ESP_FAIL;
#endif
#if defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_LABPLUS)
    // The owning board sets these four analog outputs to0dB after opening.
    // Keep their values, but propagate write/readback errors rather than
    // silently assuming the register update succeeded.
    uint8_t analog_volume = 30;
    for (int reg = 46; reg <= 49; ++reg) {
        uint8_t actual = 0;
        const int write = g_output_control->write_reg(g_output_control, reg, 1, &analog_volume, 1);
        const int read = write == ESP_CODEC_DEV_OK ? g_output_control->read_reg(g_output_control, reg, 1, &actual, 1) : write;
        if (write != ESP_CODEC_DEV_OK || read != ESP_CODEC_DEV_OK || actual != analog_volume) {
            ESP_LOGE(TAG, "ES8388 analog output setup failed register=%d write=%d read=%d actual=%u", reg, write, read, actual);
            return ESP_FAIL;
        }
    }
#endif
    return ESP_OK;
}

#if defined(ASSISTANT_BOARD_WS_RGB_7C) || defined(ASSISTANT_BOARD_S31_KORVO)
int waveshare_spi_audio_write_mono(esp_codec_dev_handle_t speaker, uint8_t *data, size_t length) {
    if (!speaker || !data || !length || length % sizeof(int16_t)) return ESP_CODEC_DEV_INVALID_ARG;
    // Match the fixed ES8389 board's stereo output at the codec boundary.
    // Bound conversion memory and let codec_dev apply the configured volume
    // to the actual stereo packet. The common AEC reference remains mono.
    std::array<int16_t, 512> stereo{};
    const auto *mono = reinterpret_cast<const int16_t *>(data);
    const size_t samples = length / sizeof(int16_t);
    size_t completed = 0;
    while (completed < samples) {
        const size_t count = std::min(samples - completed, stereo.size() / 2);
        for (size_t index = 0; index < count; ++index) stereo[index * 2] = stereo[index * 2 + 1] = mono[completed + index];
        const int result = esp_codec_dev_write(speaker, stereo.data(), static_cast<int>(count * 2 * sizeof(int16_t)));
        if (result != ESP_CODEC_DEV_OK) return result;
        completed += count;
    }
    return ESP_CODEC_DEV_OK;
}
#endif

#if defined(ASSISTANT_BOARD_CORES3) || (((defined(ASSISTANT_BOARD_AMOLED_216) || (defined(ASSISTANT_BOARD_AMOLED_206) || (defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)))) || (defined(ASSISTANT_BOARD_BOX3) || defined(ASSISTANT_BOARD_BOX) || defined(ASSISTANT_BOARD_BOXLITE))) || defined(ASSISTANT_BOARD_AMOLED_175) || defined(ASSISTANT_BOARD_AMOLED_175C)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_PCA_BOX) || defined(ASSISTANT_BOARD_ZHENGCHEN_CAM) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_ATK_BOX3) || defined(ASSISTANT_BOARD_WS_AUDIO) || defined(ASSISTANT_BOARD_CHD) || (defined(ASSISTANT_BOARD_WS_RGB_C) || ((defined(ASSISTANT_BOARD_WS4B) || (defined(ASSISTANT_BOARD_LCD_EV) || defined(ASSISTANT_BOARD_VOCAT))) || (defined(ASSISTANT_BOARD_WS349) || defined(ASSISTANT_BOARD_WS143C)))) || defined(ASSISTANT_BOARD_KORVO2) || (defined(ASSISTANT_BOARD_WS_CAM) || (defined(ASSISTANT_BOARD_WS_RLCD) || defined(ASSISTANT_BOARD_MATRIX))) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || defined(ASSISTANT_BOARD_ECHO_PYRAMID) || defined(ASSISTANT_BOARD_SENSECAP) || defined(ASSISTANT_BOARD_P4_MIPI) || defined(ASSISTANT_BOARD_M5_TAB5) || defined(ASSISTANT_BOARD_M5_COREP4) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31))
int waveshare_spi_audio_set_microphone_gain(float gain) {
    // Return the actual codec driver's result; codec_dev1.5.11 discards it.
#if defined(ASSISTANT_BOARD_BOXLITE) || defined(ASSISTANT_BOARD_SENSECAP) || defined(ASSISTANT_BOARD_ATK_DEV) || (defined(ASSISTANT_BOARD_ATK_BOX2) || defined(ASSISTANT_BOARD_S31_KORVO)) || defined(ASSISTANT_BOARD_MIXGO) || defined(ASSISTANT_BOARD_LABPLUS) || ((defined(ASSISTANT_BOARD_AMOLED_132) || defined(ASSISTANT_BOARD_SH8601)) || (defined(ASSISTANT_BOARD_C5_LCD169) || (defined(ASSISTANT_BOARD_MOJI2_C5) || defined(ASSISTANT_BOARD_SPOT_C5))) || defined(ASSISTANT_BOARD_P4_NANO) || ((defined(ASSISTANT_BOARD_S31_CORE) || defined(ASSISTANT_BOARD_MOSAICO)) || defined(ASSISTANT_BOARD_MAZA_S31)))
    if (!g_input_codec || !g_input_codec->set_mic_gain) return ESP_CODEC_DEV_WRONG_STATE;
    return g_input_codec->set_mic_gain(g_input_codec, gain);
#else
    if (!g_input_codec || !g_input_codec->set_mic_channel_gain) return ESP_CODEC_DEV_WRONG_STATE;
    return g_input_codec->set_mic_channel_gain(g_input_codec, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), gain);
#endif
}

int waveshare_spi_audio_set_output_mute(bool muted) {
    if (!g_output_codec || !g_output_codec->mute) return ESP_CODEC_DEV_WRONG_STATE;
    return g_output_codec->mute(g_output_codec, muted);
}
#endif

esp_err_t waveshare_spi_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone) {
    if (!speaker || !microphone || g_data) return ESP_ERR_INVALID_STATE;
    *speaker = nullptr; *microphone = nullptr;
    const esp_err_t result = initialize_audio(speaker, microphone);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "audio_init failed: %s", esp_err_to_name(result));
        release_audio(speaker, microphone);
        return result;
    }
#if defined(ASSISTANT_BOARD_SH8601) || defined(ASSISTANT_BOARD_AMOLED_132)
    ESP_LOGI(TAG, "audio_initialized board=%s rate=24000 codec=ES8311 mono input_output PA=GPIO46 reference=software", board_profile::name);
#elif defined(ASSISTANT_BOARD_ECHO_PYRAMID)
    ESP_LOGI(TAG, "audio_initialized rate=24000 input=ES7210-MIC1/MIC3-STD2 output=ES8311 MCLK=external6144000 reference=software");
#elif defined(ASSISTANT_BOARD_SPOT_C5)
    ESP_LOGI(TAG, "audio_initialized rate=16000 codec=ES8311 mono input_output BCLK_clock PA=GPIO23 reference=software");
#elif defined(ASSISTANT_BOARD_MOJI2_C5)
    ESP_LOGI(TAG, "audio_initialized rate=24000 codec=ES8311 mono input_output PA=GPIO5 reference=software");
#elif defined(ASSISTANT_BOARD_BOXLITE)
    ESP_LOGI(TAG, "audio_initialized rate=16000 input=ES7243E output=ES8156 selected_slot=0 reference=software");
#elif defined(ASSISTANT_BOARD_C5_LCD169)
    ESP_LOGI(TAG, "audio_initialized rate=24000 codec=ES8311 mono input_output PA=TCA9554-P0 reference=software");
#elif defined(ASSISTANT_BOARD_MIXGO)
    ESP_LOGI(TAG, "audio_initialized rate=24000 codec=ES8374 input=mono reference=software MCLK=256fs");
#elif defined(ASSISTANT_BOARD_S31_KORVO)
    ESP_LOGI(TAG, "audio_initialized rate=16000 codec=ES8389 input=mono output=stereo_duplicated reference=software BCLK_clock=retained");
#elif defined(ASSISTANT_BOARD_ATK_BOX2)
    ESP_LOGI(TAG, "audio_initialized rate=24000 codec=ES8389 input=mono reference=software TX_clock=retained");
#elif defined(ASSISTANT_BOARD_YUNLIAO) || defined(ASSISTANT_BOARD_ATK_DEV) || defined(ASSISTANT_BOARD_LABPLUS)
    ESP_LOGI(TAG, "audio_initialized rate=24000 codec=ES8388 mic=left reference=software codec_clock=slave");
#else
    ESP_LOGI(TAG, "audio_initialized rate=%lu input=ES7210 output=ES8311 selected_mic=1 reference=software", static_cast<unsigned long>(board_profile::speaker_sample_rate));
#endif
    return ESP_OK;
}
