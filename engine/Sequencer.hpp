#pragma once
#include "MidiEvents.hpp"
#include "Pattern.hpp"
#include "Transport.hpp"
#include <array>

namespace puls {

// Headless, allocation-free sequencing engine. Real-time safe: all storage is fixed-size members.
//
// Each block: the host position gives a half-open window [t0, t1) of musical units. Every step of every
// track has a nominal start on its own grid, anchor + s * unitsPerStep; the events of a step (ratchets,
// swing, microtiming applied) are emitted by whichever block contains their time. Step numbers are derived
// from absolute position, so tempo changes, block sizes, and jumps need no per-track playback state.
//
// Overflow policy (deterministic): per-block candidates beyond kMaxCandidates are dropped in generation order
// (track order, then step); the remaining ones are accepted in time order until the event or pending-note-off
// capacity runs out, then the NEWEST note-ons are dropped. Every drop is counted; note-offs are never dropped.
class Sequencer {
public:
    static constexpr uint32_t kMaxCandidates  = 2048;
    static constexpr uint32_t kMaxPendingOffs = 512;
    static constexpr uint32_t kMaxEvents      = 4096;

    Sequencer() noexcept { reset(); }

    void setAnchorMode(AnchorMode m) noexcept { anchorMode_ = m; }

    // Fills events() for this block. If `next` is given, every event whose time is >= `switchAt` (musical units,
    // see Timing.hpp; INT64_MIN = immediately) is generated from `next` instead of `pattern`. The step grid is not
    // re-anchored, so the choice depends only on each event's own time and the output is identical for any block
    // size. switchCommitted() tells the caller that the block's window reached switchAt (`next` is now current).
    void process(const Pattern& pattern, const TransportInfo& transport, uint32_t frames,
                 const Pattern* next = nullptr, int64_t switchAt = 0) noexcept;
    bool switchCommitted() const noexcept { return switchCommitted_; }

    // Clears events() and fills it with note-offs (frame 0) for every sounding note.
    // For lifecycle changes (deactivate, pattern replacement).
    void flush() noexcept;

    // Forgets all state without emitting anything.
    void reset() noexcept;

    const MidiMsg* events() const noexcept { return events_.data(); }
    uint32_t eventCount() const noexcept { return eventCount_; }
    uint32_t droppedNoteOns() const noexcept { return dropped_; }
    uint32_t soundingNotes() const noexcept { return activeCount_; }

    // Step index (into Track::steps) each track is at, at the start of the last processed block; -1 when stopped.
    int playhead(int track) const noexcept { return track >= 0 && track < kMaxTracks ? playheads_[track] : -1; }

    // How the last block related to the previous one (diagnostics and tests).
    enum class BlockKind : uint8_t {
        Stopped,     // not playing (or invalid transport): sounding notes released
        Started,     // playback started: the grid was (re)anchored
        Continuous,  // the host position continued the previous block (within 2 samples)
        Resynced,    // the host was slightly off (jitter, <= kResyncSeconds): followed without touching the grid
        Resumed,     // a short transport dropout ended where playback would have been: the grid was kept
        Jumped,      // a real relocation (loop wrap, seek): notes released, grid re-anchored in RestartOnStart
    };
    BlockKind blockKind() const noexcept { return blockKind_; }

    // Host position errors up to this are treated as jitter, not as a relocation. Loop wraps and seeks move by
    // at least a step (31 ms for 1/64 at 120 bpm), far beyond it.
    static constexpr double kResyncSeconds = 0.010;
    // A dropout (blocks without a playing transport) longer than this is a real stop.
    static constexpr double kMaxDropoutSeconds = 0.25;

private:
    struct Item {
        int64_t  time;
        int64_t  offTime;   // for note-ons
        uint32_t order;
        uint32_t id;        // note-off: id of the note-on it ends
        uint8_t  kind;      // 0 = note-off, 1 = note-on
        uint8_t  channel, note, velocity;
    };
    struct Pending { int64_t time; uint32_t id; uint8_t channel, note; };

    static bool before(const Item& a, const Item& b) noexcept;
    void emit(uint8_t status, uint8_t channel, uint8_t note, uint8_t velocity, uint32_t frame) noexcept;
    void releaseAll() noexcept;
    uint32_t frameOf(int64_t time, int64_t t0, double unitsPerSample, uint32_t frames) const noexcept;
    void updatePlayheads(const Pattern& p, int64_t t0) noexcept;
    void generate(const Pattern& p, int64_t lo, int64_t hi, uint32_t& order, uint32_t& n) noexcept;

    AnchorMode anchorMode_ = AnchorMode::RestartOnStart;
    BlockKind  blockKind_ = BlockKind::Stopped;
    bool       wasPlaying_ = false;
    bool       canResume_ = false;   // stopped right after playing: a quick return may continue the grid
    int64_t    stoppedFrames_ = 0;   // frames since then
    double     lastUps_ = 0.0;       // units per sample of the last playing block
    bool       switchCommitted_ = false;
    int64_t    lastEnd_ = 0;
    int64_t    lastHi_ = 0;
    int64_t    anchor_ = 0;
    uint32_t   nextId_ = 1;
    uint32_t   activeCount_ = 0;
    uint32_t   pendingCount_ = 0;
    uint32_t   dropped_ = 0;
    uint32_t   eventCount_ = 0;

    std::array<int16_t, kMaxTracks> playheads_{};
    std::array<uint32_t, 16 * 128> active_{};
    std::array<Pending, kMaxPendingOffs> pending_{};
    std::array<MidiMsg, kMaxEvents> events_{};
    std::array<Item, 2 * kMaxCandidates + kMaxPendingOffs> items_{};
};

} // namespace puls
