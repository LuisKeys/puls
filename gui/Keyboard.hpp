#pragma once
#include "EditorLayout.hpp"
#include "vs/ui/Notes.hpp"
#include "vs/ui/PianoGeometry.hpp"
#include <string>

namespace puls::gui {

using vs::ui::isBlackKey;                // moved to vs-ui
using vs::ui::noteName;                  // 60 = "C3" (Bitwig, Ableton and Cubase naming), 37 = "C#1"
const char* gmDrumName(int note);        // General MIDI percussion (35..81), nullptr for other notes

enum class KeysPart : uint8_t { Outside, Panel, Close, Prev, Next, Track, Key };

struct KeysHit {
    KeysPart part = KeysPart::Outside;
    int      index = -1;   // Track: track index, Key: MIDI note
};

// Geometry of the note keyboard overlay: a centered panel with a header, one chip per track (two rows of 8 at
// most), an info row with octave buttons and a piano that shows whole octaves of the 0..127 range. The piano
// itself (key rects, octave scroll, markers, `area`, `octaves`, `firstOctave`) is vs::ui::PianoGeometry.
class KeyboardLayout : public vs::ui::PianoGeometry {
public:
    static constexpr float kMaxW = 980, kMargin = 16, kHeaderH = 32, kPad = 12, kChipH = 22, kChipGap = 4;
    static constexpr float kInfoH = 22, kNavW = 26, kMinWhiteW = kDefaultMinWhiteW, kMaxKeysH = 170;
    static constexpr int   kChipsPerRow = 8;

    void    update(float winW, float winH, int trackCount) noexcept;
    KeysHit hitTest(float x, float y) const noexcept;

    Rect chip(int t) const noexcept;

    Rect  panel, header, close, info, prev, next;
    int   trackCount = 0;
};

} // namespace puls::gui
