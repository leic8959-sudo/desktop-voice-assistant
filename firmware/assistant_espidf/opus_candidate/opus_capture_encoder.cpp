#include "opus_capture_encoder.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "esp_audio_enc.h"
#include "esp_log.h"
#include "esp_opus_enc.h"

namespace native_opus {
namespace {
constexpr const char *kTag = "opus_capture";
constexpr size_t kCachedPayloadBytes = 128;
}

CaptureEncoder::~CaptureEncoder() {
    if (encoder_) esp_opus_enc_close(encoder_);
}

esp_err_t CaptureEncoder::Initialize(uint16_t delay_samples, Sink sink, void *context) {
    if (encoder_ || !sink || delay_samples >= kInputFrameSamples) return ESP_ERR_INVALID_ARG;
    esp_opus_enc_config_t configuration = ESP_OPUS_ENC_CONFIG_DEFAULT();
    configuration.sample_rate = 16000; configuration.channel = 1; configuration.bits_per_sample = 16;
    configuration.bitrate = 24000; configuration.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    configuration.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP; configuration.complexity = 0;
    configuration.enable_fec = false; configuration.enable_dtx = false; configuration.enable_vbr = false;
    esp_audio_enc_frame_info_t info{};
    auto result = esp_opus_enc_get_frame_info_by_cfg(&configuration, &info);
    if (result != ESP_AUDIO_ERR_OK || info.in_frame_size != sizeof(pcm_) ||
        info.out_frame_size <= 0 || info.out_frame_size > static_cast<int>(encoded_.size()) ||
        info.in_frame_align > 16 || info.out_frame_align > 16) {
        ESP_LOGE(kTag, "frame requirements unsupported result=%d input=%d output=%d", result, info.in_frame_size, info.out_frame_size);
        return ESP_ERR_NOT_SUPPORTED;
    }
    result = esp_opus_enc_open(&configuration, sizeof(configuration), &encoder_);
    if (result != ESP_AUDIO_ERR_OK || !encoder_) {
        if (encoder_) esp_opus_enc_close(encoder_);
        encoder_ = nullptr;
        ESP_LOGE(kTag, "encoder open failed result=%d", result);
        return result == ESP_AUDIO_ERR_MEM_LACK ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
    delay_ = delay_samples; sink_ = sink; context_ = context;
    ESP_LOGI(kTag, "candidate16k-mono20ms24kbps delay_samples=%u runtime_acceptance=not_run", delay_);
    return ESP_OK;
}

esp_err_t CaptureEncoder::Begin(uint32_t stream) {
    if (!encoder_ || active_ || !stream || stream <= stream_) return ESP_ERR_INVALID_STATE;
    const auto result = esp_opus_enc_reset(encoder_);
    if (result != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(kTag, "encoder reset failed result=%d", result);
        return ESP_FAIL;
    }
    stream_ = stream; sequence_ = position_ = input_samples_ = 0;
    packet_start_ = -static_cast<int64_t>(delay_); active_ = true; failed_ = false;
    return ESP_OK;
}

esp_err_t CaptureEncoder::Abort() {
    esp_err_t result = ESP_OK;
    if (active_ && sequence_) {
        const Header header{stream_, sequence_, position_, 0, 0, 0, kCancel};
        if (!WriteHeader(header, 0, kInputFrameSamples, wire_.data(), wire_.size())) result = ESP_ERR_INVALID_SIZE;
        else result = sink_(wire_.data(), kHeaderBytes, context_);
    }
    active_ = false; failed_ = true;
    return result;
}

esp_err_t CaptureEncoder::Emit(const int16_t *samples, size_t count) {
    if (!active_ || failed_ || count > kInputFrameSamples || (count && !samples) ||
        count > std::numeric_limits<uint32_t>::max() - input_samples_) return ESP_ERR_INVALID_STATE;
    pcm_.fill(0);
    if (count) std::memcpy(pcm_.data(), samples, count * sizeof(int16_t));
    input_samples_ += static_cast<uint32_t>(count);
    esp_audio_enc_in_frame_t input{reinterpret_cast<uint8_t *>(pcm_.data()), sizeof(pcm_)};
    esp_audio_enc_out_frame_t output{encoded_.data(), static_cast<uint32_t>(encoded_.size()), 0, 0};
    const auto result = esp_opus_enc_process(encoder_, &input, &output);
    if (result != ESP_AUDIO_ERR_OK || !output.encoded_bytes || output.encoded_bytes > kCachedPayloadBytes ||
        !PacketIs20msMono(encoded_.data(), output.encoded_bytes, 16000)) {
        failed_ = true;
        ESP_LOGE(kTag, "encode rejected result=%d encoded_bytes=%lu", result, static_cast<unsigned long>(output.encoded_bytes));
        return ESP_FAIL;
    }
    const int64_t start = packet_start_;
    packet_start_ += kInputFrameSamples;
    const uint16_t trim = static_cast<uint16_t>(std::min<int64_t>(kInputFrameSamples, std::max<int64_t>(0, -start)));
    const int64_t first = std::max<int64_t>(0, start);
    const uint16_t valid = static_cast<uint16_t>(std::max<int64_t>(0, std::min<int64_t>(start + kInputFrameSamples, input_samples_) - first));
    if (!valid && start >= input_samples_) return ESP_OK;
    if (first != position_ || sequence_ == std::numeric_limits<uint32_t>::max()) { failed_ = true; return ESP_ERR_INVALID_SIZE; }
    const Header header{stream_, sequence_, position_, kInputFrameSamples, trim, valid, static_cast<uint16_t>(sequence_ ? 0 : kReset)};
    if (!WriteHeader(header, output.encoded_bytes, kInputFrameSamples, wire_.data(), wire_.size())) { failed_ = true; return ESP_ERR_INVALID_SIZE; }
    std::memcpy(wire_.data() + kHeaderBytes, encoded_.data(), output.encoded_bytes);
    const auto sent = sink_(wire_.data(), kHeaderBytes + output.encoded_bytes, context_);
    if (sent != ESP_OK) { failed_ = true; return sent; }
    ++sequence_; position_ += valid;
    return ESP_OK;
}

esp_err_t CaptureEncoder::Encode(const int16_t *samples, size_t count) {
    if (count != kInputFrameSamples) return ESP_ERR_INVALID_SIZE;
    return Emit(samples, count);
}

esp_err_t CaptureEncoder::End(const int16_t *tail, size_t count) {
    if (!active_ || failed_ || count >= kInputFrameSamples || (count && !tail)) return ESP_ERR_INVALID_STATE;
    if (count) {
        const auto result = Emit(tail, count);
        if (result != ESP_OK) return result;
    }
    if (input_samples_ && delay_) {
        const auto result = Emit(nullptr, 0);
        if (result != ESP_OK) return result;
    }
    if (position_ != input_samples_) { failed_ = true; return ESP_ERR_INVALID_SIZE; }
    if (sequence_) {
        const Header header{stream_, sequence_, position_, 0, 0, 0, kEnd};
        if (!WriteHeader(header, 0, kInputFrameSamples, wire_.data(), wire_.size())) { failed_ = true; return ESP_ERR_INVALID_SIZE; }
        const auto result = sink_(wire_.data(), kHeaderBytes, context_);
        if (result != ESP_OK) { failed_ = true; return result; }
    }
    active_ = false;
    return ESP_OK;
}
}
