#include "TestFramework.hpp"
#include "TestHelpers.hpp"
#include "PatternSwitcher.hpp"
#include "SpscQueue.hpp"
#include <cstdlib>
#include <random>

using namespace th;

// 120 bpm @ 48 kHz: quarter = 24000 samples, 1/16 = 6000, 1/8 = 12000, 1/32 = 3000, 1/8T = 8000, 1/16T = 4000.

TEST(timing_constants_are_exact)
{
    CHECK_EQ(ticksPerStep(Resolution::Quarter), 3840);
    CHECK_EQ(ticksPerStep(Resolution::Sixteenth), 960);
    CHECK_EQ(ticksPerStep(Resolution::EighthTriplet) * 3, kPPQ);
    CHECK_EQ(ticksPerStep(Resolution::SixteenthTriplet) * 6, kPPQ);
    CHECK_EQ(ticksPerStep(Resolution::ThirtySecondTriplet) * 12, kPPQ);
    CHECK_EQ(unitsPerStep(Resolution::SixtyFourth) * 16, kUnitsPerQuarter);
    for (int r = 0; r < int(Resolution::Count); ++r)
        for (int k = 1; k <= 8; ++k) CHECK_EQ(unitsPerStep(Resolution(r)) % k, 0);
}

TEST(independent_lengths_and_resolutions)
{
    Pattern p = emptyPattern(4);
    fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);   // note 36, 6000 samples/step
    fillIndexed(p.tracks[1], 12, Resolution::Sixteenth);   // note 37
    fillIndexed(p.tracks[2], 7,  Resolution::Eighth);      // note 38, 12000
    fillIndexed(p.tracks[3], 13, Resolution::ThirtySecond);// note 39, 3000
    const int64_t total = 24000 * 16; // 4 bars
    const int64_t period[4] = {6000, 6000, 12000, 3000};
    const int len[4] = {16, 12, 7, 13};
    auto ev = renderAll(p, total, 512);
    for (int t = 0; t < 4; ++t) {
        auto ons = onsOf(ev, 36 + t);
        CHECK_EQ((int64_t)ons.size(), total / period[t]);
        for (size_t k = 0; k < ons.size(); ++k) {
            CHECK_EQ(ons[k].sample, (int64_t)k * period[t]);
            CHECK_EQ((int)ons[k].vel, (int)(k % len[t]) + 1); // wraps independently per track
        }
    }
}

TEST(triplet_timing)
{
    Pattern p = emptyPattern(3);
    fillIndexed(p.tracks[0], 3, Resolution::EighthTriplet);
    fillIndexed(p.tracks[1], 6, Resolution::SixteenthTriplet);
    fillIndexed(p.tracks[2], 12, Resolution::ThirtySecondTriplet);
    auto ev = renderAll(p, 24000 * 8, 333);
    const int64_t period[3] = {8000, 4000, 2000};
    for (int t = 0; t < 3; ++t) {
        auto ons = onsOf(ev, 36 + t);
        CHECK_EQ((int64_t)ons.size(), 24000 * 8 / period[t]);
        for (size_t k = 0; k < ons.size(); ++k) CHECK_EQ(ons[k].sample, (int64_t)k * period[t]);
    }
}

// A busy pattern touching every event property.
static Pattern busyPattern()
{
    Pattern p = emptyPattern(6);
    p.seed = 12345;
    fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    fillIndexed(p.tracks[1], 12, Resolution::Sixteenth); p.tracks[1].swing = 40;
    fillIndexed(p.tracks[2], 7, Resolution::Eighth);     p.tracks[2].phase = 3;
    fillIndexed(p.tracks[3], 9, Resolution::SixteenthTriplet);
    fillIndexed(p.tracks[4], 13, Resolution::ThirtySecond); p.tracks[4].durationPct = 70;
    fillIndexed(p.tracks[5], 5, Resolution::Quarter);
    for (int t = 0; t < 6; ++t)
        for (int i = 0; i < p.tracks[t].length; ++i) {
            Step& s = p.tracks[t].steps[i];
            s.ratchets = (i % 4 == 1) ? 3 : (i % 5 == 2 ? 8 : 1);
            s.microtiming = static_cast<int8_t>((i * 17) % 101 - 50);
            s.gate = static_cast<uint8_t>(20 + (i * 13) % 81);
            s.probability = (i % 3 == 0) ? 60 : 100;
        }
    return p;
}

