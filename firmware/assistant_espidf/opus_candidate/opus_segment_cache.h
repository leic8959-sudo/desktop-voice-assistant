#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "opus_wire.h"

namespace native_opus {
// A single capture owner appends; freeze before a sender reads. All ownership
// transitions must be serialized by the integrating protocol, not by this cache.
template <size_t SegmentCount, size_t PacketsPerSegment, size_t PayloadCapacity, uint32_t RetainSamples>
class SegmentCache {
    static_assert(SegmentCount >= 2 && PacketsPerSegment >= 2);
    static_assert(PayloadCapacity > 0 && PayloadCapacity <= kMaxPayloadBytes);
public:
    using Sink = bool (*)(const uint8_t *packet, size_t length, void *context);
    struct Packet {
        uint16_t length;
        std::array<uint8_t, kHeaderBytes + PayloadCapacity> data;
    };
    struct Segment {
        std::array<Packet, PacketsPerSegment> packets;
        size_t count = 0;
        uint32_t stream = 0;
        uint32_t samples = 0;
        bool closed = false;
    };

    void Reset(uint32_t generation) {
        generation_ = generation; first_ = count_ = 0; samples_ = 0;
        last_stream_ = 0; active_ = frozen_ = false;
        for (auto &segment : segments_) { segment.count = 0; segment.samples = 0; segment.closed = false; }
    }

    bool Append(uint32_t generation, const uint8_t *data, size_t length) {
        Header header{};
        if (generation != generation_ || frozen_ || length > kHeaderBytes + PayloadCapacity ||
            !ReadHeader(data, length, kInputFrameSamples, &header)) return false;
        // Cancelled speech is not a reusable prefix. The owner must discard
        // this generation rather than replaying a partially cancelled segment.
        if (header.flags & kCancel) return false;
        if (!(header.flags & kEnd) && !PacketIs20msMono(data + kHeaderBytes, length - kHeaderBytes, 16000)) return false;
        if (!active_) {
            if (header.flags != kReset || header.stream <= last_stream_) return false;
            if (count_ == SegmentCount) {
                const auto &oldest = segments_[first_];
                // Evict only a complete independent segment, and keep the full
                // requested prefix duration. Otherwise stop instead of losing speech.
                if (!oldest.closed || samples_ - oldest.samples < RetainSamples) return false;
                samples_ -= oldest.samples; first_ = (first_ + 1) % SegmentCount; --count_;
            }
            auto &next = segments_[(first_ + count_) % SegmentCount];
            next.count = 0; next.samples = 0; next.closed = false; next.stream = header.stream;
            ++count_; active_ = true; last_stream_ = header.stream;
        }
        auto &segment = segments_[(first_ + count_ - 1) % SegmentCount];
        if (header.stream != segment.stream || header.sequence != segment.count || header.position != segment.samples ||
            (segment.count && (header.flags & kReset)) || segment.count == PacketsPerSegment ||
            header.valid > std::numeric_limits<uint32_t>::max() - samples_) return false;
        auto &packet = segment.packets[segment.count];
        packet.length = static_cast<uint16_t>(length);
        std::memcpy(packet.data.data(), data, length);
        ++segment.count; segment.samples += header.valid; samples_ += header.valid;
        if (header.flags & kEnd) { segment.closed = true; active_ = false; }
        return true;
    }

    bool Freeze(uint32_t generation) {
        if (generation != generation_ || active_ || !count_) return false;
        frozen_ = true;
        return true;
    }

    const Segment *At(uint32_t generation, size_t index) const {
        if (generation != generation_ || !frozen_ || index >= count_) return nullptr;
        return &segments_[(first_ + index) % SegmentCount];
    }

    bool ReplayTail(uint32_t generation, uint32_t retained_samples, Sink sink, void *context) const {
        if (!sink || generation != generation_ || !frozen_ || !retained_samples || retained_samples > samples_) return false;
        uint32_t skip = samples_ - retained_samples;
        Packet wire{};
        for (size_t index = 0; index < count_; ++index) {
            const auto &segment = segments_[(first_ + index) % SegmentCount];
            if (skip >= segment.samples) { skip -= segment.samples; continue; }
            uint32_t position = 0;
            for (size_t packet_index = 0; packet_index < segment.count; ++packet_index) {
                wire = segment.packets[packet_index];
                Header header{};
                if (!ReadHeader(wire.data.data(), wire.length, kInputFrameSamples, &header)) return false;
                if (!(header.flags & kEnd)) {
                    const uint16_t discarded = static_cast<uint16_t>(skip < header.valid ? skip : header.valid);
                    header.trim += discarded; header.valid -= discarded; skip -= discarded;
                }
                // Preserve every bootstrap packet from RESET, but exclude its
                // discarded PCM from the wake_clip audio_bytes accounting.
                header.position = position; position += header.valid;
                if (!WriteHeader(header, wire.length - kHeaderBytes, kInputFrameSamples, wire.data.data(), wire.data.size()) ||
                    !sink(wire.data.data(), wire.length, context)) return false;
            }
        }
        return true;
    }

    size_t Count() const { return count_; }
    uint32_t Samples() const { return samples_; }

private:
    std::array<Segment, SegmentCount> segments_{};
    size_t first_ = 0, count_ = 0;
    uint32_t generation_ = 0, last_stream_ = 0, samples_ = 0;
    bool active_ = false, frozen_ = false;
};
}
