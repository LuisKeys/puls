#include "TestFramework.hpp"
#include "Bridge.hpp"
#include "Serialization.hpp"
#include "SnapshotExchange.hpp"
#include "TestHelpers.hpp"
#include <atomic>
#include <random>
#include <thread>

using namespace puls;
using json = nlohmann::json;

static Model defaultModel() { Model m; makeDefaultBank(m); return m; }
static json one(const Bridge::Result& r) { return r.events.empty() ? json() : json::parse(r.events[0]); }
static std::string typeOf(const Bridge::Result& r) { const json j = one(r); return j.is_object() ? j.value("type", "") : "none"; }

TEST(state_round_trip_is_stable)
{
    Model m = defaultModel();
    m.pattern().tracks[2].steps[63].active = true;
    m.pattern().tracks[2].steps[63].ratchets = 4;
    m.pattern().tracks[1].length = 7;
    m.pattern().tracks[1].resolution = Resolution::SixteenthTriplet;
    m.settings.anchorMode = AnchorMode::HostAbsolute;
    const std::string a = saveState(m);
    Model b; std::string err;
    CHECK(loadState(a.c_str(), b, err));
    CHECK(saveState(b) == a);
    CHECK_EQ(b.pattern().trackCount, 8);
    CHECK_EQ(b.nextTrackId, 9);
    CHECK(b.pattern().tracks[2].steps[63].active);
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
    Model m = defaultModel();
    const std::string before = saveState(m);
    for (const char* s : bad) { std::string err; CHECK(!loadState(s, m, err)); CHECK(!err.empty()); }
    CHECK(saveState(m) == before);
    std::string err;
    CHECK(loadState("{\"schemaVersion\":1,\"pattern\":{\"tracks\":[{\"id\":5}]}}", m, err)); // minimal valid
    CHECK_EQ(m.pattern().trackCount, 1);           // v1 document: migrated into slot 0
    CHECK(m.nextTrackId > 5);
    CHECK_EQ(m.bank[1].trackCount, 8);             // the other slots are blank default patterns
    CHECK(isBlank(m.bank[1]));
}

