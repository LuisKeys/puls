#include "Sequencer.hpp"
#include <algorithm>
#include <cmath>

namespace puls {

namespace {

constexpr int64_t floorDiv(int64_t a, int64_t b) noexcept
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

constexpr uint64_t mix(uint64_t x) noexcept // splitmix64 finalizer
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Deterministic roll in [0, 100) for (pattern seed, track, absolute step number).
constexpr uint32_t roll(uint32_t seed, uint32_t trackId, int64_t step) noexcept
{
    return static_cast<uint32_t>(mix(mix(seed) ^ (static_cast<uint64_t>(trackId) << 32) ^ static_cast<uint64_t>(step)) % 100u);
}

template <typename T> constexpr T clampv(T v, T lo, T hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

} // namespace

bool Sequencer::before(const Item& a, const Item& b) noexcept
{
    if (a.time != b.time) return a.time < b.time;
    if (a.kind != b.kind) return a.kind < b.kind; // note-offs sort before note-ons at the same time
    return a.order < b.order;
}

void Sequencer::reset() noexcept
{
    active_.fill(0);
    playheads_.fill(-1);
    activeCount_ = pendingCount_ = eventCount_ = 0;
    wasPlaying_ = false;
    lastEnd_ = lastHi_ = anchor_ = 0;
}

void Sequencer::emit(uint8_t status, uint8_t channel, uint8_t note, uint8_t velocity, uint32_t frame) noexcept
{
    if (eventCount_ >= kMaxEvents) return; // unreachable for note-offs by the reserve in process()
    events_[eventCount_++] = MidiMsg{frame, {static_cast<uint8_t>(status | channel), note, velocity}};
}

void Sequencer::releaseAll() noexcept
{
    pendingCount_ = 0;
    if (activeCount_ == 0) return;
    for (uint32_t i = 0; i < active_.size(); ++i) {
        if (active_[i] != 0) {
            emit(0x80, static_cast<uint8_t>(i / 128), static_cast<uint8_t>(i % 128), 0, 0);
            active_[i] = 0;
        }
    }
    activeCount_ = 0;
}

void Sequencer::flush() noexcept
{
    eventCount_ = 0;
    releaseAll();
}

uint32_t Sequencer::frameOf(int64_t time, int64_t t0, double ups, uint32_t frames) const noexcept
{
    if (time <= t0) return 0;
    // Round up to the next sample so nothing is emitted before its host time; the small epsilon absorbs
    // the sub-unit rounding of the block boundaries (keeps output identical across block sizes).
    const double x = std::ceil(static_cast<double>(time - t0) / ups - 0.01);
    if (x <= 0.0) return 0;
    return x >= frames ? frames - 1 : static_cast<uint32_t>(x);
}

void Sequencer::updatePlayheads(const Pattern& p, int64_t t0) noexcept
{
    const int count = clampv<int>(p.trackCount, 0, kMaxTracks);
    for (int i = 0; i < kMaxTracks; ++i) {
        if (i >= count) { playheads_[i] = -1; continue; }
        const Track& tr = p.tracks[i];
        const int length = clampv<int>(tr.length, 1, kMaxSteps);
        const int64_t s = std::max<int64_t>(0, floorDiv(t0 - anchor_, unitsPerStep(tr.resolution)));
        playheads_[i] = static_cast<int16_t>((s + tr.phase % length) % length);
    }
}

void Sequencer::generate(const Pattern& p, int64_t lo, int64_t hi, uint32_t& order, uint32_t& n) noexcept
{
    const int count = clampv<int>(p.trackCount, 0, kMaxTracks);
    bool anySolo = false;
    for (int i = 0; i < count; ++i) anySolo = anySolo || p.tracks[i].solo;

    for (int ti = 0; ti < count; ++ti) {
        const Track& tr = p.tracks[ti];
        if (tr.mute || (anySolo && !tr.solo)) continue;

        const int64_t stepU = unitsPerStep(tr.resolution);
        const int length = clampv<int>(tr.length, 1, kMaxSteps);
        const int phase = tr.phase % length;
        const int64_t sLo = std::max<int64_t>(0, floorDiv(lo - anchor_, stepU) - 3);
        const int64_t sHi = floorDiv(hi - anchor_, stepU) + 1;

        for (int64_t s = sLo; s <= sHi; ++s) {
            const Step& st = tr.steps[(s + phase) % length];
            if (!st.active) continue;

            const uint32_t prob = std::min<uint32_t>(100, (uint32_t(st.probability) * tr.probabilityPct) / 100);
            if (prob == 0) continue;
            if (prob < 100 && roll(p.seed, tr.id, s) >= prob) continue;

            const int64_t ratchets = clampv<int>(st.ratchets, 1, 8);
            const int64_t sub = stepU / ratchets;
            const int64_t swing = (s & 1) ? stepU * clampv<int>(tr.swing, 0, 75) / 100 : 0;
            const int64_t micro = stepU * clampv<int>(st.microtiming, -50, 50) / 100;
            const int64_t gate = std::max<int64_t>(1, sub * clampv<int>(st.gate, 1, 100) * clampv<int>(tr.durationPct, 1, 100) / 10000);
            const int vel = clampv<int>((int(st.velocity) * tr.velocityPct) / 100, 1, 127);

            for (int64_t k = 0; k < ratchets; ++k) {
                const int64_t e = anchor_ + s * stepU + swing + micro + k * sub;
                if (e < lo || e >= hi) continue;
                if (n >= kMaxCandidates) { ++dropped_; continue; }
                items_[n++] = Item{e, e + gate, order++, 0, 1, static_cast<uint8_t>(tr.channel & 15),
                                   static_cast<uint8_t>(tr.note & 127), static_cast<uint8_t>(vel)};
            }
        }
    }
}

void Sequencer::process(const Pattern& pattern, const TransportInfo& tp, uint32_t frames,
                        const Pattern* next, int64_t switchAt) noexcept
{
    eventCount_ = 0;
    switchCommitted_ = false;

    // Reject hosts that report nonsense: non-finite or absurd values would overflow the int64 unit arithmetic
    // (1e9 quarters = ~1e18 units, int64 holds 9.2e18) or make the block window degenerate.
    const bool valid = tp.playing && frames > 0
                       && std::isfinite(tp.bpm) && tp.bpm > 0.0 && tp.bpm < 1e5
                       && std::isfinite(tp.sampleRate) && tp.sampleRate >= 1000.0 && tp.sampleRate < 1e7
                       && std::isfinite(tp.quarterPos) && tp.quarterPos >= 0.0 && tp.quarterPos < 1e9;
    if (!valid) {
        releaseAll();
        playheads_.fill(-1);
        wasPlaying_ = false;
        return;
    }

    const double ups = tp.bpm / 60.0 / tp.sampleRate * static_cast<double>(kUnitsPerQuarter); // units per sample
    const double exactStart = tp.quarterPos * static_cast<double>(kUnitsPerQuarter);
    const int64_t startCand = static_cast<int64_t>(std::llround(exactStart));
    const int64_t tol = static_cast<int64_t>(2.0 * ups) + 2;

    // Ownership window. An event is emitted at the first sample at or after its time, so a block owns the
    // events in [t0 - 0.99 sample, t1 - 0.99 sample): this makes the sample an event lands on independent of
    // where block boundaries fall, and consecutive windows tile exactly (lo of a block == hi of the last).
    const int64_t shift = static_cast<int64_t>(0.99 * ups);

    int64_t t0, lo;
    if (wasPlaying_ && std::llabs(startCand - lastEnd_) <= tol) {
        t0 = lastEnd_; // continuous: tile exactly with the previous block
        lo = lastHi_;
    } else {
        releaseAll(); // start from stopped, or a position jump / loop wrap: old note-off times are void
        t0 = startCand;
        anchor_ = anchorMode_ == AnchorMode::RestartOnStart ? t0 : 0;
        lo = t0 - shift;
    }
    int64_t t1 = static_cast<int64_t>(std::llround(exactStart + static_cast<double>(frames) * ups));
    if (t1 <= t0) t1 = t0 + 1;
    int64_t hi = t1 - shift;
    if (hi <= lo) hi = lo + 1;

    updatePlayheads(pattern, t0);

    uint32_t n = 0, order = 0;
    if (next != nullptr && switchAt < hi) {
        const int64_t cut = std::min(std::max(switchAt, lo), hi);
        if (cut > lo) generate(pattern, lo, cut, order, n);
        generate(*next, cut, hi, order, n);
        switchCommitted_ = true;
    } else {
        generate(pattern, lo, hi, order, n);
    }

    // Merge in note-offs that were scheduled by earlier blocks and fall into this one.
    uint32_t keep = 0;
    for (uint32_t i = 0; i < pendingCount_; ++i) {
        const Pending& pe = pending_[i];
        if (pe.time < hi) items_[n++] = Item{pe.time, 0, pe.id, pe.id, 0, pe.channel, pe.note, 0};
        else pending_[keep++] = pe;
    }
    pendingCount_ = keep;

    std::sort(items_.begin(), items_.begin() + n, before);

    // Walk in time order. Offs reserve room so they can always be emitted.
    constexpr uint32_t kOnLimit = kMaxEvents - kMaxPendingOffs;
    for (uint32_t i = 0; i < n; ++i) {
        const Item it = items_[i];
        uint32_t& act = active_[it.channel * 128u + it.note];
        const uint32_t frame = frameOf(it.time, t0, ups, frames);

        if (it.kind == 0) {
            if (act == it.id) { emit(0x80, it.channel, it.note, 0, frame); act = 0; --activeCount_; }
            continue;
        }

        const bool offInBlock = it.offTime < hi;
        if (eventCount_ + 3 > kOnLimit || (!offInBlock && pendingCount_ >= kMaxPendingOffs)
            || (offInBlock && n >= items_.size())) {
            ++dropped_;
            continue;
        }
        if (act != 0) { emit(0x80, it.channel, it.note, 0, frame); --activeCount_; } // retrigger: end the previous note first
        emit(0x90, it.channel, it.note, it.velocity, frame);
        const uint32_t id = nextId_++;
        if (nextId_ == 0) nextId_ = 1;
        act = id;
        ++activeCount_;

        if (offInBlock) {
            // Insert the note-off into the not-yet-processed tail, keeping the sort order.
            Item off{it.offTime, 0, id, id, 0, it.channel, it.note, 0};
            uint32_t pos = n++;
            while (pos > i + 1 && before(off, items_[pos - 1])) { items_[pos] = items_[pos - 1]; --pos; }
            items_[pos] = off;
        } else {
            pending_[pendingCount_++] = Pending{it.offTime, id, it.channel, it.note};
        }
    }

    lastEnd_ = t1;
    lastHi_ = hi;
    wasPlaying_ = true;
}

} // namespace puls
