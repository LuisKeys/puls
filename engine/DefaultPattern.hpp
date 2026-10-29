#pragma once
#include "Pattern.hpp"
#include <cstring>
#include <initializer_list>

namespace puls {

// Spec §4.1 default tracks (editable defaults, not hardcoded mappings) with a basic groove.
inline void makeDefaultPattern(Pattern& p)
{
    struct Def { const char* name; uint8_t note; uint32_t color; };
    static const Def defs[8] = {
        {"Kick", 36, 0xFF6256}, {"Snare", 38, 0xFFAB50}, {"Clap", 39, 0xE3D65C}, {"Closed HH", 42, 0x79D86A},
        {"Open HH", 46, 0x48D9B1}, {"Tom", 45, 0x50BAF1}, {"Perc 1", 50, 0x9C8CF5}, {"Perc 2", 51, 0xE77FCE},
    };
    p = Pattern{};
    std::strncpy(p.name, "Pattern 1", sizeof(p.name) - 1);
    p.trackCount = 8;
    for (int i = 0; i < 8; ++i) {
        Track& t = p.tracks[i];
        t.id = static_cast<uint32_t>(i + 1);
        std::strncpy(t.name, defs[i].name, sizeof(t.name) - 1);
        t.note = defs[i].note;
        t.color = defs[i].color;
    }
    for (int s : {0, 4, 8, 12}) p.tracks[0].steps[s].active = true;
    for (int s : {4, 12}) p.tracks[1].steps[s].active = true;
    for (int s = 0; s < 16; s += 2) { p.tracks[3].steps[s].active = true; p.tracks[3].steps[s].velocity = (s % 4 == 0) ? 110 : 70; }
}

} // namespace puls
