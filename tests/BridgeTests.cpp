#include "TestFramework.hpp"
#include "Bridge.hpp"
#include "Serialization.hpp"
#include "SnapshotExchange.hpp"
#include "TestHelpers.hpp"
#include <atomic>
#include <thread>

using namespace puls;
using json = nlohmann::json;

static json one(const Bridge::Result& r) { return r.events.size() == 1 ? json::parse(r.events[0]) : json(); }
static std::string typeOf(const Bridge::Result& r) { const json j = one(r); return j.is_object() ? j.value("type", "") : "none"; }

TEST(state_round_trip_is_stable)
{
    Model m = makeDefaultModel();
    m.pattern.tracks[2].steps[63].active = true;
    m.pattern.tracks[2].steps[63].ratchets = 4;
    m.pattern.tracks[1].length = 7;
    m.pattern.tracks[1].resolution = Resolution::SixteenthTriplet;
    m.settings.anchorMode = AnchorMode::HostAbsolute;
    const std::string a = saveState(m);
    Model b; std::string err;
    CHECK(loadState(a.c_str(), b, err));
    CHECK(saveState(b) == a);
    CHECK_EQ(b.pattern.trackCount, 8);
    CHECK_EQ(b.nextTrackId, 9);
    CHECK(b.pattern.tracks[2].steps[63].active);
    CHECK(b.settings.anchorMode == AnchorMode::HostAbsolute);
}

TEST(corrupt_state_is_rejected_and_leaves_model_untouched)
{
    const char* bad[] = {
        "", "[]", "{}", "{\"schemaVersion\":2,\"pattern\":{\"tracks\":[]}}",
        "{\"schemaVersion\":1}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1,\"length\":65}]}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1},{\"id\":1}]}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1,\"steps\":[{\"velocity\":0}]}]}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1,\"resolution\":\"1/3\"}]}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1,\"color\":\"red\"}]}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1,\"name\":\"0123456789abcdef\"}]}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1}],\"seed\":99999999999}}",
        "{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":1}]},\"settings\":{\"anchorMode\":\"x\"}}",
    };
    Model m = makeDefaultModel();
    const std::string before = saveState(m);
    for (const char* s : bad) { std::string err; CHECK(!loadState(s, m, err)); CHECK(!err.empty()); }
    CHECK(saveState(m) == before);
    std::string err;
    CHECK(loadState("{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":5}]}}", m, err)); // minimal valid
    CHECK_EQ(m.pattern.trackCount, 1);
    CHECK_EQ(m.nextTrackId, 6);
}

TEST(toggle_and_velocity_commands)
{
    Model m = makeDefaultModel(); Bridge b(m);
    CHECK(!m.pattern.tracks[0].steps[1].active);
    auto r = b.handle("{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    CHECK(r.changed); CHECK(typeOf(r) == "stepChanged");
    CHECK(m.pattern.tracks[0].steps[1].active);
    CHECK(one(r)["data"]["active"] == true);
    r = b.handle("{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":1,\"velocity\":42}");
    CHECK(r.changed); CHECK_EQ(m.pattern.tracks[0].steps[1].velocity, 42);
    r = b.handle("{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    CHECK(!m.pattern.tracks[0].steps[1].active);
    CHECK_EQ(m.pattern.tracks[0].steps[1].velocity, 42); // velocity survives deactivation
}

TEST(invalid_commands_report_errors_and_change_nothing)
{
    Model m = makeDefaultModel(); Bridge b(m);
    const std::string before = saveState(m);
    const char* bad[] = {
        "garbage", "[]", "{\"type\":\"hello\"}", "{\"version\":2,\"type\":\"hello\"}", "{\"version\":1}",
        "{\"version\":1,\"type\":\"nope\"}",
        "{\"version\":1,\"type\":\"toggleStep\",\"track\":99,\"step\":0}",
        "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":64}",
        "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":-1}",
        "{\"version\":1,\"type\":\"toggleStep\",\"track\":\"1\",\"step\":0}",
        "{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":0,\"velocity\":128}",
        "{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":0}",
        "{\"version\":1,\"type\":\"modifyStep\",\"track\":1,\"step\":0,\"props\":{\"ratchets\":9}}",
        "{\"version\":1,\"type\":\"modifyTrack\",\"track\":1,\"props\":{\"length\":0}}",
        // partially valid props must be atomic: length is fine, swing is not
        "{\"version\":1,\"type\":\"modifyTrack\",\"track\":1,\"props\":{\"length\":9,\"swing\":99}}",
        "{\"version\":1,\"type\":\"setSettings\",\"settings\":{\"enabled\":1}}",
        "{\"version\":1,\"type\":\"removeTrack\",\"track\":4294967297}",
    };
    for (const char* s : bad) {
        auto r = b.handle(s);
        CHECK(!r.changed); CHECK(typeOf(r) == "error");
    }
    CHECK(saveState(m) == before);
}

