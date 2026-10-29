#pragma once
#include "MidiEvents.hpp"
#include <array>
#include <cstdint>
#include <utility>

namespace puls {

// A note to preview when a step is switched on in the editor (main thread -> audio thread).
struct AuditionNote { uint8_t channel = 0, note = 36, velocity = 100; };

// Plays preview notes of a fixed length, independently of the transport and of the sequencer. Real-time safe:
// fixed storage, no allocation. Note-ons sound at frame 0 of the block they are triggered in, note-offs are
// sample accurate. Retriggering a sounding note ends it first; with every voice busy, the one closest to its
// end is stolen. Per block: begin(), trigger() for each request, advance(); events() are then sorted by frame.
class Auditioner {
public:
    static constexpr int      kMaxVoices = 16;
    static constexpr uint32_t kMaxTriggers = 64;   // per block; more are ignored
    static constexpr uint32_t kMaxEvents = 2 * kMaxTriggers + kMaxVoices;

    void reset() noexcept { voiceCount_ = 0; eventCount_ = 0; triggers_ = 0; }
    void begin() noexcept { eventCount_ = 0; triggers_ = 0; }

    void trigger(const AuditionNote& a, uint32_t lengthFrames) noexcept
    {
        if (triggers_ >= kMaxTriggers) return;
        ++triggers_;
        int v = 0;
        while (v < voiceCount_ && !(voices_[v].channel == a.channel && voices_[v].note == a.note)) ++v;
        if (v == voiceCount_) {
            if (voiceCount_ < kMaxVoices) {
                ++voiceCount_;
            } else {
                v = 0;
                for (int i = 1; i < voiceCount_; ++i)
                    if (voices_[i].remaining < voices_[v].remaining) v = i;
                emit(0x80, voices_[v].channel, voices_[v].note, 0, 0);
            }
        } else {
            emit(0x80, a.channel, a.note, 0, 0);
        }
        voices_[v] = Voice{lengthFrames > 0 ? lengthFrames : 1, static_cast<uint8_t>(a.channel & 0x0F), static_cast<uint8_t>(a.note & 0x7F)};
        emit(0x90, voices_[v].channel, voices_[v].note, a.velocity < 1 ? 1 : a.velocity > 127 ? 127 : a.velocity, 0);
    }

    // Emits the note-offs that fall inside this block and counts the rest down.
    void advance(uint32_t frames) noexcept
    {
        for (int v = 0; v < voiceCount_;) {
            if (voices_[v].remaining < frames) {
                emit(0x80, voices_[v].channel, voices_[v].note, 0, voices_[v].remaining);
                voices_[v] = voices_[--voiceCount_];
            } else {
                voices_[v++].remaining -= frames;
            }
        }
        // Stable insertion sort: frame-0 events keep their off-before-on order.
        for (uint32_t i = 1; i < eventCount_; ++i)
            for (uint32_t j = i; j > 0 && events_[j - 1].frame > events_[j].frame; --j)
                std::swap(events_[j - 1], events_[j]);
    }

    const MidiMsg* events() const noexcept { return events_.data(); }
    uint32_t eventCount() const noexcept { return eventCount_; }
    int soundingNotes() const noexcept { return voiceCount_; }

private:
    struct Voice { uint32_t remaining; uint8_t channel, note; };   // frames from the block start to the note-off

    void emit(uint8_t status, uint8_t channel, uint8_t note, uint8_t velocity, uint32_t frame) noexcept
    {
        if (eventCount_ < kMaxEvents) events_[eventCount_++] = MidiMsg{frame, {static_cast<uint8_t>(status | channel), note, velocity}};
    }

    std::array<Voice, kMaxVoices> voices_{};
    std::array<MidiMsg, kMaxEvents> events_{};
    int      voiceCount_ = 0;
    uint32_t eventCount_ = 0;
    uint32_t triggers_ = 0;
};

} // namespace puls