TEST(toggle_and_velocity_commands)
{
    Model m = defaultModel(); Bridge b(m);
    CHECK(!m.pattern().tracks[0].steps[1].active);
    auto r = b.handle("{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    CHECK(r.changed); CHECK(typeOf(r) == "stepChanged");
    CHECK(m.pattern().tracks[0].steps[1].active);
    CHECK(one(r)["data"]["active"] == true);
    r = b.handle("{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":1,\"velocity\":42}");
    CHECK(r.changed); CHECK_EQ(m.pattern().tracks[0].steps[1].velocity, 42);
    r = b.handle("{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    CHECK(!m.pattern().tracks[0].steps[1].active);
    CHECK_EQ(m.pattern().tracks[0].steps[1].velocity, 42); // velocity survives deactivation
}

TEST(invalid_commands_report_errors_and_change_nothing)
{
    Model m = defaultModel(); Bridge b(m);
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
    Model m = defaultModel(); Bridge b(m);
    auto r = b.handle("{\"version\":1,\"type\":\"modifyTrack\",\"track\":2,\"props\":{\"length\":5,\"resolution\":\"1/8T\",\"mute\":true,\"name\":\"Rim\"}}");
    CHECK(r.changed); CHECK(typeOf(r) == "trackChanged");
    CHECK_EQ(m.pattern().tracks[1].length, 5);
    CHECK(m.pattern().tracks[1].resolution == Resolution::EighthTriplet);
    CHECK(m.pattern().tracks[1].mute);
    CHECK(one(r)["track"]["name"] == "Rim");

    // add up to 16, then refuse
    while (m.pattern().trackCount < 16) CHECK(b.handle("{\"version\":1,\"type\":\"addTrack\"}").changed);
    CHECK(typeOf(b.handle("{\"version\":1,\"type\":\"addTrack\"}")) == "error");
    CHECK_EQ(m.pattern().tracks[15].id, 16);

    // remove from the middle keeps order and ids stable; new tracks never reuse an id
    r = b.handle("{\"version\":1,\"type\":\"removeTrack\",\"track\":3}");
    CHECK(r.changed); CHECK_EQ(m.pattern().trackCount, 15);
    CHECK_EQ(m.pattern().tracks[2].id, 4);
    b.handle("{\"version\":1,\"type\":\"addTrack\",\"props\":{\"note\":60}}");
    CHECK_EQ(m.pattern().tracks[15].id, 17); CHECK_EQ(m.pattern().tracks[15].note, 60);

    // the last track cannot be removed
    Model one1 = defaultModel(); one1.pattern().trackCount = 1; Bridge b1(one1);
    CHECK(typeOf(b1.handle("{\"version\":1,\"type\":\"removeTrack\",\"track\":1}")) == "error");
}

TEST(hello_returns_full_state_and_settings)
{
    Model m = defaultModel(); Bridge b(m);
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
    Model m = defaultModel(); Bridge b(m);
    b.handle("{\"version\":1,\"type\":\"toggleStep\",\"track\":3,\"step\":0}"); // clap on step 0
    auto ev = th::renderAll(m.pattern(), 24000, 512);
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

static Bridge::Result cmd(Bridge& b, const std::string& s) { return b.handle(s.c_str()); }

TEST(undo_redo_restores_pattern_states)
{
    Model m = defaultModel(); Bridge b(m);
    const std::string original = saveState(m);
    CHECK(typeOf(cmd(b, "{\"version\":1,\"type\":\"undo\"}")) == "error"); // empty history
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    cmd(b, "{\"version\":1,\"type\":\"modifyTrack\",\"track\":2,\"props\":{\"length\":9}}");
    cmd(b, "{\"version\":1,\"type\":\"removeTrack\",\"track\":3}");
    const std::string edited = saveState(m);
    CHECK(m.history.canUndo()); CHECK(!m.history.canRedo());

    auto r = cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(r.changed); CHECK_EQ(r.events.size(), 2); CHECK(json::parse(r.events[0])["type"] == "patternChanged");
    CHECK(json::parse(r.events[1])["canRedo"] == true);
    CHECK_EQ(m.pattern().trackCount, 8); CHECK_EQ(m.pattern().tracks[2].id, 3);  // removed track is back
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(saveState(m) == original);
    CHECK(!m.history.canUndo());
    cmd(b, "{\"version\":1,\"type\":\"redo\"}"); cmd(b, "{\"version\":1,\"type\":\"redo\"}"); cmd(b, "{\"version\":1,\"type\":\"redo\"}");
    CHECK(saveState(m) == edited);
    CHECK(typeOf(cmd(b, "{\"version\":1,\"type\":\"redo\"}")) == "error");

    // a new edit after undo discards the redo branch
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(m.history.canRedo());
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":5}");
    CHECK(!m.history.canRedo());
}

TEST(velocity_drag_gesture_is_one_undo_step)
{
    Model m = defaultModel(); Bridge b(m);
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":0}"); // kick step 0 is active -> now off
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":0}"); // on again, velocity 100
    for (int v = 101; v <= 120; ++v)
        cmd(b, "{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":0,\"velocity\":" + std::to_string(v) + ",\"gesture\":7}");
    CHECK_EQ(m.pattern().tracks[0].steps[0].velocity, 120);
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK_EQ(m.pattern().tracks[0].steps[0].velocity, 100);          // whole drag reverted at once
    CHECK(m.pattern().tracks[0].steps[0].active);
    // a second drag with a different gesture id is its own step
    cmd(b, "{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":0,\"velocity\":50,\"gesture\":8}");
    cmd(b, "{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":0,\"velocity\":60,\"gesture\":9}");
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK_EQ(m.pattern().tracks[0].steps[0].velocity, 50);
    CHECK(typeOf(cmd(b, "{\"version\":1,\"type\":\"setStepVelocity\",\"track\":1,\"step\":0,\"velocity\":5,\"gesture\":-1}")) == "error");
}

