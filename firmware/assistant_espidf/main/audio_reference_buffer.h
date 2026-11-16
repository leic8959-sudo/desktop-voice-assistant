#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// The caller owns synchronization. Each push retains its original time origin
// so floor(sample * 1000000 / rate) is unchanged across partial consumption.
template<size_t Capacity, uint32_t SampleRate, size_t SegmentCapacity>
class AudioReferenceBuffer {
    static_assert(Capacity > 0 && SampleRate > 0 && SegmentCapacity > 0);
    static_assert(SegmentCapacity <= Capacity);

public:
    void clear() {
        read_ = count_ = segment_read_ = segment_count_ = 0;
    }

    size_t size() const { return count_; }

    // Returns false before changing any data if the independent segment bound
    // is reached. Sample-capacity overflow evicts oldest PCM, as before.
    bool push(const int16_t *samples, size_t count, int64_t start_us, bool muted) {
        if (!samples || !count || count > Capacity) return false;
        const size_t discard = count_ + count > Capacity ? count_ + count - Capacity : 0;
        size_t discard_remaining = discard;
        size_t released_segments = 0;
        while (discard_remaining && released_segments < segment_count_) {
            const Segment &segment = segments_[(segment_read_ + released_segments) % SegmentCapacity];
            const size_t remaining = segment.count - segment.consumed;
            if (discard_remaining < remaining) break;
            discard_remaining -= remaining;
            ++released_segments;
        }
        if (segment_count_ - released_segments == SegmentCapacity) return false;
        for (size_t index = 0; index < discard; ++index) pop();
        for (size_t index = 0; index < count; ++index) {
            samples_[(read_ + count_ + index) % Capacity] = muted ? 0 : samples[index];
        }
        segments_[(segment_read_ + segment_count_) % SegmentCapacity] = {start_us, count, 0};
        ++segment_count_;
        count_ += count;
        return true;
    }

    // Only call front/pop when size() > 0.
    int16_t front_sample() const { return samples_[read_]; }
    int64_t front_due_us() const {
        const Segment &segment = segments_[segment_read_];
        return segment.start_us + static_cast<int64_t>(
            (static_cast<uint64_t>(segment.consumed) * 1000000ULL) / SampleRate);
    }

    void pop() {
        read_ = (read_ + 1) % Capacity;
        --count_;
        Segment &segment = segments_[segment_read_];
        if (++segment.consumed == segment.count) {
            segment_read_ = (segment_read_ + 1) % SegmentCapacity;
            --segment_count_;
        }
        if (!count_) clear();
    }

private:
    struct Segment {
        int64_t start_us;
        size_t count;
        size_t consumed;
    };
    std::array<int16_t, Capacity> samples_{};
    std::array<Segment, SegmentCapacity> segments_{};
    size_t read_ = 0, count_ = 0, segment_read_ = 0, segment_count_ = 0;
};
