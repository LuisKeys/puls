#pragma once
#include "Pattern.hpp"
#include "Transport.hpp"

namespace puls {

struct Settings {
    AnchorMode anchorMode = AnchorMode::RestartOnStart;
    bool       enabled = true;   // false: the sequencer stays silent (sounding notes are released)
};

// Immutable (from the audio thread's point of view) copy of everything the engine reads. POD, fixed size.
struct EngineSnapshot {
    Pattern  pattern;
    Settings settings;
};

} // namespace puls