TEST(slider_gestures_coalesce_for_step_and_track_edits)
{
    Model m = defaultModel(); Bridge b(m);
    for (int v = 1; v <= 30; ++v) {
        cmd(b, "{\"version\":1,\"type\":\"modifyTrack\",\"track\":1,\"gesture\":4,\"props\":{\"swing\":" + std::to_string(v) + "}}");
        cmd(b, "{\"version\":1,\"type\":\"modifyStep\",\"track\":2,\"step\":4,\"gesture\":5,\"props\":{\"probability\":" + std::to_string(100 - v) + "}}");
    }
    // interleaved targets with distinct gesture ids are still separate runs: each interleave breaks the run
    CHECK_EQ(m.pattern().tracks[0].swing, 30); CHECK_EQ(m.pattern().tracks[1].steps[4].probability, 70);
    Model m2 = defaultModel(); Bridge b2(m2);
    for (int v = 1; v <= 30; ++v) cmd(b2, "{\"version\":1,\"type\":\"modifyTrack\",\"track\":1,\"gesture\":4,\"props\":{\"swing\":" + std::to_string(v) + "}}");
    for (int v = 1; v <= 30; ++v) cmd(b2, "{\"version\":1,\"type\":\"modifyStep\",\"track\":2,\"step\":4,\"gesture\":5,\"props\":{\"ratchets\":" + std::to_string(1 + v % 8) + "}}");
    cmd(b2, "{\"version\":1,\"type\":\"undo\"}");
    CHECK_EQ(m2.pattern().tracks[1].steps[4].ratchets, 1);   // whole ratchet drag reverted
    CHECK_EQ(m2.pattern().tracks[0].swing, 30);
    cmd(b2, "{\"version\":1,\"type\":\"undo\"}");
    CHECK_EQ(m2.pattern().tracks[0].swing, 0);               // whole swing drag reverted
    CHECK(!m2.history.canUndo());
    // no gesture id: every command is its own step
    cmd(b2, "{\"version\":1,\"type\":\"modifyTrack\",\"track\":1,\"props\":{\"phase\":1}}");
    cmd(b2, "{\"version\":1,\"type\":\"modifyTrack\",\"track\":1,\"props\":{\"phase\":2}}");
    cmd(b2, "{\"version\":1,\"type\":\"undo\"}");
    CHECK_EQ(m2.pattern().tracks[0].phase, 1);
}

TEST(settings_and_failed_commands_do_not_touch_history)
{
    Model m = defaultModel(); Bridge b(m);
    cmd(b, "{\"version\":1,\"type\":\"setSettings\",\"settings\":{\"enabled\":false}}");
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":99,\"step\":0}");
    CHECK(!m.history.canUndo());
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(!m.settings.enabled);                                      // undo leaves settings alone
    std::string err;
    CHECK(loadState(saveState(m).c_str(), m, err));                  // a restored project starts with no history
    CHECK(!m.history.canUndo() && !m.history.canRedo());
    CHECK(json::parse(b.initEvent())["canUndo"] == false);
}

static bool hasEvent(const Bridge::Result& r, const char* type)
{
    for (const auto& e : r.events) if (json::parse(e).value("type", "") == type) return true;
    return false;
}

TEST(bank_state_round_trip_and_v1_migration)
{
    Model m = defaultModel(); Bridge b(m);
    cmd(b, "{\"version\":1,\"type\":\"selectPattern\",\"index\":5}");
    cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":2,\"step\":9}");
    cmd(b, "{\"version\":1,\"type\":\"renamePattern\",\"index\":5,\"name\":\"Breakdown\"}");
    cmd(b, "{\"version\":1,\"type\":\"setSettings\",\"settings\":{\"patternSwitch\":\"beat\"}}");
    const std::string a = saveState(m);
    Model c; std::string err;
    CHECK(loadState(a.c_str(), c, err));
    CHECK(saveState(c) == a);
    CHECK_EQ(c.selected, 5);
    CHECK(c.bank[5].tracks[1].steps[9].active);
    CHECK(std::string(c.bank[5].name) == "Breakdown");
    CHECK(c.settings.patternSwitch == SwitchQuantize::Beat);
    CHECK(json::parse(a)["schemaVersion"] == 2);

    // a v1 document (single pattern) lands in slot 0, rest blank
    Model d; err.clear();
    CHECK(loadState("{\"schemaVersion\":1,\"pattern\":{\"name\":\"Old\",\"tracks\":[{\"id\":3,\"steps\":[{\"active\":true}]}]}}", d, err));
    CHECK(std::string(d.bank[0].name) == "Old"); CHECK(d.bank[0].tracks[0].steps[0].active);
    CHECK_EQ(d.selected, 0);
    for (int i = 1; i < kBankSize; ++i) CHECK(isBlank(d.bank[i]));

    // fewer than 16 patterns: missing slots are blank; selected must be in range
    CHECK(loadState("{\"schemaVersion\":2,\"selected\":1,\"patterns\":[{\"tracks\":[{\"id\":1}]},{\"tracks\":[{\"id\":1}]}]}", d, err));
    CHECK_EQ(d.selected, 1); CHECK(isBlank(d.bank[15]));
    const char* bad[] = {
        "{\"schemaVersion\":2,\"selected\":16,\"patterns\":[{\"tracks\":[]}]}",
        "{\"schemaVersion\":2,\"patterns\":[]}",
        "{\"schemaVersion\":2,\"patterns\":\"x\"}",
        "{\"schemaVersion\":2,\"patterns\":[{\"tracks\":[{\"id\":1,\"length\":0}]}]}",
        "{\"schemaVersion\":2,\"patterns\":[{\"tracks\":[]}],\"settings\":{\"patternSwitch\":\"later\"}}",
        "{\"schemaVersion\":3,\"patterns\":[{\"tracks\":[]}]}",
    };
    const std::string before = saveState(d);
    for (const char* x : bad) { std::string e2; CHECK(!loadState(x, d, e2)); }
    CHECK(saveState(d) == before);
}

