#pragma once
#include <stdint.h>

enum class NotificationCue : uint8_t { Timer, Reminder, Error, Count };
struct CueNote { uint16_t frequency_hz; uint16_t duration_ms; uint16_t gap_ms; };
struct CueSpec { CueNote notes[3]; uint8_t note_count; uint32_t cooldown_ms; uint32_t expires_ms; };

static constexpr uint32_t kCueRate = 24000;
static constexpr uint32_t kCueGlobalGapMs = 8000;
static constexpr int kCueAmplitude = 3000;
static constexpr uint32_t kCueMaxSamples = 9600;
static constexpr CueSpec kCueSpecs[] = {
    {{{784, 90, 20}, {988, 90, 20}, {1175, 140, 0}}, 3, 20000, 60000},
    {{{660, 120, 30}, {880, 150, 0}, {0, 0, 0}}, 2, 20000, 60000},
    {{{392, 70, 25}, {294, 90, 0}, {0, 0, 0}}, 2, 60000, 5000},
};

constexpr uint32_t cue_samples(NotificationCue cue) {
    const CueSpec &spec = kCueSpecs[static_cast<uint8_t>(cue)];
    uint32_t duration = 0;
    for (uint8_t note = 0; note < spec.note_count; ++note) duration += spec.notes[note].duration_ms + spec.notes[note].gap_ms;
    return duration * (kCueRate / 1000);
}

struct CueGate {
    bool has_played = false;
    uint64_t last_ms = 0;
    bool played[3] = {};
    uint64_t last_kind_ms[3] = {};

    constexpr bool allows(NotificationCue cue, uint64_t now_ms) const {
        const uint8_t index = static_cast<uint8_t>(cue);
        if (index >= 3) return false;
        return (!has_played || now_ms - last_ms >= kCueGlobalGapMs) && (!played[index] || now_ms - last_kind_ms[index] >= kCueSpecs[index].cooldown_ms);
    }

    constexpr void record(NotificationCue cue, uint64_t now_ms) {
        const uint8_t index = static_cast<uint8_t>(cue);
        has_played = true; last_ms = now_ms; played[index] = true; last_kind_ms[index] = now_ms;
    }
};

static_assert(cue_samples(NotificationCue::Timer) <= kCueMaxSamples, "timer cue exceeds fixed buffer");
static_assert(cue_samples(NotificationCue::Reminder) <= kCueMaxSamples, "reminder cue exceeds fixed buffer");
static_assert(cue_samples(NotificationCue::Error) <= kCueMaxSamples, "error cue exceeds fixed buffer");