TEST(identical_output_for_every_block_size)
{
    const Pattern p = busyPattern();
    for (double bpm : {120.0, 123.37, 20.0, 190.5})
        for (double sr : {44100.0, 48000.0, 96000.0}) {
            const int64_t total = int64_t(sr * 12.0);
            auto ref = renderAll(p, total, 512, bpm, sr);
            CHECK(ref.size() > 100);
            for (uint32_t block : {1u, 7u, 32u, 64u, 480u, 4096u}) {
                if (block == 1 && sr > 50000) continue; // keep runtime reasonable
                auto got = renderAll(p, total, block, bpm, sr);
                bool same = got.size() == ref.size();
                for (size_t i = 0; same && i < ref.size(); ++i) same = got[i] == ref[i];
                if (!same) std::printf("    mismatch: bpm=%g sr=%g block=%u (%zu vs %zu events)\n", bpm, sr, block, got.size(), ref.size());
                CHECK(same);
            }
        }
}

TEST(long_term_stability_10000_bars)
{
    Pattern p = emptyPattern(2);
    fillIndexed(p.tracks[0], 1, Resolution::Quarter);       // a quarter every 24000 samples
    fillIndexed(p.tracks[1], 7, Resolution::SixteenthTriplet); // 4000-sample steps
    const int64_t bars = 10000, total = bars * 4 * 24000;
    int64_t kicks = 0, trip = 0; bool exact = true;
    Sequencer seq;
    render(seq, p, total, 512, 120.0, 48000.0, [&](const AbsEvent& e) {
        if (!e.on()) return;
        if (e.note == 36) { exact = exact && e.sample == kicks * 24000; ++kicks; }
        else { exact = exact && e.sample == trip * 4000; ++trip; }
    });
    CHECK(exact);
    CHECK_EQ(kicks, bars * 4);
    CHECK_EQ(trip, bars * 4 * 6);
}

TEST(tempo_changes_between_blocks)
{
    Pattern p = emptyPattern(1);
    fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    Sequencer seq;
    const double sr = 48000.0; double q = 0.0; int ons = 0; bool onGrid = true;
    for (int blk = 0; blk < 4000; ++blk) {
        const double bpm = 60.0 + 100.0 * (0.5 + 0.5 * std::sin(blk * 0.01)) ; // 60..160, changes every block
        const uint32_t frames = 256;
        seq.process(p, TransportInfo{true, q, bpm, sr}, frames);
        for (uint32_t i = 0; i < seq.eventCount(); ++i) {
            const MidiMsg& m = seq.events()[i];
            if (!m.isNoteOn()) continue;
            ++ons;
            const double eq = q + m.frame * bpm / (60.0 * sr);          // event position in quarters
            const double off = std::fabs(eq / 0.25 - std::round(eq / 0.25)) * 0.25;
            if (off > 1.5 * bpm / (60.0 * sr)) onGrid = false;           // within ~1 sample
        }
        q += frames * bpm / (60.0 * sr);
    }
    CHECK(onGrid);
    CHECK(std::abs(ons - (int)std::ceil(q / 0.25)) <= 1);
}

TEST(stop_flushes_notes_and_start_restarts_from_phase)
{
    Pattern p = emptyPattern(1);
    fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    for (auto& s : p.tracks[0].steps) s.gate = 100;
    for (AnchorMode mode : {AnchorMode::RestartOnStart, AnchorMode::HostAbsolute}) {
        Sequencer seq; seq.setAnchorMode(mode);
        const double sr = 48000, bpm = 120;
        for (int b = 0; b < 8; ++b) seq.process(p, TransportInfo{true, b * 512 * bpm / (60 * sr), bpm, sr}, 512);
        CHECK_EQ(seq.soundingNotes(), 1);
        seq.process(p, TransportInfo{false, 0, bpm, sr}, 512);
        CHECK_EQ(seq.eventCount(), 1);
        CHECK(seq.events()[0].isNoteOff() && seq.events()[0].frame == 0);
        CHECK_EQ(seq.soundingNotes(), 0);
        seq.process(p, TransportInfo{false, 0, bpm, sr}, 512);
        CHECK_EQ(seq.eventCount(), 0);
        // Restart at q=0.1 (off the 1/16 grid).
        seq.process(p, TransportInfo{true, 0.1, bpm, sr}, 4096);
        bool firstIsAtZero = seq.eventCount() > 0 && seq.events()[0].isNoteOn() && seq.events()[0].frame == 0;
        if (mode == AnchorMode::RestartOnStart) {
            CHECK(firstIsAtZero);
            CHECK_EQ((int)seq.events()[0].data[2], 1); // step 0 again
        } else {
            CHECK(!firstIsAtZero);
            CHECK_EQ((int)seq.events()[0].data[2], 2); // next grid step (index 1 at q=0.25)
            CHECK_EQ((int)seq.events()[0].frame, (int)std::ceil((0.25 - 0.1) * 60 / bpm * sr));
        }
    }
}