TEST(select_pattern_views_requests_and_validates)
{
    Model m = defaultModel(); Bridge b(m);
    auto r = cmd(b, "{\"version\":1,\"type\":\"selectPattern\",\"index\":3}");
    CHECK(!r.changed); CHECK(r.switchRequested);
    CHECK_EQ(r.request.index, 3); CHECK(r.request.quantize == SwitchQuantize::Bar);   // default setting
    CHECK_EQ(m.selected, 3);
    const json e = one(r); CHECK(e["type"] == "patternChanged"); CHECK(e["selected"] == 3);
    r = cmd(b, "{\"version\":1,\"type\":\"selectPattern\",\"index\":0,\"quantize\":\"now\"}");
    CHECK(r.request.quantize == SwitchQuantize::Now);
    cmd(b, "{\"version\":1,\"type\":\"setSettings\",\"settings\":{\"patternSwitch\":\"beat\"}}");
    r = cmd(b, "{\"version\":1,\"type\":\"selectPattern\",\"index\":1}");
    CHECK(r.request.quantize == SwitchQuantize::Beat);
    for (const char* bad : {"{\"version\":1,\"type\":\"selectPattern\",\"index\":16}", "{\"version\":1,\"type\":\"selectPattern\"}",
                            "{\"version\":1,\"type\":\"selectPattern\",\"index\":1,\"quantize\":\"soon\"}"}) {
        r = cmd(b, bad); CHECK(typeOf(r) == "error"); CHECK(!r.switchRequested);
    }
    CHECK_EQ(m.selected, 1);
    CHECK(!m.history.canUndo());   // viewing is not an edit
}

TEST(pattern_bank_operations_and_their_undo)
{
    Model m = defaultModel(); Bridge b(m);
    // rename (a pattern that is not selected) -> bankChanged, undo brings the old name back and shows that slot
    auto r = cmd(b, "{\"version\":1,\"type\":\"renamePattern\",\"index\":4,\"name\":\"Fill\"}");
    CHECK(r.changed); CHECK(hasEvent(r, "bankChanged")); CHECK(!hasEvent(r, "patternChanged"));
    CHECK(std::string(m.bank[4].name) == "Fill");
    for (const char* bad : {"{\"version\":1,\"type\":\"renamePattern\",\"index\":4,\"name\":\"\"}",
                            "{\"version\":1,\"type\":\"renamePattern\",\"index\":4,\"name\":\"0123456789abcdef\"}",
                            "{\"version\":1,\"type\":\"renamePattern\",\"index\":4,\"name\":5}"})
        CHECK(typeOf(cmd(b, bad)) == "error");
    r = cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(std::string(m.bank[4].name) == "Pattern 5"); CHECK_EQ(m.selected, 4); CHECK(hasEvent(r, "patternChanged"));
    cmd(b, "{\"version\":1,\"type\":\"redo\"}");
    CHECK(std::string(m.bank[4].name) == "Fill");

    // paste needs a clipboard; copy -> paste overwrites the target, undoable
    CHECK(typeOf(cmd(b, "{\"version\":1,\"type\":\"pastePattern\",\"index\":2}")) == "error");
    r = cmd(b, "{\"version\":1,\"type\":\"copyPattern\",\"index\":0}");
    CHECK(!r.changed); CHECK(typeOf(r) == "clipboard"); CHECK(m.hasClipboard);
    const std::string groove = patternToJson(m.bank[0]).dump();
    cmd(b, "{\"version\":1,\"type\":\"pastePattern\",\"index\":2}");
    CHECK(patternToJson(m.bank[2]).dump() == groove);
    CHECK(!isBlank(m.bank[2]));
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(isBlank(m.bank[2]));

    // duplicate goes to the next blank slot after the source and selects it; nothing is switched on the audio side
    m.selected = 0;
    r = cmd(b, "{\"version\":1,\"type\":\"duplicatePattern\",\"index\":0}");
    CHECK(r.changed); CHECK(!r.switchRequested); CHECK_EQ(m.selected, 1);
    CHECK(patternToJson(m.bank[1]).dump() == patternToJson(m.bank[0]).dump());
    r = cmd(b, "{\"version\":1,\"type\":\"duplicatePattern\",\"index\":0}");
    CHECK_EQ(m.selected, 2);                                           // slot 1 is taken now
    for (int i = 0; i < kBankSize; ++i) if (isBlank(m.bank[i])) m.bank[i].tracks[0].steps[0].active = true;
    CHECK(typeOf(cmd(b, "{\"version\":1,\"type\":\"duplicatePattern\",\"index\":0}")) == "error"); // no free slot

    // clear keeps tracks, drops steps
    r = cmd(b, "{\"version\":1,\"type\":\"clearPattern\",\"index\":0}");
    CHECK(r.changed); CHECK(isBlank(m.bank[0])); CHECK_EQ(m.bank[0].trackCount, 8);
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(!isBlank(m.bank[0]));
    CHECK(typeOf(cmd(b, "{\"version\":1,\"type\":\"clearPattern\",\"index\":16}")) == "error");
}

