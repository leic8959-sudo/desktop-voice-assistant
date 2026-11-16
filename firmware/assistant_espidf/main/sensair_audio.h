#pragma once
#include "esp_codec_dev.h"
#include "esp_err.h"
esp_err_t sensair_audio_init(esp_codec_dev_handle_t *speaker, esp_codec_dev_handle_t *microphone);
esp_err_t sensair_audio_status();
int sensair_audio_set_mute(bool muted);
