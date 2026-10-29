#pragma once
#include "Track.hpp"

namespace puls {

struct Pattern {
    char     name[16] = {};
    uint32_t seed = 1;          // drives deterministic probability rolls
    uint8_t  trackCount = 0;    // 0..16
    Track    tracks[kMaxTracks];
};

} // namespace puls