TEST(bank_summary_follows_step_edits)
{
    Model m = defaultModel(); Bridge b(m);
    cmd(b, "{\"version\":1,\"type\":\"selectPattern\",\"index\":7}");
    auto r = cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":0}");
    CHECK(hasEvent(r, "bankChanged"));                     // slot 8 became "used"
    r = cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":1,\"step\":1}");
    CHECK(!hasEvent(r, "bankChanged"));                    // still used: no event
    CHECK(json::parse(b.initEvent())["bank"][7]["used"] == true);
    CHECK_EQ((int)json::parse(b.initEvent())["bank"].size(), 16);
}

TEST(edit_steps_batch_is_atomic_and_one_undo_step)
{
    Model m = defaultModel(); Bridge b(m);
    const std::string before = saveState(m);
    auto r = cmd(b, "{\"version\":1,\"type\":\"editSteps\",\"cells\":["
        "{\"track\":1,\"step\":2,\"props\":{\"active\":true,\"velocity\":90}},"
        "{\"track\":2,\"step\":3,\"props\":{\"active\":true}},"
        "{\"track\":1,\"step\":4,\"props\":{\"ratchets\":3}}]}");
    CHECK(r.changed); CHECK(hasEvent(r, "stepsChanged"));
    CHECK_EQ((int)one(r)["cells"].size(), 3);
    CHECK(m.pattern().tracks[0].steps[2].active); CHECK_EQ(m.pattern().tracks[0].steps[2].velocity, 90);
    CHECK_EQ(m.pattern().tracks[0].steps[4].ratchets, 3);
    cmd(b, "{\"version\":1,\"type\":\"undo\"}");
    CHECK(saveState(m) == before);                         // the whole batch reverted at once
    CHECK(!m.history.canUndo());

    // one bad cell rejects everything
    const char* bad[] = {
        "{\"version\":1,\"type\":\"editSteps\",\"cells\":[{\"track\":1,\"step\":2,\"props\":{\"active\":true}},{\"track\":99,\"step\":0,\"props\":{}}]}",
        "{\"version\":1,\"type\":\"editSteps\",\"cells\":[{\"track\":1,\"step\":2,\"props\":{\"active\":true}},{\"track\":1,\"step\":64,\"props\":{}}]}",
        "{\"version\":1,\"type\":\"editSteps\",\"cells\":[{\"track\":1,\"step\":2,\"props\":{\"active\":true}},{\"track\":1,\"step\":0,\"props\":{\"velocity\":0}}]}",
        "{\"version\":1,\"type\":\"editSteps\",\"cells\":[]}", "{\"version\":1,\"type\":\"editSteps\"}",
        "{\"version\":1,\"type\":\"editSteps\",\"cells\":[5]}",
    };
    for (const char* x : bad) { r = cmd(b, x); CHECK(typeOf(r) == "error"); CHECK(!r.changed); }
    CHECK(saveState(m) == before);
}