TEST(position_jump_and_loop_wrap_leave_no_stuck_notes)
{
    Pattern p = emptyPattern(2);
    fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    fillIndexed(p.tracks[1], 5, Resolution::Eighth);
    for (auto& t : p.tracks) for (auto& s : t.steps) s.gate = 100;
    Sequencer seq; std::vector<AbsEvent> ev; int64_t sample = 0;
    const double sr = 48000, bpm = 120; const uint32_t B = 480;
    auto run = [&](double q, int blocks) {
        for (int b = 0; b < blocks; ++b) {
            seq.process(p, TransportInfo{true, q + b * B * bpm / (60 * sr), bpm, sr}, B);
            for (uint32_t i = 0; i < seq.eventCount(); ++i) {
                const MidiMsg& m = seq.events()[i];
                ev.push_back({sample + m.frame, uint8_t(m.data[0] & 0xF0), uint8_t(m.data[0] & 15), m.data[1], m.data[2]});
            }
            sample += B;
        }
    };
    run(0.0, 37);
    run(10.3, 41);              // jump forward, off-grid
    for (int loop = 0; loop < 6; ++loop) run(4.0, 13); // loop 4.0 .. ~4.0+13*0.0.. repeating (wrap back each 13 blocks)
    run(1.0, 5);                // jump backward
    seq.process(p, TransportInfo{false, 0, bpm, sr}, B);
    for (uint32_t i = 0; i < seq.eventCount(); ++i) { const MidiMsg& m = seq.events()[i]; ev.push_back({sample, uint8_t(m.data[0] & 0xF0), uint8_t(m.data[0] & 15), m.data[1], m.data[2]}); }
    bool valid; int sounding = checkNoteBalance(ev, valid);
    CHECK(valid);
    CHECK_EQ(sounding, 0);
    CHECK(ev.size() > 50);
}

TEST(probability_boundaries_and_determinism)
{
    auto count = [](int prob, uint32_t seed, uint32_t block) {
        Pattern p = emptyPattern(1); p.seed = seed;
        fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
        for (auto& s : p.tracks[0].steps) s.probability = static_cast<uint8_t>(prob);
        return renderAll(p, 6000LL * 20000, block);
    };
    CHECK_EQ((int)count(0, 1, 512).size(), 0);
    CHECK_EQ((int)onsOf(count(100, 1, 512), 36).size(), 20000);
    auto a = count(50, 7, 512), b = count(50, 7, 97), c = count(50, 8, 512);
    const int n = (int)onsOf(a, 36).size();
    CHECK(n > 9500 && n < 10500);
    CHECK(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin())); // same seed, different blocks
    CHECK(!(a.size() == c.size() && std::equal(a.begin(), a.end(), c.begin()))); // different seed
    // Track probability multiplier scales the step probability.
    Pattern p = emptyPattern(1); fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    p.tracks[0].probabilityPct = 0;
    CHECK_EQ((int)renderAll(p, 6000 * 64, 512).size(), 0);
}

TEST(ratchets_are_evenly_spaced_with_ordered_note_offs)
{
    Pattern p = emptyPattern(1);
    p.tracks[0].steps[0].active = true; p.tracks[0].steps[0].ratchets = 4; p.tracks[0].steps[0].gate = 100;
    for (uint32_t block : {1u, 500u, 6000u}) {
        auto ev = renderAll(p, 6000 * 4, block);
        CHECK_EQ((int)ev.size() >= 8 ? 8 : (int)ev.size(), 8);
        const int64_t expect[8][2] = {{0,1},{1500,0},{1500,1},{3000,0},{3000,1},{4500,0},{4500,1},{6000,0}};
        for (int i = 0; i < 8 && i < (int)ev.size(); ++i) {
            CHECK_EQ(ev[i].sample, expect[i][0]);
            CHECK_EQ((int)ev[i].on(), (int)expect[i][1]);
        }
    }
}

