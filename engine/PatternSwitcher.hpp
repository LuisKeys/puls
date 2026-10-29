#pragma once
#include "Timing.hpp"
#include <cmath>
#include <cstdint>
#include <limits>

namespace puls {

enum class SwitchQuantize : uint8_t { Now, Beat, Bar };

struct SwitchRequest {
    uint8_t        index = 0;
    SwitchQuantize quantize = SwitchQuantize::Bar;
};

// Audio-thread bookkeeping of which pattern plays and when a requested switch happens. A request is resolved
// into an absolute target (musical units), so the moment of the switch does not depend on block sizes; the
// Sequencer applies it per event. Real-time safe: no allocation, plain members.
class PatternSwitcher {
public:
    static constexpr int64_t kImmediate = std::numeric_limits<int64_t>::min();

    void reset(int index = 0) noexcept { current_ = index; pending_ = hasRequest_ = false; }

    // Latest request wins. Resolved by the next plan().
    void request(const SwitchRequest& r) noexcept { request_ = r; hasRequest_ = true; }

    // Once per block, before Sequencer::process. quarterPos is the host position of the block's first sample.
    void plan(bool playing, double quarterPos, double beatsPerBar) noexcept
    {
        if (!playing) { // nothing sequenced is sounding: switch right away
            if (hasRequest_) current_ = request_.index;
            else if (pending_) current_ = pendingIndex_;
            hasRequest_ = pending_ = false;
            return;
        }
        if (hasRequest_) {
            hasRequest_ = false;
            if (request_.index == current_) { pending_ = false; return; } // back to the current one: cancel
            pendingIndex_ = request_.index;
            quantize_ = request_.quantize;
            pending_ = true;
            resolve(quarterPos, beatsPerBar);
            return;
        }
        // Stale target (e.g. the transport jumped backwards): re-resolve instead of waiting for it.
        if (pending_ && quantize_ != SwitchQuantize::Now) {
            const double len = span(beatsPerBar);
            if (switchAt_ > toUnits(quarterPos) + toUnits(len + 1.0)) resolve(quarterPos, beatsPerBar);
        }
    }

    int     current() const noexcept { return current_; }
    bool    pending() const noexcept { return pending_; }
    int     pendingIndex() const noexcept { return pendingIndex_; }
    int64_t switchAt() const noexcept { return switchAt_; }

    // The Sequencer reached the target: the pending pattern is now the current one.
    void commit() noexcept { if (pending_) { current_ = pendingIndex_; pending_ = false; } }

private:
    static int64_t toUnits(double quarters) noexcept { return static_cast<int64_t>(std::llround(quarters * static_cast<double>(kUnitsPerQuarter))); }
    double span(double beatsPerBar) const noexcept { return quantize_ == SwitchQuantize::Beat ? 1.0 : (beatsPerBar >= 1.0 ? beatsPerBar : 4.0); }
    void resolve(double quarterPos, double beatsPerBar) noexcept
    {
        if (quantize_ == SwitchQuantize::Now || !(quarterPos >= 0.0)) { switchAt_ = kImmediate; return; }
        const double len = span(beatsPerBar);
        switchAt_ = toUnits(std::ceil(quarterPos / len - 1e-9) * len); // a block starting on a boundary switches at once
    }

    int            current_ = 0;
    int            pendingIndex_ = 0;
    int64_t        switchAt_ = kImmediate;
    SwitchQuantize quantize_ = SwitchQuantize::Bar;
    SwitchRequest  request_;
    bool           hasRequest_ = false;
    bool           pending_ = false;
};

} // namespace puls
