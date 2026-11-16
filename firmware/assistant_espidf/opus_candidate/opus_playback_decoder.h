#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "opus_wire.h"

namespace native_opus {
// One playback task owns all decoder calls. The WS callback queues compressed
// packets with an immutable playback generation; it never calls this decoder.
class PlaybackDecoder {
public:
    using Sink = esp_err_t (*)(const int16_t *samples, size_t count, bool ended, void *context);
    PlaybackDecoder() = default;
    PlaybackDecoder(const PlaybackDecoder &) = delete;
    PlaybackDecoder &operator=(const PlaybackDecoder &) = delete;
    ~PlaybackDecoder();
    esp_err_t Initialize(Sink sink, void *context);
    esp_err_t BeginPlayback(uint32_t generation);
    esp_err_t Decode(uint32_t generation, const uint8_t *packet, size_t length);
    void Abort();

private:
    void *decoder_ = nullptr;
    Sink sink_ = nullptr;
    void *context_ = nullptr;
    uint32_t generation_ = 0, stream_ = 0, sequence_ = 0, position_ = 0;
    bool have_playback_ = false, active_ = false, failed_ = false;
    alignas(16) std::array<uint8_t, kMaxPayloadBytes> encoded_{};
    alignas(16) std::array<int16_t, kOutputFrameSamples> pcm_{};
};
}
