#pragma once
#include "Sequencer.hpp"
#include <cmath>
#include <functional>
#include <vector>

namespace th {
using namespace puls;

struct AbsEvent {
    int64_t sample; uint8_t status, ch, note, vel;
    bool on() const { return status == 0x90; }
    bool operator==(const AbsEvent& o) const { return sample == o.sample && status == o.status && ch == o.ch && note == o.note && vel == o.vel; }
};

inline Pattern emptyPattern(int tracks = 1)
{
    Pattern p;
    p.trackCount = static_cast<uint8_t>(tracks);
    for (int i = 0; i < tracks; ++i) { p.tracks[i].id = i + 1; p.tracks[i].note = static_cast<uint8_t>(36 + i); }
    return p;
}

// All steps active with velocity index+1, so the played velocity identifies the step index.
inline void fillIndexed(Track& t, int length, Resolution r)
{
    t.length = static_cast<uint8_t>(length); t.resolution = r;
    for (int i = 0; i < length; ++i) { t.steps[i].active = true; t.steps[i].velocity = static_cast<uint8_t>(i + 1); }
}

// Simulates a host with constant tempo: block position is recomputed from the sample counter every block.
// Calls `sink` with each absolute event; returns nothing. startSample/endSample in samples from position start.
inline void render(Sequencer& seq, const Pattern& p, int64_t totalSamples, uint32_t block, double bpm, double sr,
                   const std::function<void(const AbsEvent&)>& sink, double startQuarter = 0.0)
{
    for (int64_t s = 0; s < totalSamples; s += block) {
        const uint32_t frames = static_cast<uint32_t>(std::min<int64_t>(block, totalSamples - s));
        TransportInfo tp{true, startQuarter + static_cast<double>(s) * bpm / (60.0 * sr), bpm, sr};
        seq.process(p, tp, frames);
        for (uint32_t i = 0; i < seq.eventCount(); ++i) {
            const MidiMsg& m = seq.events()[i];
            sink(AbsEvent{s + m.frame, static_cast<uint8_t>(m.data[0] & 0xF0), static_cast<uint8_t>(m.data[0] & 0x0F), m.data[1], m.data[2]});
        }
    }
}

inline std::vector<AbsEvent> renderAll(const Pattern& p, int64_t totalSamples, uint32_t block, double bpm = 120.0, double sr = 48000.0,
                                       AnchorMode mode = AnchorMode::RestartOnStart, double startQuarter = 0.0)
{
    Sequencer seq; seq.setAnchorMode(mode);
    std::vector<AbsEvent> out;
    render(seq, p, totalSamples, block, bpm, sr, [&](const AbsEvent& e) { out.push_back(e); }, startQuarter);
    return out;
}

inline std::vector<AbsEvent> onsOf(const std::vector<AbsEvent>& v, int note)
{
    std::vector<AbsEvent> o;
    for (auto& e : v) if (e.on() && e.note == note) o.push_back(e);
    return o;
}
inline std::vector<AbsEvent> offsOf(const std::vector<AbsEvent>& v, int note)
{
    std::vector<AbsEvent> o;
    for (auto& e : v) if (!e.on() && e.note == note) o.push_back(e);
    return o;
}

// Strict per-(channel,note) alternation: never two ons or two offs in a row, offs only after ons.
// Returns the number of notes still sounding at the end.
inline int checkNoteBalance(const std::vector<AbsEvent>& v, bool& valid)
{
    static thread_local bool state[16][128];
    for (auto& r : state) for (auto& b : r) b = false;
    valid = true;
    int sounding = 0;
    for (auto& e : v) {
        bool& s = state[e.ch][e.note];
        if (e.on()) { if (s) valid = false; s = true; ++sounding; }
        else { if (!s) valid = false; s = false; --sounding; }
    }
    return sounding;
}
} // namespace th
