#pragma once

#include "driver/gpio.h"
#include "esp_codec_dev.h"
#include "esp_err.h"

namespace digital_audio_pins {
#if defined(ASSISTANT_BOARD_ELECTRON)
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_42, microphone_clock = GPIO_NUM_40, microphone_data = GPIO_NUM_41;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_18, speaker_word_select = GPIO_NUM_8, speaker_data = GPIO_NUM_17;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_K10)
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_0, microphone_clock = GPIO_NUM_38, microphone_data = GPIO_NUM_39;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_0, speaker_word_select = GPIO_NUM_38, speaker_data = GPIO_NUM_45;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_MVSRLORA)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_1, microphone_data = GPIO_NUM_2;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_4, speaker_word_select = GPIO_NUM_15, speaker_data = GPIO_NUM_11;
// Board wrapper owns boost41 with checked pad readback;dummycodec has no PA.
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_TCAMERA)
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_41, speaker_word_select = GPIO_NUM_42;
#if defined(ASSISTANT_BOARD_TCAMERA_V12)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_40, microphone_data = GPIO_NUM_38;
inline constexpr gpio_num_t speaker_data = GPIO_NUM_39;
#else
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_39, microphone_data = GPIO_NUM_40, microphone_bclk = GPIO_NUM_18;
inline constexpr gpio_num_t speaker_data = GPIO_NUM_38;
#endif
// GPIO18onV1.2powersbothMICandMAX98357throughP-channelMOSFETs(activeLOW).
// It stays low duringcapture/mute;dummycodec must not control this sharedrail.
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_OTTO)
inline gpio_num_t microphone_clock = GPIO_NUM_NC, microphone_data = GPIO_NUM_NC, microphone_bclk = GPIO_NUM_NC;
inline gpio_num_t speaker_bclk = GPIO_NUM_NC, speaker_word_select = GPIO_NUM_NC, speaker_data = GPIO_NUM_NC;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_EDA_BEAR)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_40, microphone_data = GPIO_NUM_41;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_39;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_48, speaker_word_select = GPIO_NUM_45, speaker_data = GPIO_NUM_47;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_EDA_ROBOT)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_17, microphone_data = GPIO_NUM_18;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_16;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_39, speaker_word_select = GPIO_NUM_38, speaker_data = GPIO_NUM_40;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_GUITION)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_7, microphone_data = GPIO_NUM_15;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_6;
#if defined(ASSISTANT_BOARD_GUITION_EXT_AMP)
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_9, speaker_word_select = GPIO_NUM_14, speaker_data = GPIO_NUM_16;
#else
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_42, speaker_word_select = GPIO_NUM_2, speaker_data = GPIO_NUM_41;
#endif
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_BREAD_S3_CAM)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_1, microphone_data = GPIO_NUM_42;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_2;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_40, speaker_word_select = GPIO_NUM_41, speaker_data = GPIO_NUM_39;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_ATK_BOX)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_13, microphone_data = GPIO_NUM_47;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_21;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_21, speaker_word_select = GPIO_NUM_13, speaker_data = GPIO_NUM_14;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_MINSI)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_9, microphone_data = GPIO_NUM_18;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_10;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_5, speaker_word_select = GPIO_NUM_6, speaker_data = GPIO_NUM_4;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_DUCHATX)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_39, microphone_data = GPIO_NUM_7;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_38;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_42, speaker_word_select = GPIO_NUM_2, speaker_data = GPIO_NUM_40;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_DF_AI_CAM)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_38, microphone_data = GPIO_NUM_39;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_45, speaker_word_select = GPIO_NUM_46, speaker_data = GPIO_NUM_42;
// GPIO41 is an unused gain definition in the owning source, not a PA enable.
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_DOIT)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_41, microphone_data = GPIO_NUM_42;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_17, speaker_word_select = GPIO_NUM_16, speaker_data = GPIO_NUM_18;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_TAIJI)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_45, microphone_data = GPIO_NUM_46;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_42;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_18, speaker_word_select = GPIO_NUM_16, speaker_data = GPIO_NUM_17;
inline constexpr int amplifier_enable = 48;
#elif defined(ASSISTANT_BOARD_WK)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_40, microphone_data = GPIO_NUM_41;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_42;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_47, speaker_word_select = GPIO_NUM_48, speaker_data = GPIO_NUM_21;
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_TCIRCLE)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_9, microphone_data = GPIO_NUM_8;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_7;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_5, speaker_word_select = GPIO_NUM_4, speaker_data = GPIO_NUM_6;
inline constexpr int amplifier_enable = 45;
#elif defined(ASSISTANT_BOARD_LCD_146) || defined(ASSISTANT_BOARD_LCD_185)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_2, microphone_data = GPIO_NUM_39;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_15;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_48, speaker_word_select = GPIO_NUM_38, speaker_data = GPIO_NUM_47;
// PCM5101 is followed by a board amplifier; no separate PA GPIO is declared.
inline constexpr int amplifier_enable = -1;
#elif defined(ASSISTANT_BOARD_TFT_DIGITAL) || defined(ASSISTANT_BOARD_OLED)
inline constexpr gpio_num_t microphone_clock = GPIO_NUM_4, microphone_data = GPIO_NUM_6;
inline constexpr gpio_num_t microphone_bclk = GPIO_NUM_5;
inline constexpr gpio_num_t speaker_bclk = GPIO_NUM_15, speaker_word_select = GPIO_NUM_16, speaker_data = GPIO_NUM_7;
#if defined(ASSISTANT_BOARD_HU087)
inline constexpr int amplifier_enable = 17;
#else
inline constexpr int amplifier_enable = -1;
#endif
#else
#error "No implemented digital audio wiring selected"
#endif
}

esp_err_t digital_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t digital_audio_open_status();
#if defined(ASSISTANT_BOARD_K10)
int digital_audio_set_microphone_gain(float gain);
#endif
#if defined(ASSISTANT_BOARD_MVSRLORA)
int digital_audio_set_output_mute(bool muted);
#endif
