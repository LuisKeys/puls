#pragma once

namespace puls {

struct TransportInfo {
    bool   playing = false;
    double quarterPos = 0.0;   // host position of the first sample of the block, in quarter notes
    double bpm = 120.0;
    double sampleRate = 44100.0;
};

enum class AnchorMode {
    // Step grids are anchored where playback starts (and re-anchored after a position jump / loop wrap),
    // so every track restarts from its phase offset together. Spec default.
    RestartOnStart,
    // Step grids are anchored to host position 0: tracks keep their phase relationship to the song.
    HostAbsolute,
};

} // namespace puls
