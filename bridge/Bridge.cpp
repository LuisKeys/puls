#include "Bridge.hpp"
#include "Serialization.hpp"
#include <cstdio>

namespace puls {

using json = nlohmann::json;

namespace {

json event(const char* type)
{
    json j;
    j["version"] = kBridgeVersion;
    j["type"] = type;
    return j;
}

bool readTrackRef(const json& cmd, Model& m, Track*& out, std::string& err)
{
    const auto it = cmd.find("track");
    if (it == cmd.end() || !it->is_number_unsigned()) { err = "track: expected a track id"; return false; }
    const uint64_t id = it->get<uint64_t>();
    out = id <= 0xFFFFFFFFull ? m.findTrack(static_cast<uint32_t>(id)) : nullptr;
    if (out == nullptr) { err = "track: unknown id"; return false; }
    return true;
}

bool readStepRef(const json& cmd, int& out, std::string& err)
{
    const auto it = cmd.find("step");
    if (it == cmd.end() || !it->is_number_unsigned() || it->get<uint64_t>() >= static_cast<uint64_t>(kMaxSteps)) {
        err = "step: expected an index 0..63";
        return false;
    }
    out = static_cast<int>(it->get<uint64_t>());
    return true;
}

// Optional "gesture" id: consecutive commands of the same kind on the same target with the same non-zero id
// are recorded as one undo step (a slider drag, a drag over a cell).
bool readGesture(const json& cmd, uint64_t tag, uint32_t trackId, int step, uint64_t& key, std::string& err)
{
    const auto it = cmd.find("gesture");
    if (it == cmd.end()) return true;
    if (!it->is_number_unsigned() || it->get<uint64_t>() > 0xFFFFFFFFull) { err = "gesture: expected a non-negative integer"; return false; }
    const uint64_t g = it->get<uint64_t>();
    if (g != 0) key = (1ull << 63) | (g << 24) | (tag << 16) | (static_cast<uint64_t>(trackId & 0xFF) << 8) | static_cast<uint64_t>(step & 0xFF);
    return true;
}

json stepEvent(const Track& t, int step)
{
    json e = event("stepChanged");
    e["track"] = t.id;
    e["step"] = step;
    e["data"] = stepToJson(t.steps[step]);
    return e;
}

json historyEvent(const Model& m)
{
    json e = event("history");
    e["canUndo"] = m.history.canUndo();
    e["canRedo"] = m.history.canRedo();
    return e;
}

json patternEvent(const Model& m)
{
    json e = event("patternChanged");
    e["pattern"] = patternToJson(m.pattern);
    return e;
}

constexpr uint32_t kNewTrackColors[] = {0xFF6256, 0xFFAB50, 0xE3D65C, 0x79D86A, 0x48D9B1, 0x50BAF1, 0x9C8CF5, 0xE77FCE};

} // namespace

std::string Bridge::errorEvent(const std::string& command, const std::string& message)
{
    json e = event("error");
    e["command"] = command;
    e["message"] = message;
    return dumpJson(e);
}

std::string Bridge::initEvent() const
{
    json e = event("init");
    e["pattern"] = patternToJson(model_.pattern);
    e["settings"] = settingsToJson(model_.settings);
    e["canUndo"] = model_.history.canUndo();
    e["canRedo"] = model_.history.canRedo();
    return dumpJson(e);
}

Bridge::Result Bridge::handle(const char* text)
{
    Result r;
    const json cmd = json::parse(text, nullptr, false);
    std::string type = "?", err;
    auto fail = [&]() { r.events.push_back(errorEvent(type, err)); return r; };

    if (cmd.is_discarded() || !cmd.is_object()) { err = "not a JSON object"; return fail(); }
    const auto vit = cmd.find("version");
    if (vit == cmd.end() || !vit->is_number_integer() || vit->get<int64_t>() != kBridgeVersion) { err = "unsupported version"; return fail(); }
    const auto tit = cmd.find("type");
    if (tit == cmd.end() || !tit->is_string()) { err = "missing type"; return fail(); }
    type = tit->get<std::string>();

    Track* t = nullptr;
    int step = 0;
    const Pattern before = model_.pattern;   // history entry if the command turns out to edit the pattern
    uint64_t coalesce = 0;

    if (type == "hello") {
        r.events.push_back(initEvent());
    } else if (type == "toggleStep") {
        if (!readTrackRef(cmd, model_, t, err) || !readStepRef(cmd, step, err)) return fail();
        t->steps[step].active = !t->steps[step].active;
        r.events.push_back(dumpJson(stepEvent(*t, step)));
        r.changed = true;
    } else if (type == "setStepVelocity") {
        if (!readTrackRef(cmd, model_, t, err) || !readStepRef(cmd, step, err)) return fail();
        json props = json::object();
        if (cmd.contains("velocity")) props["velocity"] = cmd["velocity"]; else { err = "velocity: required"; return fail(); }
        if (!readGesture(cmd, 1, t->id, step, coalesce, err)) return fail();
        Step s = t->steps[step];
        if (!readStep(props, s, err)) return fail();
        t->steps[step] = s;
        r.events.push_back(dumpJson(stepEvent(*t, step)));
        r.changed = true;
    } else if (type == "modifyStep") {
        if (!readTrackRef(cmd, model_, t, err) || !readStepRef(cmd, step, err)) return fail();
        const auto pit = cmd.find("props");
        if (pit == cmd.end()) { err = "props: required"; return fail(); }
        if (!readGesture(cmd, 2, t->id, step, coalesce, err)) return fail();
        Step s = t->steps[step];
        if (!readStep(*pit, s, err)) return fail();
        t->steps[step] = s;
        r.events.push_back(dumpJson(stepEvent(*t, step)));
        r.changed = true;
    } else if (type == "modifyTrack") {
        if (!readTrackRef(cmd, model_, t, err)) return fail();
        const auto pit = cmd.find("props");
        if (pit == cmd.end()) { err = "props: required"; return fail(); }
        if (!readGesture(cmd, 3, t->id, 0, coalesce, err)) return fail();
        Track copy = *t;
        if (!readTrackProps(*pit, copy, err)) return fail();
        *t = copy;
        json e = event("trackChanged");
        e["track"] = trackToJson(*t, false);
        r.events.push_back(dumpJson(e));
        r.changed = true;
    } else if (type == "addTrack") {
        if (model_.pattern.trackCount >= kMaxTracks) { err = "pattern already has 16 tracks"; return fail(); }
        Track nt;
        nt.id = model_.nextTrackId;
        const int n = model_.pattern.trackCount;
        std::snprintf(nt.name, sizeof(nt.name), "Track %d", n + 1);
        nt.color = kNewTrackColors[n % 8];
        nt.note = static_cast<uint8_t>(36 + n > 127 ? 127 : 36 + n);
        const auto pit = cmd.find("props");
        if (pit != cmd.end() && !readTrackProps(*pit, nt, err)) return fail();
        model_.pattern.tracks[model_.pattern.trackCount++] = nt;
        ++model_.nextTrackId;
        r.events.push_back(dumpJson(patternEvent(model_)));
        r.changed = true;
    } else if (type == "removeTrack") {
        if (!readTrackRef(cmd, model_, t, err)) return fail();
        if (model_.pattern.trackCount <= 1) { err = "cannot remove the last track"; return fail(); }
        const int idx = static_cast<int>(t - model_.pattern.tracks);
        for (int i = idx; i + 1 < model_.pattern.trackCount; ++i) model_.pattern.tracks[i] = model_.pattern.tracks[i + 1];
        model_.pattern.tracks[--model_.pattern.trackCount] = Track{};
        r.events.push_back(dumpJson(patternEvent(model_)));
        r.changed = true;
    } else if (type == "setSettings") {
        const auto sit = cmd.find("settings");
        if (sit == cmd.end()) { err = "settings: required"; return fail(); }
        Settings s = model_.settings;
        if (!readSettings(*sit, s, err)) return fail();
        model_.settings = s;
        json e = event("settings");
        e["settings"] = settingsToJson(s);
        r.events.push_back(dumpJson(e));
        r.changed = true;
    } else if (type == "undo" || type == "redo") {
        if (!(type == "undo" ? model_.history.undo(model_.pattern) : model_.history.redo(model_.pattern))) {
            err = type == "undo" ? "nothing to undo" : "nothing to redo";
            return fail();
        }
        r.events.push_back(dumpJson(patternEvent(model_)));
        r.events.push_back(dumpJson(historyEvent(model_)));
        r.changed = true;
        return r;
    } else {
        err = "unknown command";
        return fail();
    }

    if (r.changed && type != "setSettings") {
        model_.history.record(before, coalesce);
        r.events.push_back(dumpJson(historyEvent(model_)));
    }
    return r;
}

} // namespace puls
