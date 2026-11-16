#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace native_opus {
constexpr size_t kHeaderBytes = 24;
constexpr size_t kMaxPayloadBytes = 1275;
constexpr uint16_t kReset = 1;
constexpr uint16_t kEnd = 2;
constexpr uint16_t kCancel = 4;
constexpr uint16_t kInputFrameSamples = 320;
constexpr uint16_t kOutputFrameSamples = 480;

struct Header {
    uint32_t stream;
    uint32_t sequence;
    uint32_t position;
    uint16_t decoded;
    uint16_t trim;
    uint16_t valid;
    uint16_t flags;
};

inline uint16_t Read16(const uint8_t *data) { return static_cast<uint16_t>((data[0] << 8) | data[1]); }
inline uint32_t Read32(const uint8_t *data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | data[3];
}
inline void Write16(uint8_t *data, uint16_t value) {
    data[0] = static_cast<uint8_t>(value >> 8); data[1] = static_cast<uint8_t>(value);
}
inline void Write32(uint8_t *data, uint32_t value) {
    data[0] = static_cast<uint8_t>(value >> 24); data[1] = static_cast<uint8_t>(value >> 16);
    data[2] = static_cast<uint8_t>(value >> 8); data[3] = static_cast<uint8_t>(value);
}

inline bool ValidHeader(const Header &header, size_t payload_bytes, uint16_t frame_samples) {
    if (!header.stream || (header.flags & ~(kReset | kEnd | kCancel))) return false;
    if (header.flags & (kEnd | kCancel)) {
        return (header.flags == kEnd || header.flags == kCancel) && !payload_bytes && !header.decoded && !header.trim && !header.valid;
    }
    if (!payload_bytes || payload_bytes > kMaxPayloadBytes || header.decoded != frame_samples ||
        header.trim > frame_samples || header.valid > frame_samples - header.trim) return false;
    return !(header.flags & kReset) || (!header.sequence && !header.position);
}

inline bool ReadHeader(const uint8_t *data, size_t length, uint16_t frame_samples, Header *header) {
    if (!data || !header || length < kHeaderBytes || length > kHeaderBytes + kMaxPayloadBytes ||
        std::memcmp(data, "XYO1", 4) != 0) return false;
    *header = {Read32(data + 4), Read32(data + 8), Read32(data + 12), Read16(data + 16),
               Read16(data + 18), Read16(data + 20), Read16(data + 22)};
    return ValidHeader(*header, length - kHeaderBytes, frame_samples);
}

inline bool WriteHeader(const Header &header, size_t payload_bytes, uint16_t frame_samples,
                        uint8_t *data, size_t capacity) {
    if (!data || capacity < kHeaderBytes + payload_bytes || !ValidHeader(header, payload_bytes, frame_samples)) return false;
    std::memcpy(data, "XYO1", 4);
    Write32(data + 4, header.stream); Write32(data + 8, header.sequence); Write32(data + 12, header.position);
    Write16(data + 16, header.decoded); Write16(data + 18, header.trim);
    Write16(data + 20, header.valid); Write16(data + 22, header.flags);
    return true;
}

inline bool PacketIs20msMono(const uint8_t *data, size_t length, uint32_t rate) {
    if (!data || !length || length > kMaxPayloadBytes || (data[0] & 4) || (rate != 16000 && rate != 24000)) return false;
    const uint8_t configuration = data[0] >> 3;
    uint32_t samples;
    if (configuration < 12) {
        constexpr uint32_t durations[] = {10, 20, 40, 60};
        samples = rate * durations[configuration & 3] / 1000;
    } else if (configuration < 16) {
        samples = rate * ((configuration & 1) ? 20 : 10) / 1000;
    } else {
        samples = rate * (1U << (configuration & 3)) / 400;
    }
    const uint8_t code = data[0] & 3;
    if (code == 3 && length < 2) return false;
    const uint32_t frames = code == 0 ? 1 : code == 3 ? (data[1] & 63) : 2;
    return frames && samples * frames == rate / 50;
}
}