TEST(track_commands)
{
    Model m = makeDefaultModel(); Bridge b(m);
    auto r = b.handle("{\"version\":1,\"type\":\"modifyTrack\",\"track\":2,\"props\":{\"length\":5,\"resolution\":\"1/8T\",\"mute\":true,\"name\":\"Rim\"}}");
    CHECK(r.changed); CHECK(typeOf(r) == "trackChanged");
    CHECK_EQ(m.pattern.tracks[1].length, 5);
    CHECK(m.pattern.tracks[1].resolution == Resolution::EighthTriplet);
    CHECK(m.pattern.tracks[1].mute);
    CHECK(one(r)["track"]["name"] == "Rim");

    // add up to 16, then refuse
    while (m.pattern.trackCount < 16) CHECK(b.handle("{\"version\":1,\"type\":\"addTrack\"}").changed);
    CHECK(typeOf(b.handle("{\"version\":1,\"type\":\"addTrack\"}")) == "error");
    CHECK_EQ(m.pattern.tracks[15].id, 16);

    // remove from the middle keeps order and ids stable; new tracks never reuse an id
    r = b.handle("{\"version\":1,\"type\":\"removeTrack\",\"track\":3}");
    CHECK(r.changed); CHECK_EQ(m.pattern.trackCount, 15);
    CHECK_EQ(m.pattern.tracks[2].id, 4);
    b.handle("{\"version\":1,\"type\":\"addTrack\",\"props\":{\"note\":60}}");
    CHECK_EQ(m.pattern.tracks[15].id, 17); CHECK_EQ(m.pattern.tracks[15].note, 60);

    // the last track cannot be removed
    Model one1 = makeDefaultModel(); one1.pattern.trackCount = 1; Bridge b1(one1);
    CHECK(typeOf(b1.handle("{\"version\":1,\"type\":\"removeTrack\",\"track\":1}")) == "error");
}

TEST(hello_returns_full_state_and_settings)
{
    Model m = makeDefaultModel(); Bridge b(m);
    auto r = b.handle("{\"version\":1,\"type\":\"hello\"}");
    CHECK(!r.changed);
    const json e = one(r);
    CHECK(e["type"] == "init");
    CHECK_EQ((int)e["pattern"]["tracks"].size(), 8);
    CHECK(e["settings"]["anchorMode"] == "restart");
    r = b.handle("{\"version\":1,\"type\":\"setSettings\",\"settings\":{\"anchorMode\":\"host\",\"enabled\":false}}");
    CHECK(r.changed); CHECK(m.settings.anchorMode == AnchorMode::HostAbsolute); CHECK(!m.settings.enabled);
}

TEST(edited_model_changes_the_sequenced_output)
{
    Model m = makeDefaultModel(); Bridge b(m);
    b.handle("{\"version\":1,\"type\":\"toggleStep\",\"track\":3,\"step\":0}"); // clap on step 0
    auto ev = th::renderAll(m.pattern, 24000, 512);
    CHECK_EQ((int64_t)th::onsOf(ev, 39).size(), 1);
    CHECK_EQ(th::onsOf(ev, 39)[0].sample, 0);
}

TEST(playheads_follow_each_track_grid)
{
    Pattern p = th::emptyPattern(2);
    p.tracks[0].length = 16; p.tracks[1].length = 5; p.tracks[1].phase = 2;
    Sequencer seq;
    CHECK_EQ(seq.playhead(0), -1);
    // 120 bpm @ 48 kHz: a 1/16 step is 6000 samples; contiguous blocks, so no re-anchoring.
    const double qPerSample = 120.0 / 60.0 / 48000.0;
    seq.process(p, TransportInfo{true, 0.0, 120.0, 48000.0}, 6000);
    CHECK_EQ(seq.playhead(0), 0); CHECK_EQ(seq.playhead(1), 2);
    seq.process(p, TransportInfo{true, 6000 * qPerSample, 120.0, 48000.0}, 6000);
    CHECK_EQ(seq.playhead(0), 1); CHECK_EQ(seq.playhead(1), 3);
    for (int i = 2; i < 6; ++i) seq.process(p, TransportInfo{true, i * 6000 * qPerSample, 120.0, 48000.0}, 6000);
    CHECK_EQ(seq.playhead(0), 5); CHECK_EQ(seq.playhead(1), (5 + 2) % 5);
    Sequencer& s2 = seq;
    s2.process(p, TransportInfo{false, 0.0, 120.0, 48000.0}, 8);
    CHECK_EQ(s2.playhead(0), -1);
}

TEST(snapshot_exchange_hands_over_latest_without_tearing)
{
    SnapshotExchange<std::array<int, 64>> ex;
    bool fresh = true;
    ex.acquire(fresh); CHECK(!fresh);
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (int v = 1; v <= 200000; ++v) { ex.writeSlot().fill(v); ex.publish(); }
        done = true;
    });
    int last = 0; bool torn = false;
    while (!done || last != 200000) {
        const auto& s = ex.acquire(fresh);
        if (fresh) {
            for (int x : s) if (x != s[0]) torn = true;
            if (s[0] < last) torn = true; // never goes backwards
            last = s[0];
        }
        if (done && !fresh && last == 200000) break;
    }
    producer.join();
    CHECK(!torn); CHECK_EQ(last, 200000);
}

int main() { return testfw::runAll(); }
