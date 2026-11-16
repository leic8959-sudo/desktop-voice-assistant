#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "opus_wire.h"

namespace native_opus {
class CaptureEncoder {
public:
    using Sink = esp_err_t (*)(const uint8_t *packet, size_t length, void *context);
    CaptureEncoder() = default;
    CaptureEncoder(const CaptureEncoder &) = delete;
    CaptureEncoder &operator=(const CaptureEncoder &) = delete;
    ~CaptureEncoder();

    // Delay must come from the exact encoder implementation's verified
    // lookahead in16kHz samples. No zero/default guess is supplied here.
    esp_err_t Initialize(uint16_t delay_samples, Sink sink, void *context);
    esp_err_t Begin(uint32_t stream);
    esp_err_t Encode(const int16_t *samples, size_t count);
    esp_err_t End(const int16_t *tail = nullptr, size_t count = 0);
    esp_err_t Abort();

private:
    esp_err_t Emit(const int16_t *samples, size_t count);
    void *encoder_ = nullptr;
    Sink sink_ = nullptr;
    void *context_ = nullptr;
    uint16_t delay_ = 0;
    uint32_t stream_ = 0, sequence_ = 0, position_ = 0, input_samples_ = 0;
    int64_t packet_start_ = 0;
    bool active_ = false, failed_ = false;
    alignas(16) std::array<int16_t, kInputFrameSamples> pcm_{};
    alignas(16) std::array<uint8_t, kMaxPayloadBytes> encoded_{};
    std::array<uint8_t, kHeaderBytes + 128> wire_{};
};
}
