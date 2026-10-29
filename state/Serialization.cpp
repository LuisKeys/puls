#include "Serialization.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace puls {

using json = nlohmann::json;

namespace {

const char* const kResolutionNames[] = {"1/4", "1/8", "1/16", "1/32", "1/64", "1/8T", "1/16T", "1/32T"};
static_assert(sizeof(kResolutionNames) / sizeof(kResolutionNames[0]) == static_cast<size_t>(Resolution::Count));

bool readInt(const json& j, const char* key, int64_t lo, int64_t hi, int64_t& out, std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_number_integer()) { err = std::string(key) + ": expected an integer"; return false; }
    if (it->is_number_unsigned() && it->get<uint64_t>() > static_cast<uint64_t>(hi)) {
        err = std::string(key) + ": out of range"; return false;
    }
    const int64_t v = it->is_number_unsigned() ? static_cast<int64_t>(it->get<uint64_t>()) : it->get<int64_t>();
    if (v < lo || v > hi) { err = std::string(key) + ": out of range"; return false; }
    out = v;
    return true;
}

template <typename T>
bool readNum(const json& j, const char* key, int64_t lo, int64_t hi, T& out, std::string& err)
{
    int64_t v = out;
    if (!readInt(j, key, lo, hi, v, err)) return false;
    out = static_cast<T>(v);
    return true;
}

bool readBool(const json& j, const char* key, bool& out, std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_boolean()) { err = std::string(key) + ": expected a boolean"; return false; }
    out = it->get<bool>();
    return true;
}

bool readName(const json& j, const char* key, char (&out)[16], std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_string()) { err = std::string(key) + ": expected a string"; return false; }
    const std::string& s = it->get_ref<const std::string&>();
    if (s.size() > sizeof(out) - 1) { err = std::string(key) + ": longer than 15 bytes"; return false; }
    std::memset(out, 0, sizeof(out));
    std::memcpy(out, s.data(), s.size());
    return true;
}

bool readColor(const json& j, const char* key, uint32_t& out, std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (!it->is_string()) { err = std::string(key) + ": expected \"#rrggbb\""; return false; }
    const std::string& s = it->get_ref<const std::string&>();
    if (s.size() != 7 || s[0] != '#') { err = std::string(key) + ": expected \"#rrggbb\""; return false; }
    uint32_t v = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = s[i];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else { err = std::string(key) + ": expected \"#rrggbb\""; return false; }
        v = v * 16 + static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

bool readResolution(const json& j, const char* key, Resolution& out, std::string& err)
{
    const auto it = j.find(key);
    if (it == j.end()) return true;
    if (it->is_string()) {
        const std::string& s = it->get_ref<const std::string&>();
        for (size_t i = 0; i < sizeof(kResolutionNames) / sizeof(kResolutionNames[0]); ++i)
            if (s == kResolutionNames[i]) { out = static_cast<Resolution>(i); return true; }
    }
    err = std::string(key) + ": unknown resolution";
    return false;
}

std::string colorToString(uint32_t c)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06x", c & 0xFFFFFF);
    return buf;
}

bool isDefault(const Step& s)
{
    const Step d;
    return s.active == d.active && s.velocity == d.velocity && s.probability == d.probability
        && s.ratchets == d.ratchets && s.microtiming == d.microtiming && s.gate == d.gate;
}

} // namespace

