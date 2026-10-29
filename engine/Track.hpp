#pragma once
#include "Step.hpp"
#include "Timing.hpp"

namespace puls {

constexpr int kMaxSteps  = 64;
constexpr int kMaxTracks = 16;

struct Track {
    uint32_t   id = 0;                       // stable, unique within a pattern
    char       name[16] = {};
    uint32_t   color = 0xFFFFFF;             // 0xRRGGBB
    uint8_t    note = 36;                    // 0..127
    uint8_t    channel = 0;                  // 0..15
    uint8_t    length = 16;                  // 1..64
    Resolution resolution = Resolution::Sixteenth;
    uint8_t    phase = 0;                    // playback phase offset in steps
    uint8_t    velocityPct = 100;            // 0..200 multiplier on step velocity
    uint8_t    probabilityPct = 100;         // 0..100 multiplier on step probability
    uint8_t    swing = 0;                    // 0..75: odd steps are delayed by this % of a step
    uint8_t    durationPct = 100;            // 1..100 multiplier on step gate
    bool       mute = false;
    bool       solo = false;
    Step       steps[kMaxSteps];
};

} // namespace puls
