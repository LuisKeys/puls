#include "Keyboard.hpp"
#include <algorithm>
#include <cmath>

namespace puls::gui {

namespace {

constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr int  kWhiteIndex[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};   // black keys: the white key on their left
constexpr bool kBlack[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
constexpr float kBlackW = 0.62f, kBlackH = 0.6f;
constexpr float kWhiteLabelH = 14;   // room for the octave name at the bottom of C keys

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

std::string noteName(int note)
{
    if (note < 0 || note > 127) return {};
    return kNames[note % 12] + std::to_string(note / 12 - 2);
}

const char* gmDrumName(int note)
{
    return note >= kGmFirst && note < kGmFirst + kGmCount ? kGmDrums[note - kGmFirst] : nullptr;
}

bool isBlackKey(int note) noexcept { return kBlack[((note % 12) + 12) % 12]; }

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
    keys = {x, info.bottom() + 6, iw, std::max(0.0f, panel.bottom() - kPad - (info.bottom() + 6))};

    octaves = std::clamp(static_cast<int>(keys.w / (7 * kMinWhiteW)), 1, kOctaves);
    whiteW = keys.w / (7 * octaves);
    scrollOctaves(0);
}

void KeyboardLayout::scrollOctaves(int delta) noexcept
{
    firstOctave = std::clamp(firstOctave + delta, 0, maxFirstOctave());
}

void KeyboardLayout::ensureVisible(int note) noexcept
{
    const int o = std::clamp(note, 0, 127) / 12;
    if (o < firstOctave) firstOctave = o;
    else if (o >= firstOctave + octaves) firstOctave = o - octaves + 1;
    scrollOctaves(0);
}

Rect KeyboardLayout::chip(int t) const noexcept
{
    const float w = (keys.w - (kChipsPerRow - 1) * kChipGap) / kChipsPerRow;
    return {keys.x + (t % kChipsPerRow) * (w + kChipGap), header.bottom() + (t / kChipsPerRow) * (kChipH + kChipGap), w, kChipH};
}

Rect KeyboardLayout::key(int note) const noexcept
{
    if (note < 0 || note > 127) return {};
    const int o = note / 12 - firstOctave, pc = note % 12;
    if (o < 0 || o >= octaves) return {};
    const float left = keys.x + (o * 7 + kWhiteIndex[pc]) * whiteW;
    if (!kBlack[pc]) return {left, keys.y, whiteW, keys.h};
    const float bw = whiteW * kBlackW;
    return {left + whiteW - bw * 0.5f, keys.y, bw, keys.h * kBlackH};
}

float KeyboardLayout::markerRadius() const noexcept { return std::min(7.0f, whiteW * kBlackW * 0.4f); }

int KeyboardLayout::markerCapacity(int note) const noexcept
{
    const Rect k = key(note);
    if (k.w <= 0) return 0;
    const float r = markerRadius();
    // White keys keep their black neighbours' area and the octave label free.
    const float room = isBlackKey(note) ? k.h - 6 : k.h * (1 - kBlackH) - kWhiteLabelH - 4;
    return std::max(1, static_cast<int>((room + 2) / (2 * r + 2)));
}

Rect KeyboardLayout::marker(int note, int k) const noexcept
{
    if (k < 0 || k >= markerCapacity(note)) return {};
    const Rect kr = key(note);
    const float r = markerRadius();
    const float bottom = kr.bottom() - (isBlackKey(note) ? 4 : kWhiteLabelH + 2);
    const float cy = bottom - r - k * (2 * r + 2);
    return {kr.cx() - r, cy - r, 2 * r, 2 * r};
}

KeysHit KeyboardLayout::hitTest(float x, float y) const noexcept
{
    if (!panel.contains(x, y)) return {};
    if (close.contains(x, y)) return {KeysPart::Close};
    if (prev.contains(x, y)) return {KeysPart::Prev};
    if (next.contains(x, y)) return {KeysPart::Next};
    for (int t = 0; t < trackCount; ++t)
        if (chip(t).contains(x, y)) return {KeysPart::Track, t};
    if (keys.contains(x, y)) {
        const int first = firstOctave * 12, last = std::min(127, (firstOctave + octaves) * 12 - 1);
        for (int n = first; n <= last; ++n)   // black keys lie on top of the white ones
            if (isBlackKey(n) && key(n).contains(x, y)) return {KeysPart::Key, n};
        for (int n = first; n <= last; ++n)
            if (!isBlackKey(n) && key(n).contains(x, y)) return {KeysPart::Key, n};
    }
    return {KeysPart::Panel};
}

} // namespace puls::gui
