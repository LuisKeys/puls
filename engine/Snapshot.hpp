#pragma once
#include "Pattern.hpp"
#include "PatternSwitcher.hpp"
#include "Transport.hpp"
#include <array>

namespace puls {

constexpr int kBankSize = 16;

struct Settings {
    AnchorMode     anchorMode = AnchorMode::RestartOnStart;
    bool           enabled = true;                          // false: the sequencer stays silent (sounding notes are released)
    SwitchQuantize patternSwitch = SwitchQuantize::Bar;     // pattern switches while playing; stopped switches are immediate
    bool           audition = true;                         // switching a step on in the editor plays its note once
};

// Immutable (from the audio thread's point of view) copy of everything the engine reads. POD, fixed size.
struct EngineSnapshot {
    std::array<Pattern, kBankSize> patterns;
    Settings settings;
};

} // namespace puls
