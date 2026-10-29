#pragma once
#include <cstdint>

namespace puls {

// Musical time is integer "units". 1 quarter note = 3840 ticks (PPQ) and 1 tick = 268800 units.
// 268800 = 2^9 * 3 * 5^2 * 7, so every step length, ratchet subdivision (1..8), and percentage (/100)
// used by the engine is an exact integer. Floating point only appears when converting to sample frames.
constexpr int64_t kPPQ            = 3840;
constexpr int64_t kUnitsPerTick   = 268800;
constexpr int64_t kUnitsPerQuarter = kPPQ * kUnitsPerTick;

enum class Resolution : uint8_t {
    Quarter,          // 1/4
    Eighth,           // 1/8
    Sixteenth,        // 1/16
    ThirtySecond,     // 1/32
    SixtyFourth,      // 1/64
    EighthTriplet,    // 1/8T
    SixteenthTriplet, // 1/16T
    ThirtySecondTriplet, // 1/32T
    Count
};

constexpr int64_t ticksPerStep(Resolution r) noexcept
{
    switch (r) {
        case Resolution::Quarter:             return kPPQ;        // 3840
        case Resolution::Eighth:              return kPPQ / 2;    // 1920
        case Resolution::Sixteenth:           return kPPQ / 4;    // 960
        case Resolution::ThirtySecond:        return kPPQ / 8;    // 480
        case Resolution::SixtyFourth:         return kPPQ / 16;   // 240
        case Resolution::EighthTriplet:       return kPPQ / 3;    // 1280
        case Resolution::SixteenthTriplet:    return kPPQ / 6;    // 640
        case Resolution::ThirtySecondTriplet: return kPPQ / 12;   // 320
        default:                              return kPPQ / 4;
    }
}

constexpr int64_t unitsPerStep(Resolution r) noexcept { return ticksPerStep(r) * kUnitsPerTick; }

static_assert(kPPQ % 12 == 0 && kPPQ % 16 == 0, "all resolutions must be whole ticks");
static_assert(kUnitsPerTick % 840 == 0 && kUnitsPerTick % 100 == 0, "ratchets and percentages must be exact");

} // namespace puls