TEST(negative_microtiming_is_found_before_its_step_boundary)
{
    Pattern p = emptyPattern(1);
    p.tracks[0].steps[1].active = true; p.tracks[0].steps[1].microtiming = -50;
    p.tracks[0].steps[0].active = true; p.tracks[0].steps[0].microtiming = -50;
    for (uint32_t block : {1u, 128u, 3000u, 6000u, 7777u}) {
        auto ons = onsOf(renderAll(p, 6000 * 16 * 3, block), 36);
        // Step 0 at the anchor with -50% would fire before start: skipped in the first cycle only.
        CHECK_EQ((int)ons.size(), 6);
        const int64_t exp[6] = {3000, 16 * 6000 - 3000, 16 * 6000 + 3000, 32 * 6000 - 3000, 32 * 6000 + 3000, 48 * 6000 - 3000};
        for (int i = 0; i < 6 && i < (int)ons.size(); ++i) CHECK_EQ(ons[i].sample, exp[i]);
    }
}

TEST(swing_gate_duration_phase_mute_solo_velocity)
{
    { // swing delays odd steps
        Pattern p = emptyPattern(1); fillIndexed(p.tracks[0], 16, Resolution::Sixteenth); p.tracks[0].swing = 50;
        auto ons = onsOf(renderAll(p, 6000 * 4, 512), 36);
        CHECK_EQ(ons[0].sample, 0); CHECK_EQ(ons[1].sample, 6000 + 3000); CHECK_EQ(ons[2].sample, 12000); CHECK_EQ(ons[3].sample, 18000 + 3000);
    }
    { // gate and duration
        Pattern p = emptyPattern(1); p.tracks[0].steps[0].active = true; p.tracks[0].steps[0].gate = 50;
        auto ev = renderAll(p, 6000 * 2, 512);
        CHECK_EQ(offsOf(ev, 36)[0].sample, 3000);
        p.tracks[0].durationPct = 50;
        ev = renderAll(p, 6000 * 2, 512);
        CHECK_EQ(offsOf(ev, 36)[0].sample, 1500);
    }
    { // phase offset: only step 3 active, phase 3 -> plays at the anchor
        Pattern p = emptyPattern(1); p.tracks[0].steps[3].active = true; p.tracks[0].phase = 3;
        auto ons = onsOf(renderAll(p, 6000 * 40, 512), 36);
        CHECK_EQ(ons[0].sample, 0); CHECK_EQ(ons[1].sample, 16 * 6000);
    }
    { // mute and solo
        Pattern p = emptyPattern(2);
        fillIndexed(p.tracks[0], 16, Resolution::Sixteenth); fillIndexed(p.tracks[1], 16, Resolution::Sixteenth);
        p.tracks[0].mute = true;
        auto ev = renderAll(p, 6000 * 8, 512);
        CHECK_EQ((int)onsOf(ev, 36).size(), 0); CHECK(onsOf(ev, 37).size() > 0);
        p.tracks[0].mute = false; p.tracks[0].solo = true;
        ev = renderAll(p, 6000 * 8, 512);
        CHECK(onsOf(ev, 36).size() > 0); CHECK_EQ((int)onsOf(ev, 37).size(), 0);
    }
    { // velocity multiplier is clamped to 1..127; inactive steps are silent; channel and note are honoured
        Pattern p = emptyPattern(1); p.tracks[0].steps[0].active = true; p.tracks[0].steps[0].velocity = 100;
        p.tracks[0].channel = 9; p.tracks[0].note = 60;
        p.tracks[0].velocityPct = 200;
        auto ev = renderAll(p, 6000, 512);
        CHECK_EQ((int)ev[0].vel, 127); CHECK_EQ((int)ev[0].ch, 9); CHECK_EQ((int)ev[0].note, 60);
        p.tracks[0].velocityPct = 0;
        CHECK_EQ((int)renderAll(p, 6000, 512)[0].vel, 1);
        Pattern q = emptyPattern(1);
        CHECK_EQ((int)renderAll(q, 6000 * 4, 512).size(), 0);
    }
}