std::string dumpJson(const json& j)
{
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

json stepToJson(const Step& s)
{
    const Step d;
    json j = json::object();
    if (s.active != d.active) j["active"] = s.active;
    if (s.velocity != d.velocity) j["velocity"] = s.velocity;
    if (s.probability != d.probability) j["probability"] = s.probability;
    if (s.ratchets != d.ratchets) j["ratchets"] = s.ratchets;
    if (s.microtiming != d.microtiming) j["microtiming"] = s.microtiming;
    if (s.gate != d.gate) j["gate"] = s.gate;
    return j;
}

json trackToJson(const Track& t, bool withSteps)
{
    json j;
    j["id"] = t.id;
    j["name"] = std::string(t.name, strnlen(t.name, sizeof(t.name)));
    j["color"] = colorToString(t.color);
    j["note"] = t.note;
    j["channel"] = t.channel;
    j["length"] = t.length;
    j["resolution"] = resolutionName(t.resolution);
    j["phase"] = t.phase;
    j["velocityPct"] = t.velocityPct;
    j["probabilityPct"] = t.probabilityPct;
    j["swing"] = t.swing;
    j["durationPct"] = t.durationPct;
    j["mute"] = t.mute;
    j["solo"] = t.solo;
    if (withSteps) {
        int last = kMaxSteps;
        while (last > 0 && isDefault(t.steps[last - 1])) --last;
        json steps = json::array();
        for (int i = 0; i < last; ++i) steps.push_back(stepToJson(t.steps[i]));
        j["steps"] = std::move(steps);
    }
    return j;
}

json patternToJson(const Pattern& p)
{
    json j;
    j["name"] = std::string(p.name, strnlen(p.name, sizeof(p.name)));
    j["seed"] = p.seed;
    json tracks = json::array();
    for (int i = 0; i < p.trackCount && i < kMaxTracks; ++i) tracks.push_back(trackToJson(p.tracks[i], true));
    j["tracks"] = std::move(tracks);
    return j;
}

const char* resolutionName(Resolution r)
{
    const int i = static_cast<int>(r);
    return kResolutionNames[i < static_cast<int>(Resolution::Count) ? i : 2];
}

json settingsToJson(const Settings& s)
{
    json j;
    j["anchorMode"] = s.anchorMode == AnchorMode::HostAbsolute ? "host" : "restart";
    j["enabled"] = s.enabled;
    j["audition"] = s.audition;
    j["patternSwitch"] = s.patternSwitch == SwitchQuantize::Now ? "now" : s.patternSwitch == SwitchQuantize::Beat ? "beat" : "bar";
    return j;
}

bool readStep(const json& j, Step& out, std::string& err)
{
    if (!j.is_object()) { err = "step: expected an object"; return false; }
    return readBool(j, "active", out.active, err)
        && readNum(j, "velocity", 1, 127, out.velocity, err)
        && readNum(j, "probability", 0, 100, out.probability, err)
        && readNum(j, "ratchets", 1, 8, out.ratchets, err)
        && readNum(j, "microtiming", -50, 50, out.microtiming, err)
        && readNum(j, "gate", 1, 100, out.gate, err);
}

bool readTrackProps(const json& j, Track& out, std::string& err)
{
    if (!j.is_object()) { err = "props: expected an object"; return false; }
    return readName(j, "name", out.name, err)
        && readColor(j, "color", out.color, err)
        && readNum(j, "note", 0, 127, out.note, err)
        && readNum(j, "channel", 0, 15, out.channel, err)
        && readNum(j, "length", 1, kMaxSteps, out.length, err)
        && readResolution(j, "resolution", out.resolution, err)
        && readNum(j, "phase", 0, kMaxSteps - 1, out.phase, err)
        && readNum(j, "velocityPct", 0, 200, out.velocityPct, err)
        && readNum(j, "probabilityPct", 0, 100, out.probabilityPct, err)
        && readNum(j, "swing", 0, 75, out.swing, err)
        && readNum(j, "durationPct", 1, 100, out.durationPct, err)
        && readBool(j, "mute", out.mute, err)
        && readBool(j, "solo", out.solo, err);
}

bool readSettings(const json& j, Settings& out, std::string& err)
{
    if (!j.is_object()) { err = "settings: expected an object"; return false; }
    const auto it = j.find("anchorMode");
    if (it != j.end()) {
        if (!it->is_string()) { err = "anchorMode: expected a string"; return false; }
        const std::string& s = it->get_ref<const std::string&>();
        if (s == "restart") out.anchorMode = AnchorMode::RestartOnStart;
        else if (s == "host") out.anchorMode = AnchorMode::HostAbsolute;
        else { err = "anchorMode: expected \"restart\" or \"host\""; return false; }
    }
    const auto pit = j.find("patternSwitch");
    if (pit != j.end()) {
        if (!pit->is_string()) { err = "patternSwitch: expected a string"; return false; }
        const std::string& q = pit->get_ref<const std::string&>();
        if (q == "now") out.patternSwitch = SwitchQuantize::Now;
        else if (q == "beat") out.patternSwitch = SwitchQuantize::Beat;
        else if (q == "bar") out.patternSwitch = SwitchQuantize::Bar;
        else { err = "patternSwitch: expected \"now\", \"beat\" or \"bar\""; return false; }
    }
    return readBool(j, "enabled", out.enabled, err) && readBool(j, "audition", out.audition, err);
}

json bankSummaryToJson(const Model& m)
{
    json arr = json::array();
    for (const Pattern& p : m.bank) {
        json e;
        e["name"] = std::string(p.name, strnlen(p.name, sizeof(p.name)));
        e["used"] = !isBlank(p);
        arr.push_back(std::move(e));
    }
    return arr;
}

bool readPattern(const json& j, Pattern& out, uint32_t& maxId, std::string& err)
{
    if (!j.is_object()) { err = "pattern: expected an object"; return false; }
    Pattern p;
    if (!readName(j, "name", p.name, err)) return false;
    if (!readNum(j, "seed", 0, 0xFFFFFFFFll, p.seed, err)) return false;
    const auto tit = j.find("tracks");
    if (tit == j.end() || !tit->is_array() || tit->size() > static_cast<size_t>(kMaxTracks)) {
        err = "pattern: tracks must be an array of at most 16";
        return false;
    }
    maxId = 0;
    for (const json& tj : *tit) {
        if (!tj.is_object()) { err = "track: expected an object"; return false; }
        Track& t = p.tracks[p.trackCount];
        int64_t id = 0;
        if (tj.find("id") == tj.end() || !readInt(tj, "id", 1, 0xFFFFFFFEll, id, err)) {
            if (err.empty()) err = "track: missing id";
            return false;
        }
        for (int i = 0; i < p.trackCount; ++i)
            if (p.tracks[i].id == id) { err = "track: duplicate id"; return false; }
        t.id = static_cast<uint32_t>(id);
        if (!readTrackProps(tj, t, err)) return false;
        const auto sit = tj.find("steps");
        if (sit != tj.end()) {
            if (!sit->is_array() || sit->size() > static_cast<size_t>(kMaxSteps)) { err = "steps: expected an array of at most 64"; return false; }
            for (size_t i = 0; i < sit->size(); ++i)
                if (!readStep((*sit)[i], t.steps[i], err)) return false;
        }
        maxId = std::max(maxId, t.id);
        ++p.trackCount;
    }
    out = p;
    return true;
}

std::string saveState(const Model& m)
{
    json j;
    j["schemaVersion"] = kSchemaVersion;
    j["selected"] = m.selected;
    json pats = json::array();
    for (const Pattern& p : m.bank) pats.push_back(patternToJson(p));
    j["patterns"] = std::move(pats);
    j["settings"] = settingsToJson(m.settings);
    return dumpJson(j);
}

// Upgrades older documents to the current schema in place. v1 held a single "pattern".
static bool migrate(json& j, std::string& err)
{
    const auto ver = j.find("schemaVersion");
    if (ver == j.end() || !ver->is_number_integer()) { err = "state: unsupported schemaVersion"; return false; }
    int64_t v = ver->get<int64_t>();
    if (v == 1) {
        const auto pit = j.find("pattern");
        if (pit == j.end() || !pit->is_object()) { err = "state: missing pattern"; return false; }
        json up;
        up["schemaVersion"] = 2;
        up["selected"] = 0;
        up["patterns"] = json::array({*pit});
        const auto sit = j.find("settings");
        if (sit != j.end()) up["settings"] = *sit;
        j = std::move(up);
        v = 2;
    }
    if (v != kSchemaVersion) { err = "state: unsupported schemaVersion"; return false; }
    return true;
}

bool loadState(const char* text, Model& out, std::string& err)
{
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) { err = "state: not a JSON object"; return false; }
    if (!migrate(j, err)) return false;

    const auto pit = j.find("patterns");
    if (pit == j.end() || !pit->is_array() || pit->empty() || pit->size() > static_cast<size_t>(kBankSize)) {
        err = "state: patterns must be an array of 1 to 16";
        return false;
    }
    auto m = std::make_unique<Model>();
    uint32_t maxId = 0;
    for (size_t i = 0; i < pit->size(); ++i) {
        uint32_t id = 0;
        if (!readPattern((*pit)[i], m->bank[i], id, err)) return false;
        maxId = std::max(maxId, id);
    }
    for (size_t i = pit->size(); i < static_cast<size_t>(kBankSize); ++i) {   // slots missing from the document
        makeBlankPattern(m->bank[i], static_cast<int>(i) + 1);
        maxId = std::max<uint32_t>(maxId, m->bank[i].trackCount);
    }
    int64_t sel = 0;
    if (!readInt(j, "selected", 0, kBankSize - 1, sel, err)) return false;
    m->selected = static_cast<int>(sel);
    m->nextTrackId = maxId + 1;

    const auto sit = j.find("settings");
    if (sit != j.end() && !readSettings(*sit, m->settings, err)) return false;

    out.bank = m->bank;
    out.selected = m->selected;
    out.settings = m->settings;
    out.nextTrackId = m->nextTrackId;
    out.history.clear();   // clipboard is session state and survives a restore
    return true;
}

} // namespace puls