// ---- Phase 5: fuzzing ----

// Every field of every pattern is inside the ranges the engine and the UI assume.
static bool modelValid(const Model& m)
{
    if (m.selected < 0 || m.selected >= kBankSize) return false;
    for (const Pattern& p : m.bank) {
        if (p.trackCount > kMaxTracks || std::memchr(p.name, 0, sizeof(p.name)) == nullptr) return false;
        for (int t = 0; t < p.trackCount; ++t) {
            const Track& tr = p.tracks[t];
            if (tr.id == 0 || tr.note > 127 || tr.channel > 15 || tr.length < 1 || tr.length > kMaxSteps || tr.phase >= kMaxSteps ||
                int(tr.resolution) >= int(Resolution::Count) || tr.velocityPct > 200 || tr.probabilityPct > 100 || tr.swing > 75 ||
                tr.durationPct < 1 || tr.durationPct > 100 || std::memchr(tr.name, 0, sizeof(tr.name)) == nullptr)
                return false;
            for (int u = 0; u < t; ++u) if (p.tracks[u].id == tr.id) return false;
            for (const Step& st : tr.steps)
                if (st.velocity < 1 || st.velocity > 127 || st.probability > 100 || st.ratchets < 1 || st.ratchets > 8 ||
                    st.microtiming < -50 || st.microtiming > 50 || st.gate < 1 || st.gate > 100)
                    return false;
        }
    }
    return true;
}

static bool roundTrips(const Model& m)
{
    Model c; std::string err; const std::string a = saveState(m);
    return loadState(a.c_str(), c, err) && saveState(c) == a;
}

TEST(fuzz_commands_never_break_the_model)
{
    std::mt19937 rng(12345);
    auto R = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const char* types[] = {"hello", "toggleStep", "setStepVelocity", "modifyStep", "modifyTrack", "editSteps", "addTrack", "removeTrack",
                           "setSettings", "selectPattern", "renamePattern", "copyPattern", "pastePattern", "duplicatePattern",
                           "clearPattern", "undo", "redo", "bogus"};
    const char* trackKeys[] = {"name", "color", "note", "channel", "length", "resolution", "phase", "velocityPct", "probabilityPct",
                               "swing", "durationPct", "mute", "solo"};
    const char* stepKeys[] = {"active", "velocity", "probability", "ratchets", "microtiming", "gate"};
    // Mostly plausible values, regularly hostile ones: wrong types, out of range, huge, negative, floats, unicode.
    auto value = [&]() -> json {
        switch (R(0, 17)) {
            case 0: return nullptr; case 1: return true; case 2: return "x"; case 3: return -1; case 4: return 0; case 5: return 1;
            case 6: return 15; case 7: return 16; case 8: return 63; case 9: return 64; case 10: return 127; case 11: return 128;
            case 12: return 4294967296ull; case 13: return 1.5; case 14: return "#12ab34"; case 15: return "1/8T";
            case 16: return "\xc3\xa9\xe2\x82\xac"; default: return R(-60, 210);
        }
    };
    auto props = [&](const char* const* keys, int n) {
        json o = json::object();
        for (int i = R(0, 4); i > 0; --i) o[keys[R(0, n - 1)]] = value();
        return o;
    };
    Model m = defaultModel(); Bridge b(m);
    int errors = 0, applied = 0;
    for (int it = 0; it < 6000; ++it) {
        json c = json::object();
        if (R(0, 30) != 0) c["version"] = 1; else c["version"] = value();
        c["type"] = R(0, 40) == 0 ? value() : json(types[R(0, 17)]);
        if (R(0, 6) != 0) { const auto& tr = m.pattern().tracks; c["track"] = R(0, 3) == 0 ? value() : json(tr[R(0, std::max(0, m.pattern().trackCount - 1))].id); }
        if (R(0, 4) != 0) c["step"] = R(0, 3) == 0 ? value() : json(R(0, 63));
        if (R(0, 4) != 0) c["index"] = R(0, 3) == 0 ? value() : json(R(0, 15));
        if (R(0, 2) == 0) c["velocity"] = value();
        if (R(0, 3) == 0) c["gesture"] = R(0, 3) == 0 ? value() : json(R(0, 5));
        if (R(0, 3) == 0) c["name"] = value();
        if (R(0, 3) == 0) c["quantize"] = R(0, 2) == 0 ? value() : json("bar");
        if (R(0, 1) == 0) c["props"] = R(0, 8) == 0 ? value() : (R(0, 1) ? props(trackKeys, 13) : props(stepKeys, 6));
        if (R(0, 5) == 0) c["settings"] = R(0, 3) == 0 ? value() : json{{"enabled", value()}, {"anchorMode", value()}, {"patternSwitch", value()}};
        if (R(0, 6) == 0) {
            json cells = json::array();
            for (int i = R(0, 6); i > 0; --i) cells.push_back(R(0, 10) == 0 ? value() : json{{"track", R(0, 3) ? json(m.pattern().tracks[0].id) : value()}, {"step", R(0, 3) ? json(R(0, 63)) : value()}, {"props", props(stepKeys, 6)}});
            c["cells"] = cells;
        }
        const std::string text = R(0, 80) == 0 ? c.dump().substr(0, R(0, 20)) : c.dump();   // sometimes truncated JSON
        const auto r = b.handle(text.c_str());
        for (const std::string& ev : r.events) {
            const json e = json::parse(ev, nullptr, false);
            CHECK(!e.is_discarded() && e.value("version", 0) == 1 && e.contains("type"));
            errors += e.value("type", "") == "error";
        }
        applied += r.changed;
        if (!modelValid(m)) { std::printf("    model invalid after: %s\n", text.c_str()); CHECK(false); break; }
        if (it % 150 == 0 && !roundTrips(m)) { std::printf("    state does not round-trip after: %s\n", text.c_str()); CHECK(false); break; }
    }
    CHECK(roundTrips(m));
    std::printf("    fuzz: %d commands rejected, %d applied\n", errors, applied);
    CHECK(errors > 500); CHECK(applied > 500);   // the fuzzer actually exercises both paths
}

