#pragma once
#include "DefaultPattern.hpp"
#include "Snapshot.hpp"
#include "UndoStack.hpp"

namespace puls {

// Authoritative, editable state. Main thread only; the audio thread only ever sees EngineSnapshot copies.
struct Model {
    Pattern  pattern;
    Settings settings;
    uint32_t nextTrackId = 1;
    UndoStack history;   // not persisted; a restored project starts with empty history

    Track* findTrack(uint32_t id) noexcept
    {
        for (int i = 0; i < pattern.trackCount; ++i)
            if (pattern.tracks[i].id == id) return &pattern.tracks[i];
        return nullptr;
    }
};

inline Model makeDefaultModel()
{
    Model m;
    makeDefaultPattern(m.pattern);
    m.nextTrackId = static_cast<uint32_t>(m.pattern.trackCount) + 1;
    return m;
}

} // namespace puls
