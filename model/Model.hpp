#pragma once
#include "DefaultPattern.hpp"
#include "Snapshot.hpp"
#include "UndoStack.hpp"

namespace puls {

// Authoritative, editable state. Main thread only; the audio thread only ever sees EngineSnapshot copies.
// `selected` is the pattern being viewed/edited; which one is *playing* is decided on the audio thread
// (a switch can be quantized) and reported back through telemetry.
struct Model {
    std::array<Pattern, kBankSize> bank;
    int      selected = 0;
    Settings settings;
    uint32_t nextTrackId = 1;   // global: ids are unique per pattern, never reused within a session
    UndoStack history;          // not persisted; a restored project starts with empty history
    Pattern  clipboard;         // pattern clipboard, not persisted
    bool     hasClipboard = false;

    Pattern&       pattern() noexcept       { return bank[selected]; }
    const Pattern& pattern() const noexcept { return bank[selected]; }

    Track* findTrack(uint32_t id) noexcept
    {
        Pattern& p = pattern();
        for (int i = 0; i < p.trackCount; ++i)
            if (p.tracks[i].id == id) return &p.tracks[i];
        return nullptr;
    }
};

// One track per note and channel within a pattern (patterns loaded from older projects may still repeat one).
// The track other than `except` (a track id) that plays `note` on `channel`, or nullptr.
inline const Track* noteOwner(const Pattern& p, int note, int channel, uint32_t except) noexcept
{
    for (int i = 0; i < p.trackCount; ++i) {
        const Track& t = p.tracks[i];
        if (t.id != except && t.note == note && t.channel == channel) return &t;
    }
    return nullptr;
}

// The first note from `from` in direction `dir` (+1 / -1) that no other track uses on `channel`; -1 if none.
inline int freeNote(const Pattern& p, int from, int dir, int channel, uint32_t except) noexcept
{
    for (int n = from; n >= 0 && n <= 127; n += dir)
        if (noteOwner(p, n, channel, except) == nullptr) return n;
    return -1;
}

inline void makeDefaultBank(Model& m)
{
    makeDefaultPattern(m.bank[0]);
    for (int i = 1; i < kBankSize; ++i) makeBlankPattern(m.bank[i], i + 1);
    m.selected = 0;
    m.nextTrackId = static_cast<uint32_t>(m.bank[0].trackCount) + 1;
}

} // namespace puls