TEST(overlapping_same_note_retriggers_cleanly)
{
    Pattern p = emptyPattern(1);
    fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    for (int i = 0; i < 16; ++i) { p.tracks[0].steps[i].gate = 100; p.tracks[0].steps[i].microtiming = (i % 2) ? -50 : 50; }
    auto ev = renderAll(p, 6000 * 64, 333);
    bool valid; int sounding = checkNoteBalance(ev, valid);
    CHECK(valid);
    CHECK(sounding <= 1);
}

TEST(randomized_invariants_hold)
{
    std::mt19937 rng(2024);
    auto R = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    for (int iter = 0; iter < 300; ++iter) {
        Pattern p = emptyPattern(R(1, 16)); p.seed = rng();
        for (int t = 0; t < p.trackCount; ++t) {
            Track& tr = p.tracks[t];
            tr.length = R(1, 64); tr.resolution = Resolution(R(0, int(Resolution::Count) - 1));
            tr.phase = R(0, 63); tr.swing = R(0, 75); tr.velocityPct = R(0, 200);
            tr.probabilityPct = R(0, 100); tr.durationPct = R(1, 100); tr.channel = R(0, 15); tr.note = R(0, 127);
            tr.mute = R(0, 9) == 0; tr.solo = R(0, 12) == 0;
            for (int i = 0; i < 64; ++i) {
                Step& s = tr.steps[i]; s.active = R(0, 2) != 0; s.velocity = R(1, 127); s.probability = R(0, 100);
                s.ratchets = R(1, 8); s.microtiming = R(-50, 50); s.gate = R(1, 100);
            }
        }
        Sequencer seq; seq.setAnchorMode(iter % 2 ? AnchorMode::HostAbsolute : AnchorMode::RestartOnStart);
        std::vector<AbsEvent> ev; int64_t sample = 0;
        double sr = std::vector<double>{22050, 44100, 48000, 96000, 192000}[R(0, 4)];
        double bpm = R(20, 300), q = R(0, 100) * 0.37;
        bool playing = true; bool ok = true;
        for (int blk = 0; blk < 400; ++blk) {
            const uint32_t frames = R(1, 3000);
            const int dice = R(0, 40);
            if (dice == 0) playing = !playing;
            else if (dice == 1) q = R(0, 200) * 0.173;           // jump
            else if (dice == 2) bpm = R(20, 300);                // tempo change
            seq.process(p, TransportInfo{playing, q, bpm, sr}, frames);
            uint32_t last = 0;
            for (uint32_t i = 0; i < seq.eventCount(); ++i) {
                const MidiMsg& m = seq.events()[i];
                ok = ok && m.frame < frames && m.frame >= last && (m.data[0] & 0x80) && m.data[1] < 128 && m.data[2] < 128;
                ok = ok && (!m.isNoteOn() || m.data[2] >= 1);
                last = m.frame;
                ev.push_back({sample + m.frame, uint8_t(m.data[0] & 0xF0), uint8_t(m.data[0] & 15), m.data[1], m.data[2]});
            }
            sample += frames;
            q += frames * bpm / (60.0 * sr);
        }
        seq.process(p, TransportInfo{false, 0, bpm, sr}, 64);
        for (uint32_t i = 0; i < seq.eventCount(); ++i) { const MidiMsg& m = seq.events()[i]; ev.push_back({sample, uint8_t(m.data[0] & 0xF0), uint8_t(m.data[0] & 15), m.data[1], m.data[2]}); }
        bool valid; int sounding = checkNoteBalance(ev, valid);
        if (!ok || !valid || sounding != 0) std::printf("    iteration %d: ok=%d valid=%d sounding=%d\n", iter, ok, valid, sounding);
        CHECK(ok); CHECK(valid); CHECK_EQ(sounding, 0);
    }
}

