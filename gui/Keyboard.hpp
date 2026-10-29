#pragma once
#include "EditorLayout.hpp"
#include <string>

namespace puls::gui {

std::string noteName(int note);          // 60 = "C3" (Bitwig, Ableton and Cubase naming), 37 = "C#1"
const char* gmDrumName(int note);        // General MIDI percussion (35..81), nullptr for other notes
bool        isBlackKey(int note) noexcept;

enum class KeysPart : uint8_t { Outside, Panel, Close, Prev, Next, Track, Key };

struct KeysHit {
    KeysPart part = KeysPart::Outside;
    int      index = -1;   // Track: track index, Key: MIDI note
};

// Geometry of the note keyboard overlay: a centered panel with a header, one chip per track (two rows of 8 at
// most), an info row with octave buttons and a piano that shows whole octaves of the 0..127 range.
class KeyboardLayout {
public:
    static constexpr float kMaxW = 980, kMargin = 16, kHeaderH = 32, kPad = 12, kChipH = 22, kChipGap = 4;
    static constexpr float kInfoH = 22, kNavW = 26, kMinWhiteW = 22, kMaxKeysH = 170;
    static constexpr int   kChipsPerRow = 8, kOctaves = 11;   // note 127 is in octave 10 (C..G only)

    void    update(float winW, float winH, int trackCount) noexcept;
    KeysHit hitTest(float x, float y) const noexcept;
    void    scrollOctaves(int delta) noexcept;
    void    ensureVisible(int note) noexcept;            // scrolls the least needed to show the note's octave

    Rect chip(int t) const noexcept;
    Rect key(int note) const noexcept;                   // empty when the note is out of range or not in view
    // The k-th track dot on a key, stacked upwards from the bottom; empty past markerCapacity.
    Rect marker(int note, int k) const noexcept;
    int  markerCapacity(int note) const noexcept;
    int  maxFirstOctave() const noexcept { return kOctaves - octaves; }

    Rect  panel, header, close, info, prev, next, keys;
    int   trackCount = 0, octaves = 1, firstOctave = 2;
    float whiteW = 0;

private:
    float markerRadius() const noexcept;
};

} // namespace puls::gui
