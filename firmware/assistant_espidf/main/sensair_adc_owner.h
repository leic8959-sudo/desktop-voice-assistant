#pragma once

#include "audio_codec_data_if.h"

// Evidence candidate only. All underlying ADC operations, including its
// factory/format/close paths, run on one task. Input is16kHz PCM16 mono.
// Lifecycle caller must join all public callbacks before deleting this data_if.
// A timeout poisons the instance; cleanup can remain pending on the ADC task.
// A close error requires device restart before hardware reinitialization;
// this candidate does not yet export a cleanup-join handle to the board owner.
const audio_codec_data_if_t *sensair_adc_owner_new();
// Unlike data_if::is_open (object lifetime), this requires a successful
// format and enable transaction. codec_dev discards those callback results.
bool sensair_adc_owner_ready(const audio_codec_data_if_t *interface);