TEST(fuzz_state_documents_are_rejected_or_valid)
{
    std::mt19937 rng(777);
    auto R = [&](size_t lo, size_t hi) { return std::uniform_int_distribution<size_t>(lo, hi)(rng); };
    Model src = defaultModel(); { Bridge b(src); cmd(b, "{\"version\":1,\"type\":\"toggleStep\",\"track\":2,\"step\":7}"); }
    const std::string good = saveState(src);
    Model live = defaultModel();
    const std::string liveBefore = saveState(live);
    int accepted = 0, rejected = 0;
    auto check = [&](const std::string& doc) {
        Model m = live; std::string err;
        const bool ok = loadState(doc.c_str(), m, err);
        if (ok) { ++accepted; CHECK(modelValid(m)); CHECK(roundTrips(m)); }
        else { ++rejected; CHECK(!err.empty()); CHECK(saveState(m) == liveBefore); }   // a rejected document leaves the model alone
    };
    for (size_t len = 0; len < good.size(); len += 37) check(good.substr(0, len));        // truncations
    for (int i = 0; i < 4000; ++i) {                                                       // random byte damage
        std::string d = good;
        for (int k = (int)R(1, 4); k > 0; --k) {
            const size_t pos = R(0, d.size() - 1);
            switch (R(0, 3)) {
                case 0: d[pos] = (char)R(0, 255); break;
                case 1: d.erase(pos, R(1, 8)); break;
                case 2: d.insert(pos, std::string(R(1, 4), "{}[]\":,-0123456789e"[R(0, 19)])); break;
                default: d.replace(pos, std::min<size_t>(R(1, 6), d.size() - pos), R(0, 1) ? "999999999999" : "null"); break;
            }
        }
        check(d);
    }
    // Hostile shapes: absurd nesting must not overflow the stack, huge numbers/strings must be rejected cleanly.
    check(std::string(200000, '[') + std::string(200000, ']'));
    check("{\"schemaVersion\":2,\"patterns\":" + std::string(200000, '[') + "}");
    check("{\"schemaVersion\":2,\"patterns\":[{\"name\":\"" + std::string(1 << 20, 'a') + "\",\"tracks\":[]}]}");
    check("{\"schemaVersion\":2,\"patterns\":[{\"tracks\":[{\"id\":1e999}]}]}");
    check("{\"schemaVersion\":2,\"patterns\":[{\"tracks\":[{\"id\":-0}]}]}");
    std::printf("    fuzz: %d documents accepted, %d rejected\n", accepted, rejected);
    CHECK(rejected > 1000); CHECK(accepted >= 1);
}

int main() { return testfw::runAll(); }
