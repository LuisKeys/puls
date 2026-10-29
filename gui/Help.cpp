#include "Help.hpp"
#include <algorithm>

namespace puls::gui {

namespace {

const HelpRow kSteps[] = {
    {"Click", "Toggle a step"},
    {"Toolbar \xE2\x99\xAA", "Preview: play a track's note when one of its steps is switched on (click to turn off)"},
    {"Drag up / down on an active step", "Set its velocity (brighter = louder)"},
    {"Right-click or Ctrl+click", "Step menu: probability, ratchets, micro-timing, gate, clear"},
    {"Markers", "Number = ratchets, dashed top = probability < 100 %, side bar = early / late"},
};

const HelpRow kTracks[] = {
    {"Click a label", "Select the track (toolbar Res / Swing act on it)"},
    {"M / S", "Mute / solo"},
    {"Length box", "Steps before the track loops (1-64): drag up / down or scroll, or click and type. "
                   "Different lengths make polyrhythms"},
    {"Right-click a label", "Name, color, MIDI note, channel, resolution, length, phase, swing, scaling, mute, solo"},
    {"Piano button", "Note keyboard (toolbar, or next to MIDI note in the track menu): pick a track, click a key to set "
                     "its note; the key always plays it. A key another track uses (same channel) only plays: one track per note. Dots in the track colors mark each track's note; \xE2\x80\xB9 \xE2\x80\xBA or the "
                     "wheel change octave, Up/Down pick the track, Left/Right move its note, Esc closes"},
    {"Toolbar + / \xE2\x88\x92", "Add a track / remove the selected one (asks first)"},
};

const HelpRow kPatterns[] = {
    {"Click a slot (1-16)", "View and play it; while playing the switch waits for the next bar (see \xE2\x9A\x99)"},
    {"Alt+click a slot", "Switch immediately"},
    {"Right-click a slot", "Rename, play now, copy, paste, duplicate, clear"},
    {"Green dot", "The pattern that is actually playing"},
    {"Kit \xE2\x96\xBE (bank bar)", "Templates: load one by name to replace this pattern's tracks (names, notes, colors, lengths; "
                                   "steps start empty, asks first if there are steps; undo reverts it). The last row saves this "
                                   "pattern's tracks as your own template, for any project; \xC3\x97 deletes one of yours"},
};

const HelpRow kMenus[] = {
    {"Sliders", "Drag, or scroll over them; one drag is one undo step"},
    {"Numbers and names", "Click to type; Enter confirms, Esc cancels, clicking elsewhere confirms"},
    {"Outside the menu", "A click closes it"},
};

const HelpRow kKeyboard[] = {
    {"Arrows / Shift+arrows", "Move the cursor / extend the selection (Shift+click also extends)"},
    {"Space or Enter", "Toggle the selected steps"},
    {"Delete or Backspace", "Clear the selected steps"},
    {"Ctrl/Cmd + C, X, V", "Copy, cut, paste a block of steps at the selection"},
    {"Ctrl/Cmd + Z, Shift+Z (or Y)", "Undo / redo"},
    {"Esc / ?", "Collapse the selection / open or close this help"},
};

const HelpRow kTips[] = {
    {"Where to put it", "On a track before an instrument (note FX); it only makes notes while the host plays"},
    {"Polyrhythm", "Give tracks different lengths or resolutions (e.g. 16 steps vs 12 steps vs 1/8T)"},
    {"Groove", "Swing delays odd steps; micro-timing nudges single steps by up to \xC2\xB1" "50 %"},
    {"Phase", "Phase shifts where in its loop a track starts"},
    {"Closing the window", "Never interrupts playback; the patterns are saved with the project"},
};

template <size_t N> constexpr int countOf(const HelpRow (&)[N]) { return static_cast<int>(N); }

} // namespace

const HelpSection kHelpSections[] = {
    {"Steps", kSteps, countOf(kSteps)},
    {"Tracks", kTracks, countOf(kTracks)},
    {"Patterns", kPatterns, countOf(kPatterns)},
    {"Menus", kMenus, countOf(kMenus)},
    {"Keyboard (when the plugin window has focus)", kKeyboard, countOf(kKeyboard)},
    {"Tips", kTips, countOf(kTips)},
};
const int kHelpSectionCount = static_cast<int>(sizeof(kHelpSections) / sizeof(kHelpSections[0]));

void HelpLayout::update(float winW, float winH) noexcept
{
    const float w = std::min(kMaxW, winW - 2 * kMargin);
    const float h = winH - 2 * kMargin;
    panel = {(winW - w) * 0.5f, kMargin, w, h};
    header = {panel.x, panel.y, w, kHeaderH};
    close = {panel.right() - 8 - 26, panel.y + 5, 26, 24};
    body = {panel.x + kPad, header.bottom(), w - 2 * kPad, h - kHeaderH - 8};
    scrollBy(0);
}

HelpHit HelpLayout::hitTest(float x, float y) const noexcept
{
    if (close.contains(x, y)) return HelpHit::Close;
    return panel.contains(x, y) ? HelpHit::Panel : HelpHit::Outside;
}

void HelpLayout::scrollBy(float dy) noexcept
{
    scroll = std::clamp(scroll + dy, 0.0f, std::max(0.0f, contentH_ - body.h));
}

} // namespace puls::gui