TEST(overflow_drops_newest_note_ons_never_note_offs)
{
    Pattern p = emptyPattern(16);
    for (int t = 0; t < 16; ++t) {
        fillIndexed(p.tracks[t], 64, Resolution::SixtyFourth);
        for (auto& s : p.tracks[t].steps) { s.ratchets = 8; s.gate = 100; }
    }
    Sequencer seq; std::vector<AbsEvent> ev;
    const uint32_t frames = 400000; // ~8 quarters in one block
    seq.process(p, TransportInfo{true, 0.0, 120.0, 48000.0}, frames);
    CHECK(seq.droppedNoteOns() > 0);
    CHECK(seq.eventCount() <= Sequencer::kMaxEvents);
    uint32_t last = 0; bool ok = true;
    for (uint32_t i = 0; i < seq.eventCount(); ++i) {
        const MidiMsg& m = seq.events()[i];
        ok = ok && m.frame < frames && m.frame >= last; last = m.frame;
        ev.push_back({m.frame, uint8_t(m.data[0] & 0xF0), uint8_t(m.data[0] & 15), m.data[1], m.data[2]});
    }
    CHECK(ok);
    seq.flush();
    for (uint32_t i = 0; i < seq.eventCount(); ++i) { const MidiMsg& m = seq.events()[i]; ev.push_back({frames, uint8_t(m.data[0] & 0xF0), uint8_t(m.data[0] & 15), m.data[1], m.data[2]}); }
    bool valid; int sounding = checkNoteBalance(ev, valid);
    CHECK(valid); CHECK_EQ(sounding, 0);
}

TEST(flush_releases_sounding_notes)
{
    Pattern p = emptyPattern(1); fillIndexed(p.tracks[0], 16, Resolution::Sixteenth);
    for (auto& s : p.tracks[0].steps) s.gate = 100;
    Sequencer seq; seq.process(p, TransportInfo{true, 0, 120, 48000}, 100);
    CHECK_EQ(seq.soundingNotes(), 1);
    seq.flush();
    CHECK_EQ(seq.eventCount(), 1); CHECK(seq.events()[0].isNoteOff());
    CHECK_EQ(seq.soundingNotes(), 0);
    seq.flush(); CHECK_EQ(seq.eventCount(), 0);
}

// ---- pattern switching ----

static Pattern allStepsPattern(uint8_t note, Resolution r)
{
    Pattern p = emptyPattern(1);
    p.tracks[0].note = note; p.tracks[0].length = 16; p.tracks[0].resolution = r;
    for (int i = 0; i < 16; ++i) p.tracks[0].steps[i].active = true;
    return p;
}

// Plays A, requests B after `requestAtSample` (first block starting at/after it), returns all events.
static std::vector<AbsEvent> renderSwitch(const Pattern& a, const Pattern& b, SwitchQuantize q, int64_t total, uint32_t block,
                                          int64_t requestAtSample, bool playing = true)
{
    Pattern bank[2] = {a, b};
    Sequencer seq; PatternSwitcher sw; sw.reset(0);
    std::vector<AbsEvent> out; bool requested = false;
    const double bpm = 120.0, sr = 48000.0;
    for (int64_t s = 0; s < total; s += block) {
        const uint32_t frames = (uint32_t)std::min<int64_t>(block, total - s);
        const double qpos = (double)s * bpm / (60.0 * sr);
        if (!requested && s >= requestAtSample) { sw.request({1, q}); requested = true; }
        sw.plan(playing, qpos, 4.0);
        seq.process(bank[sw.current()], TransportInfo{playing, qpos, bpm, sr}, frames,
                    sw.pending() ? &bank[sw.pendingIndex()] : nullptr, sw.switchAt());
        if (seq.switchCommitted()) sw.commit();
        for (uint32_t i = 0; i < seq.eventCount(); ++i) {
            const MidiMsg& m = seq.events()[i];
            out.push_back(AbsEvent{s + m.frame, (uint8_t)(m.data[0] & 0xF0), (uint8_t)(m.data[0] & 0x0F), m.data[1], m.data[2]});
        }
    }
    return out;
}

