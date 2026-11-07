#include "../firmware/assistant_espidf/main/notification_cues.h"

constexpr bool rate_limits_hold() {
    CueGate gate;
    if (!gate.allows(NotificationCue::Timer, 0)) return false;
    gate.record(NotificationCue::Timer, 0);
    if (gate.allows(NotificationCue::Reminder, 7999)) return false;
    if (!gate.allows(NotificationCue::Reminder, 8000)) return false;
    if (gate.allows(NotificationCue::Timer, 19999)) return false;
    if (!gate.allows(NotificationCue::Timer, 20000)) return false;
    gate.record(NotificationCue::Error, 20000);
    if (gate.allows(NotificationCue::Reminder, 27999)) return false;
    if (!gate.allows(NotificationCue::Reminder, 28000)) return false;
    if (gate.allows(NotificationCue::Error, 79999)) return false;
    if (!gate.allows(NotificationCue::Error, 80000)) return false;
    return !gate.allows(NotificationCue::Count, 80000);
}

static_assert(rate_limits_hold(), "cue cooldown boundaries failed");
static_assert(cue_samples(NotificationCue::Timer) == 8640, "timer cue is 360ms");
static_assert(cue_samples(NotificationCue::Reminder) == 7200, "reminder cue is 300ms");
static_assert(cue_samples(NotificationCue::Error) == 4440, "error cue is 185ms");
static_assert(kCueSpecs[0].notes[0].frequency_hz < kCueSpecs[0].notes[2].frequency_hz, "timer should ascend");
static_assert(kCueSpecs[1].notes[0].frequency_hz < kCueSpecs[1].notes[1].frequency_hz, "reminder should ascend");
static_assert(kCueSpecs[2].notes[0].frequency_hz > kCueSpecs[2].notes[1].frequency_hz, "error should descend");
