#include "Keyboard.hpp"
#include <algorithm>
#include <cmath>

namespace puls::gui {

namespace {

constexpr const char* kGmDrums[] = {
    "Acoustic Bass Drum", "Bass Drum 1", "Side Stick", "Acoustic Snare", "Hand Clap", "Electric Snare",
    "Low Floor Tom", "Closed Hi-Hat", "High Floor Tom", "Pedal Hi-Hat", "Low Tom", "Open Hi-Hat",
    "Low-Mid Tom", "Hi-Mid Tom", "Crash Cymbal 1", "High Tom", "Ride Cymbal 1", "Chinese Cymbal",
    "Ride Bell", "Tambourine", "Splash Cymbal", "Cowbell", "Crash Cymbal 2", "Vibraslap",
    "Ride Cymbal 2", "Hi Bongo", "Low Bongo", "Mute Hi Conga", "Open Hi Conga", "Low Conga",
    "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa", "Maracas",
    "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "Hi Wood Block",
    "Low Wood Block", "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle",
};
constexpr int kGmFirst = 35;
constexpr int kGmCount = static_cast<int>(sizeof(kGmDrums) / sizeof(kGmDrums[0]));

} // namespace

const char* gmDrumName(int note)
{
    return note >= kGmFirst && note < kGmFirst + kGmCount ? kGmDrums[note - kGmFirst] : nullptr;
}

void KeyboardLayout::update(float winW, float winH, int tracks) noexcept
{
    trackCount = std::max(0, tracks);
    const int rows = std::max(1, (trackCount + kChipsPerRow - 1) / kChipsPerRow);
    const float chipsH = rows * kChipH + (rows - 1) * kChipGap;
    const float fixed = kHeaderH + chipsH + 8 + kInfoH + 6 + kPad;
    const float w = std::min(kMaxW, winW - 2 * kMargin);
    const float h = std::min(winH - 2 * kMargin, fixed + kMaxKeysH);
    panel = {(winW - w) * 0.5f, std::max(kMargin, (winH - h) * 0.5f), w, h};
    header = {panel.x, panel.y, w, kHeaderH};
    close = {panel.right() - 8 - 26, panel.y + 4, 26, 24};

    const float x = panel.x + kPad, iw = w - 2 * kPad;
    const float infoY = header.bottom() + chipsH + 8;
    next = {x + iw - kNavW, infoY, kNavW, kInfoH};
    prev = {next.x - 4 - kNavW, infoY, kNavW, kInfoH};
    info = {x, infoY, prev.x - 8 - x, kInfoH};
    setArea({x, info.bottom() + 6, iw, std::max(0.0f, panel.bottom() - kPad - (info.bottom() + 6))}, kMinWhiteW);
}

Rect KeyboardLayout::chip(int t) const noexcept
{
    const float w = (area.w - (kChipsPerRow - 1) * kChipGap) / kChipsPerRow;
    return {area.x + (t % kChipsPerRow) * (w + kChipGap), header.bottom() + (t / kChipsPerRow) * (kChipH + kChipGap), w, kChipH};
}

KeysHit KeyboardLayout::hitTest(float x, float y) const noexcept
{
    if (!panel.contains(x, y)) return {};
    if (close.contains(x, y)) return {KeysPart::Close};
    if (prev.contains(x, y)) return {KeysPart::Prev};
    if (next.contains(x, y)) return {KeysPart::Next};
    for (int t = 0; t < trackCount; ++t)
        if (chip(t).contains(x, y)) return {KeysPart::Track, t};
    if (const int n = noteAt(x, y); n >= 0) return {KeysPart::Key, n};   // black keys lie on top of the white ones
    return {KeysPart::Panel};
}

} // namespace puls::gui