TEST(bar_quantized_switch_lands_exactly_on_the_bar_for_every_block_size)
{
    const Pattern a = allStepsPattern(36, Resolution::Sixteenth), b = allStepsPattern(38, Resolution::Eighth);
    const int64_t bar = 96000; // 4 quarters @ 120 bpm / 48 kHz
    std::vector<AbsEvent> ref;
    for (uint32_t block : {1u, 7u, 64u, 333u, 512u, 4096u}) {
        auto ev = renderSwitch(a, b, SwitchQuantize::Bar, bar * 3, block, 20000);
        if (ref.empty()) ref = ev;
        CHECK(ev == ref);
        auto kick = onsOf(ev, 36), snare = onsOf(ev, 38);
        CHECK_EQ((int64_t)kick.size(), bar / 6000);                    // A plays bar 1 only
        CHECK_EQ(kick.back().sample, bar - 6000);
        CHECK_EQ(snare.front().sample, bar);                            // B starts on the bar line
        CHECK_EQ((int64_t)snare.size(), (bar * 2) / 12000);
        bool valid; checkNoteBalance(ev, valid); CHECK(valid);   // strict on/off alternation across the switch
    }
}

TEST(beat_quantized_switch)
{
    const Pattern a = allStepsPattern(36, Resolution::Sixteenth), b = allStepsPattern(38, Resolution::Sixteenth);
    // 1 quarter = 24000 samples. Requested at ~1.26 quarters -> next beat is quarter 2 = sample 48000.
    std::vector<AbsEvent> ref;
    for (uint32_t block : {1u, 256u, 4096u}) {
        auto ev = renderSwitch(a, b, SwitchQuantize::Beat, 72000, block, 30000);
        if (ref.empty()) ref = ev;
        CHECK(ev == ref);
        CHECK_EQ(onsOf(ev, 38).front().sample, 48000);
        CHECK_EQ(onsOf(ev, 36).back().sample, 42000);
    }
}

TEST(immediate_switch_and_stopped_switch)
{
    const Pattern a = allStepsPattern(36, Resolution::Sixteenth), b = allStepsPattern(38, Resolution::Sixteenth);
    auto ev = renderSwitch(a, b, SwitchQuantize::Now, 48000, 512, 20000);
    // first block starting at/after 20000 is 20480; step boundaries are multiples of 6000 -> B from 24000
    CHECK(!onsOf(ev, 38).empty());
    CHECK_EQ(onsOf(ev, 38).front().sample, 24000);
    CHECK_EQ(onsOf(ev, 36).back().sample, 18000);
    // stopped: the request applies at once and nothing is emitted
    auto none = renderSwitch(a, b, SwitchQuantize::Bar, 48000, 512, 0, false);
    CHECK(none.empty());
}

TEST(switcher_plans_targets_cancels_and_recovers_from_jumps)
{
    PatternSwitcher sw; sw.reset(0);
    sw.request({1, SwitchQuantize::Bar}); sw.plan(true, 5.5, 4.0);
    CHECK(sw.pending()); CHECK_EQ(sw.switchAt(), 8 * kUnitsPerQuarter);
    sw.request({0, SwitchQuantize::Bar}); sw.plan(true, 5.6, 4.0);      // back to the playing one cancels
    CHECK(!sw.pending());
    sw.request({2, SwitchQuantize::Beat}); sw.plan(true, 3.0, 4.0);     // exactly on a beat: switch there
    CHECK_EQ(sw.switchAt(), 3 * kUnitsPerQuarter);
    sw.request({2, SwitchQuantize::Bar}); sw.plan(true, 100.0, 3.0);    // 3/4 bars; 102 is the next bar line
    CHECK_EQ(sw.switchAt(), 102 * kUnitsPerQuarter);
    sw.plan(true, 4.0, 3.0);                                            // transport jumped back: re-resolve
    CHECK_EQ(sw.switchAt(), 6 * kUnitsPerQuarter);
    sw.plan(false, 0.0, 4.0);                                           // stopped: applied immediately
    CHECK(!sw.pending()); CHECK_EQ(sw.current(), 2);
    sw.request({1, SwitchQuantize::Bar}); sw.plan(false, 0.0, 4.0); CHECK_EQ(sw.current(), 1);
}

TEST(spsc_queue_preserves_order_and_reports_full)
{
    SpscQueue<int, 4> q; int v;
    CHECK(!q.pop(v));
    for (int i = 0; i < 4; ++i) CHECK(q.push(i));
    CHECK(!q.push(99));
    for (int i = 0; i < 4; ++i) { CHECK(q.pop(v)); CHECK_EQ(v, i); }
    CHECK(!q.pop(v));
    for (int i = 0; i < 10; ++i) { CHECK(q.push(i)); CHECK(q.pop(v)); CHECK_EQ(v, i); } // wraps
}

int main() { return testfw::runAll(); }
