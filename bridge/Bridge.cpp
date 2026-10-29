#include "Bridge.hpp"
#include "Serialization.hpp"
#include <algorithm>
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

bool readIndex(const json& j, const char* key, int limit, int& out, std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_unsigned() || it->get<uint64_t>() >= static_cast<uint64_t>(limit)) {
        err = std::string(key) + ": expected an index 0.." + std::to_string(limit - 1);
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

json stepJson(const Track& t, int step)
{
    json d;
    d["track"] = t.id;
    d["step"] = step;
    d["data"] = stepToJson(t.steps[step]);
    return d;
}

json stepEvent(const Track& t, int step)
{
    json e = stepJson(t, step);
    e["version"] = kBridgeVersion;
    e["type"] = "stepChanged";
    return e;
}

json patternEvent(const Model& m)
{
    json e = event("patternChanged");
    e["selected"] = m.selected;
    e["pattern"] = patternToJson(m.pattern());
    return e;
}

json historyEvent(const Model& m)
{
    json e = event("history");
    e["canUndo"] = m.history.canUndo();
    e["canRedo"] = m.history.canRedo();
    return e;
}

json clipboardEvent(const Model& m)
{
    json e = event("clipboard");
    e["hasPattern"] = m.hasClipboard;
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
    e["selected"] = model_.selected;
    e["pattern"] = patternToJson(model_.pattern());
    e["bank"] = bankSummaryToJson(model_);
    e["settings"] = settingsToJson(model_.settings);
    e["canUndo"] = model_.history.canUndo();
    e["canRedo"] = model_.history.canRedo();
    e["hasClipboard"] = model_.hasClipboard;
    return dumpJson(e);
}

Bridge::Result Bridge::handle(const char* text)
{
    Result r;
    const json cmd = json::parse(text, nullptr, false);
    std::string type = "?", err;
    auto fail = [&]() { r.events.clear(); r.events.push_back(errorEvent(type, err)); r.changed = r.switchRequested = false; r.auditions.clear(); return r; };

    if (cmd.is_discarded() || !cmd.is_object()) { err = "not a JSON object"; return fail(); }
    const auto vit = cmd.find("version");
    if (vit == cmd.end() || !vit->is_number_integer() || vit->get<int64_t>() != kBridgeVersion) { err = "unsupported version"; return fail(); }
    const auto tit = cmd.find("type");
    if (tit == cmd.end() || !tit->is_string()) { err = "missing type"; return fail(); }
    type = tit->get<std::string>();

    Track* t = nullptr;
    int step = 0;
    // History entry if the command turns out to edit a pattern: by default the selected one.
    int histIndex = model_.selected;
    Pattern before = model_.pattern();
    const std::string bankBefore = dumpJson(bankSummaryToJson(model_));
    uint64_t coalesce = 0;
    bool undoable = true;
    bool selectedChanged = false;   // the selected pattern's content changed other than by a step/track event

    if (type == "hello") {
        r.events.push_back(initEvent());
        undoable = false;
    } else if (type == "toggleStep") {
        if (!readTrackRef(cmd, model_, t, err) || !readIndex(cmd, "step", kMaxSteps, step, err)) return fail();
        t->steps[step].active = !t->steps[step].active;
        r.events.push_back(dumpJson(stepEvent(*t, step)));
        r.changed = true;
    } else if (type == "setStepVelocity") {
        if (!readTrackRef(cmd, model_, t, err) || !readIndex(cmd, "step", kMaxSteps, step, err)) return fail();
        json props = json::object();
        if (cmd.contains("velocity")) props["velocity"] = cmd["velocity"]; else { err = "velocity: required"; return fail(); }
        if (!readGesture(cmd, 1, t->id, step, coalesce, err)) return fail();
        Step s = t->steps[step];
        if (!readStep(props, s, err)) return fail();
        t->steps[step] = s;
        r.events.push_back(dumpJson(stepEvent(*t, step)));
        r.changed = true;
    } else if (type == "modifyStep") {
        if (!readTrackRef(cmd, model_, t, err) || !readIndex(cmd, "step", kMaxSteps, step, err)) return fail();
        const auto pit = cmd.find("props");
        if (pit == cmd.end()) { err = "props: required"; return fail(); }
        if (!readGesture(cmd, 2, t->id, step, coalesce, err)) return fail();
        Step s = t->steps[step];
        if (!readStep(*pit, s, err)) return fail();
        t->steps[step] = s;
        r.events.push_back(dumpJson(stepEvent(*t, step)));
        r.changed = true;
    } else if (type == "editSteps") {
        const auto cit = cmd.find("cells");
        if (cit == cmd.end() || !cit->is_array() || cit->empty() || cit->size() > kMaxBatchCells) {
            err = "cells: expected an array of 1 to 1024";
            return fail();
        }
        // Validate everything against a scratch copy first so the batch is atomic.
        Pattern scratch = model_.pattern();
        std::vector<std::pair<uint32_t, int>> touched;
        for (const json& c : *cit) {
            Track* ct = nullptr;
            int cs = 0;
            if (!c.is_object() || !readTrackRef(c, model_, ct, err) || !readIndex(c, "step", kMaxSteps, cs, err)) {
                if (err.empty()) err = "cells: expected objects";
                return fail();
            }
            const auto pit = c.find("props");
            if (pit == c.end()) { err = "props: required"; return fail(); }
            Track& st = scratch.tracks[ct - model_.pattern().tracks];
            if (!readStep(*pit, st.steps[cs], err)) return fail();
            touched.emplace_back(ct->id, cs);
        }
        model_.pattern() = scratch;
        json e = event("stepsChanged");
        json cells = json::array();
        for (const auto& [id, s] : touched) cells.push_back(stepJson(*model_.findTrack(id), s));
        e["cells"] = std::move(cells);
        r.events.push_back(dumpJson(e));
        r.changed = true;
    } else if (type == "modifyTrack") {
        if (!readTrackRef(cmd, model_, t, err)) return fail();
        const auto pit = cmd.find("props");
        if (pit == cmd.end()) { err = "props: required"; return fail(); }
        if (!readGesture(cmd, 3, t->id, 0, coalesce, err)) return fail();
        const auto prit = cmd.find("preview");
        if (prit != cmd.end() && !prit->is_boolean()) { err = "preview: expected a boolean"; return fail(); }
        const bool preview = prit != cmd.end() && prit->get<bool>();
        Track copy = *t;
        if (!readTrackProps(*pit, copy, err)) return fail();
        if (copy.note != t->note || copy.channel != t->channel)
            if (const Track* o = noteOwner(model_.pattern(), copy.note, copy.channel, t->id)) {
                err = "note " + std::to_string(copy.note) + " is already used by " + o->name + " on this channel";
                return fail();
            }
        json after = trackToJson(copy, false);
        const bool same = after == trackToJson(*t, false);
        *t = copy;
        json e = event("trackChanged");
        e["track"] = std::move(after);
        r.events.push_back(dumpJson(e));
        r.changed = !same;
        undoable = !same;   // e.g. picking the note a track already has: no empty undo step
        if ((model_.settings.audition || preview) && pit->is_object() && pit->contains("note")) {
            // Choosing a note previews it, even when it is the current one; `preview` asks for it regardless of the setting.
            const int vel = std::clamp((100 * t->velocityPct) / 100, 1, 127);
            r.auditions.push_back(AuditionNote{t->channel, t->note, static_cast<uint8_t>(vel)});
        }
    } else if (type == "previewNote") {
        // Plays `note` on the track's channel without changing anything (the note keyboard's occupied keys).
        int note = 0;
        if (!readTrackRef(cmd, model_, t, err) || !readIndex(cmd, "note", 128, note, err)) return fail();
        const int vel = std::clamp((100 * t->velocityPct) / 100, 1, 127);
        r.auditions.push_back(AuditionNote{t->channel, static_cast<uint8_t>(note), static_cast<uint8_t>(vel)});
        undoable = false;
    } else if (type == "addTrack") {
        Pattern& p = model_.pattern();
        if (p.trackCount >= kMaxTracks) { err = "pattern already has 16 tracks"; return fail(); }
        Track nt;
        nt.id = model_.nextTrackId;
        const int n = p.trackCount;
        std::snprintf(nt.name, sizeof(nt.name), "Track %d", n + 1);
        nt.color = kNewTrackColors[n % 8];
        int note = freeNote(p, 36, 1, nt.channel, 0);   // the first free note from C1 up
        if (note < 0) note = freeNote(p, 35, -1, nt.channel, 0);
        nt.note = static_cast<uint8_t>(std::max(note, 0));
        const auto pit = cmd.find("props");
        if (pit != cmd.end() && !readTrackProps(*pit, nt, err)) return fail();
        if (const Track* o = noteOwner(p, nt.note, nt.channel, 0)) {
            err = "note " + std::to_string(nt.note) + " is already used by " + o->name + " on this channel";
            return fail();
        }
        p.tracks[p.trackCount++] = nt;
        ++model_.nextTrackId;
        selectedChanged = true;
        r.changed = true;
    } else if (type == "removeTrack") {
        if (!readTrackRef(cmd, model_, t, err)) return fail();
        Pattern& p = model_.pattern();
        if (p.trackCount <= 1) { err = "cannot remove the last track"; return fail(); }
        const int idx = static_cast<int>(t - p.tracks);
        for (int i = idx; i + 1 < p.trackCount; ++i) p.tracks[i] = p.tracks[i + 1];
        p.tracks[--p.trackCount] = Track{};
        selectedChanged = true;
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
        undoable = false;
    } else if (type == "selectPattern") {
        int idx = 0;
        if (!readIndex(cmd, "index", kBankSize, idx, err)) return fail();
        SwitchQuantize q = model_.settings.patternSwitch;
        const auto qit = cmd.find("quantize");
        if (qit != cmd.end()) {
            const std::string qs = qit->is_string() ? qit->get<std::string>() : "";
            if (qs == "now") q = SwitchQuantize::Now;
            else if (qs == "beat") q = SwitchQuantize::Beat;
            else if (qs == "bar") q = SwitchQuantize::Bar;
            else { err = "quantize: expected \"now\", \"beat\" or \"bar\""; return fail(); }
        }
        model_.selected = idx;
        r.events.push_back(dumpJson(patternEvent(model_)));
        r.switchRequested = true;
        r.request = SwitchRequest{static_cast<uint8_t>(idx), q};
        undoable = false;
    } else if (type == "renamePattern") {
        int idx = 0;
        if (!readIndex(cmd, "index", kBankSize, idx, err)) return fail();
        const auto nit = cmd.find("name");
        if (nit == cmd.end() || !nit->is_string()) { err = "name: expected a string"; return fail(); }
        const std::string& nm = nit->get_ref<const std::string&>();
        if (nm.empty() || nm.size() > sizeof(Pattern::name) - 1) { err = "name: expected 1 to 15 bytes"; return fail(); }
        histIndex = idx; before = model_.bank[idx];
        std::memset(model_.bank[idx].name, 0, sizeof(Pattern::name));
        std::memcpy(model_.bank[idx].name, nm.data(), nm.size());
        selectedChanged = idx == model_.selected;
        r.changed = true;
    } else if (type == "copyPattern") {
        int idx = 0;
        if (!readIndex(cmd, "index", kBankSize, idx, err)) return fail();
        model_.clipboard = model_.bank[idx];
        model_.hasClipboard = true;
        r.events.push_back(dumpJson(clipboardEvent(model_)));
        undoable = false;
    } else if (type == "pastePattern") {
        int idx = 0;
        if (!readIndex(cmd, "index", kBankSize, idx, err)) return fail();
        if (!model_.hasClipboard) { err = "clipboard is empty"; return fail(); }
        histIndex = idx; before = model_.bank[idx];
        model_.bank[idx] = model_.clipboard;
        selectedChanged = idx == model_.selected;
        r.changed = true;
    } else if (type == "duplicatePattern") {
        int idx = 0;
        if (!readIndex(cmd, "index", kBankSize, idx, err)) return fail();
        int dst = -1;   // the next unused (blank) slot after the source, wrapping around
        for (int k = 1; k < kBankSize && dst < 0; ++k)
            if (isBlank(model_.bank[(idx + k) % kBankSize])) dst = (idx + k) % kBankSize;
        if (dst < 0) { err = "no free pattern slot"; return fail(); }
        histIndex = dst; before = model_.bank[dst];
        model_.bank[dst] = model_.bank[idx];
        model_.selected = dst;
        selectedChanged = true;
        r.changed = true;
    } else if (type == "clearPattern") {
        int idx = 0;
        if (!readIndex(cmd, "index", kBankSize, idx, err)) return fail();
        histIndex = idx; before = model_.bank[idx];
        Pattern& p = model_.bank[idx];
        for (int i = 0; i < p.trackCount; ++i)
            for (Step& st : p.tracks[i].steps) st = Step{};
        selectedChanged = idx == model_.selected;
        r.changed = true;
    } else if (type == "undo" || type == "redo") {
        const int idx = type == "undo" ? model_.history.undo(model_.bank.data()) : model_.history.redo(model_.bank.data());
        if (idx < 0) { err = type == "undo" ? "nothing to undo" : "nothing to redo"; return fail(); }
        model_.selected = idx;   // show what changed; playback is not switched
        r.events.push_back(dumpJson(patternEvent(model_)));
        r.events.push_back(dumpJson(historyEvent(model_)));
        r.changed = true;
        undoable = false;
    } else {
        err = "unknown command";
        return fail();
    }

    if (r.changed && undoable) {
        model_.history.record(histIndex, before, coalesce);
        r.events.push_back(dumpJson(historyEvent(model_)));
    }
    if (selectedChanged) r.events.push_back(dumpJson(patternEvent(model_)));
    if (model_.settings.audition && (type == "toggleStep" || type == "modifyStep" || type == "editSteps")) {
        // Step commands never reorder tracks, so `before` lines up with the selected pattern.
        const Pattern& p = model_.pattern();
        for (int i = 0; i < p.trackCount; ++i) {
            const Track& tr = p.tracks[i];
            for (int s = 0; s < kMaxSteps; ++s) {
                if (!tr.steps[s].active || before.tracks[i].steps[s].active) continue;
                const int vel = std::clamp((int(tr.steps[s].velocity) * tr.velocityPct) / 100, 1, 127);
                r.auditions.push_back(AuditionNote{tr.channel, tr.note, static_cast<uint8_t>(vel)});
                break;
            }
        }
    }
    if (type == "duplicatePattern" || type == "pastePattern" || type == "renamePattern" || type == "clearPattern" ||
        type == "undo" || type == "redo" || type == "toggleStep" || type == "setStepVelocity" || type == "modifyStep" ||
        type == "editSteps") {
        json summary = bankSummaryToJson(model_);
        if (dumpJson(summary) != bankBefore) {
            json e = event("bankChanged");
            e["bank"] = std::move(summary);
            r.events.push_back(dumpJson(e));
        }
    }
    return r;
}

} // namespace puls
