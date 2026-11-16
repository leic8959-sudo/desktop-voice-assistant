#include "opus_playback_decoder.h"

#include <cstring>
#include <limits>

#include "esp_audio_dec.h"
#include "esp_log.h"
#include "esp_opus_dec.h"

namespace native_opus {
namespace {
constexpr const char *kTag = "opus_playback";
}

PlaybackDecoder::~PlaybackDecoder() {
    if (decoder_) {
        const auto result = esp_opus_dec_close(decoder_);
        if (result != ESP_AUDIO_ERR_OK) ESP_LOGE(kTag, "decoder close failed result=%d", result);
    }
}

esp_err_t PlaybackDecoder::Initialize(Sink sink, void *context) {
    if (decoder_ || !sink) return ESP_ERR_INVALID_ARG;
    esp_opus_dec_cfg_t configuration = ESP_OPUS_DEC_CONFIG_DEFAULT();
    configuration.sample_rate = 24000; configuration.channel = 1;
    configuration.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS; configuration.self_delimited = false;
    const auto result = esp_opus_dec_open(&configuration, sizeof(configuration), &decoder_);
    if (result != ESP_AUDIO_ERR_OK || !decoder_) {
        if (decoder_) {
            const auto closed = esp_opus_dec_close(decoder_);
            if (closed != ESP_AUDIO_ERR_OK) ESP_LOGE(kTag, "failed decoder cleanup result=%d", closed);
        }
        decoder_ = nullptr;
        ESP_LOGE(kTag, "decoder open failed result=%d", result);
        return result == ESP_AUDIO_ERR_MEM_LACK ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    sink_ = sink; context_ = context;
    return ESP_OK;
}

esp_err_t PlaybackDecoder::BeginPlayback(uint32_t generation) {
    if (!decoder_ || (have_playback_ && generation <= generation_)) return ESP_ERR_INVALID_STATE;
    const auto result = esp_opus_dec_reset(decoder_);
    if (result != ESP_AUDIO_ERR_OK) { ESP_LOGE(kTag, "decoder reset failed result=%d", result); return ESP_FAIL; }
    generation_ = generation; sequence_ = position_ = 0;
    have_playback_ = true; active_ = failed_ = false;
    return ESP_OK;
}

void PlaybackDecoder::Abort() { active_ = false; failed_ = true; }

esp_err_t PlaybackDecoder::Decode(uint32_t generation, const uint8_t *packet, size_t length) {
    if (!decoder_ || !have_playback_ || generation != generation_ || failed_) return ESP_ERR_INVALID_STATE;
    Header header{};
    if (!ReadHeader(packet, length, kOutputFrameSamples, &header)) { failed_ = true; return ESP_ERR_INVALID_RESPONSE; }
    if (header.flags & kReset) {
        if (active_ || header.stream <= stream_) { failed_ = true; return ESP_ERR_INVALID_STATE; }
        const auto result = esp_opus_dec_reset(decoder_);
        if (result != ESP_AUDIO_ERR_OK) { failed_ = true; return ESP_FAIL; }
        stream_ = header.stream; sequence_ = position_ = 0; active_ = true;
    }
    if (!active_ || header.stream != stream_ || header.sequence != sequence_ || header.position != position_) {
        failed_ = true; return ESP_ERR_INVALID_RESPONSE;
    }
    if (header.flags & kCancel) { Abort(); return ESP_OK; }
    if (header.flags & kEnd) {
        active_ = false;
        const auto result = sink_(nullptr, 0, true, context_);
        if (result != ESP_OK) failed_ = true;
        return result;
    }
    const size_t payload_bytes = length - kHeaderBytes;
    if (!PacketIs20msMono(packet + kHeaderBytes, payload_bytes, 24000) ||
        sequence_ == std::numeric_limits<uint32_t>::max() || header.valid > std::numeric_limits<uint32_t>::max() - position_) {
        failed_ = true; return ESP_ERR_INVALID_SIZE;
    }
    std::memcpy(encoded_.data(), packet + kHeaderBytes, payload_bytes);
    esp_audio_dec_in_raw_t raw{};
    raw.buffer = encoded_.data(); raw.len = static_cast<uint32_t>(payload_bytes);
    esp_audio_dec_out_frame_t output{};
    output.buffer = reinterpret_cast<uint8_t *>(pcm_.data()); output.len = sizeof(pcm_);
    esp_audio_dec_info_t info{};
    const auto decoded = esp_opus_dec_decode(decoder_, &raw, &output, &info);
    if (decoded != ESP_AUDIO_ERR_OK || raw.consumed != payload_bytes || output.decoded_size != sizeof(pcm_) ||
        info.sample_rate != 24000 || info.channel != 1 || info.bits_per_sample != 16) {
        failed_ = true;
        ESP_LOGE(kTag, "decode rejected result=%d consumed=%lu pcm_bytes=%lu", decoded,
                 static_cast<unsigned long>(raw.consumed), static_cast<unsigned long>(output.decoded_size));
        return ESP_FAIL;
    }
    if (header.valid) {
        const auto result = sink_(pcm_.data() + header.trim, header.valid, false, context_);
        if (result != ESP_OK) { failed_ = true; return result; }
    }
    ++sequence_; position_ += header.valid;
    return ESP_OK;
}
}
