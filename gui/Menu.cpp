#include "Menu.hpp"
#include "Commands.hpp"
#include "Serialization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace puls::gui {

namespace {

constexpr int kResolutions = static_cast<int>(Resolution::Count);

// A field of the generic popover keyed by a FieldKey. The MIDI note field gets the keyboard button.
constexpr FieldDef F(FieldKey k, FieldType type, const char* label, int min = 0, int max = 0, int column = 0)
{
    return FieldDef{static_cast<int>(k), type, label, min, max, column, k == FieldKey::TrackNote};
}

const FieldDef kStepFields[] = {
    F(FieldKey::StepActive, FieldType::Check, "Active"),
    F(FieldKey::StepVelocity, FieldType::Range, "Velocity", 1, 127),
    F(FieldKey::StepProbability, FieldType::Range, "Probability %", 0, 100),
    F(FieldKey::StepRatchets, FieldType::Range, "Ratchets", 1, 8),
    F(FieldKey::StepMicro, FieldType::Range, "Micro-timing %", -50, 50),
    F(FieldKey::StepGate, FieldType::Range, "Gate %", 1, 100),
    F(FieldKey::StepClear, FieldType::Action, "Clear step"),
};

const FieldDef kTrackFields[] = {
    F(FieldKey::TrackName, FieldType::Text, "Name"),
    F(FieldKey::TrackColor, FieldType::Color, "Color"),
    F(FieldKey::TrackNote, FieldType::Range, "MIDI note", 0, 127),
    F(FieldKey::TrackChannel, FieldType::Range, "Channel", 1, 16),
    F(FieldKey::TrackResolution, FieldType::Select, "Resolution", 0, kResolutions - 1),
    F(FieldKey::TrackLength, FieldType::Range, "Length", 1, kMaxSteps),
    F(FieldKey::TrackPhase, FieldType::Range, "Phase", 0, kMaxSteps - 1, 1),
    F(FieldKey::TrackVelocityPct, FieldType::Range, "Velocity %", 0, 200, 1),
    F(FieldKey::TrackProbabilityPct, FieldType::Range, "Probability %", 0, 100, 1),
    F(FieldKey::TrackSwing, FieldType::Range, "Swing %", 0, 75, 1),
    F(FieldKey::TrackDurationPct, FieldType::Range, "Duration %", 1, 100, 1),
    F(FieldKey::TrackMute, FieldType::Check, "Mute", 0, 1, 1),
    F(FieldKey::TrackSolo, FieldType::Check, "Solo", 0, 1, 1),
};

const FieldDef kPatternFields[] = {
    F(FieldKey::PatternName, FieldType::Text, "Name"),
    F(FieldKey::PatternPlayNow, FieldType::Action, "Play now"),
    F(FieldKey::PatternCopy, FieldType::Action, "Copy"),
    F(FieldKey::PatternPaste, FieldType::Action, "Paste (replaces this pattern)"),
    F(FieldKey::PatternDuplicate, FieldType::Action, "Duplicate to next free slot"),
    F(FieldKey::PatternClear, FieldType::Action, "Clear steps"),
};

template <size_t N> constexpr int countOf(const FieldDef (&)[N]) { return static_cast<int>(N); }

const FieldDef* findDef(MenuKind kind, FieldKey k)
{
    const FieldDef* defs = kind == MenuKind::Step ? kStepFields : kind == MenuKind::Track ? kTrackFields : kPatternFields;
    const int n = kind == MenuKind::Step ? countOf(kStepFields) : kind == MenuKind::Track ? countOf(kTrackFields) : countOf(kPatternFields);
    for (int i = 0; i < n; ++i)
        if (keyOf(defs[i]) == k) return &defs[i];
    return nullptr;
}

const Track* findTrack(const Model& m, const MenuTarget& t)
{
    if (t.pattern < 0 || t.pattern >= kBankSize) return nullptr;
    const Pattern& p = m.bank[t.pattern];
    for (int i = 0; i < p.trackCount; ++i)
        if (p.tracks[i].id == t.track) return &p.tracks[i];
    return nullptr;
}

std::string trimmed(const std::string& s)
{
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

} // namespace

// ---- geometry ----------------------------------------------------------------------------------------------------

void Menu::open(const MenuTarget& t, float x, float y, float winW, float winH)
{
    target = t;
    switch (t.kind) {
        case MenuKind::Step:    Popover::open(kStepFields, countOf(kStepFields), x, y, winW, winH); break;
        case MenuKind::Track:   Popover::open(kTrackFields, countOf(kTrackFields), x, y, winW, winH); break;
        case MenuKind::Pattern: Popover::open(kPatternFields, countOf(kPatternFields), x, y, winW, winH); break;
        case MenuKind::None:    close(); return;
    }
}

// ---- model access ------------------------------------------------------------------------------------------------

bool targetValid(const Model& m, const MenuTarget& t)
{
    switch (t.kind) {
        case MenuKind::Step:
            return m.selected == t.pattern && findTrack(m, t) != nullptr && t.step >= 0 && t.step < kMaxSteps;
        case MenuKind::Track:   return m.selected == t.pattern && findTrack(m, t) != nullptr;
        case MenuKind::Pattern: return t.pattern >= 0 && t.pattern < kBankSize;
        case MenuKind::None:    return false;
    }
    return false;
}

std::string menuTitle(const Model& m, const MenuTarget& t)
{
    char buf[64];
    const Track* tr = findTrack(m, t);
    switch (t.kind) {
        case MenuKind::Step:
            std::snprintf(buf, sizeof(buf), "%s \xC2\xB7 step %d", tr ? tr->name : "", t.step + 1);   // ·
            return buf;
        case MenuKind::Track:   return tr ? tr->name : "";
        case MenuKind::Pattern: std::snprintf(buf, sizeof(buf), "Pattern %d", t.pattern + 1); return buf;
        case MenuKind::None:    break;
    }
    return {};
}

int fieldValue(const Model& m, const MenuTarget& t, FieldKey k)
{
    const Track* tr = findTrack(m, t);
    if (tr == nullptr) return 0;
    const Step& st = tr->steps[std::clamp(t.step, 0, kMaxSteps - 1)];
    switch (k) {
        case FieldKey::StepActive:          return st.active;
        case FieldKey::StepVelocity:        return st.velocity;
        case FieldKey::StepProbability:     return st.probability;
        case FieldKey::StepRatchets:        return st.ratchets;
        case FieldKey::StepMicro:           return st.microtiming;
        case FieldKey::StepGate:            return st.gate;
        case FieldKey::TrackColor:          return static_cast<int>(tr->color & 0xFFFFFF);
        case FieldKey::TrackNote:           return tr->note;
        case FieldKey::TrackChannel:        return tr->channel + 1;
        case FieldKey::TrackResolution:     return static_cast<int>(tr->resolution);
        case FieldKey::TrackLength:         return tr->length;
        case FieldKey::TrackPhase:          return tr->phase;
        case FieldKey::TrackVelocityPct:    return tr->velocityPct;
        case FieldKey::TrackProbabilityPct: return tr->probabilityPct;
        case FieldKey::TrackSwing:          return tr->swing;
        case FieldKey::TrackDurationPct:    return tr->durationPct;
        case FieldKey::TrackMute:           return tr->mute;
        case FieldKey::TrackSolo:           return tr->solo;
        default:                            return 0;
    }
}

std::string fieldText(const Model& m, const MenuTarget& t, FieldKey k)
{
    if (k == FieldKey::PatternName && t.pattern >= 0 && t.pattern < kBankSize) return m.bank[t.pattern].name;
    if (k == FieldKey::TrackName)
        if (const Track* tr = findTrack(m, t)) return tr->name;
    return {};
}

bool fieldEnabled(const Model& m, const MenuTarget&, FieldKey k)
{
    return k != FieldKey::PatternPaste || m.hasClipboard;
}

std::string fieldValueText(FieldKey k, int value)
{
    if (k == FieldKey::TrackResolution) return resolutionName(static_cast<Resolution>(std::clamp(value, 0, kResolutions - 1)));
    return std::to_string(value);
}

int snapValue(const Model& m, const MenuTarget& t, FieldKey k, int value, int dir)
{
    const Track* tr = findTrack(m, t);
    if (k != FieldKey::TrackNote || tr == nullptr) return value;
    const Pattern& p = m.bank[t.pattern];
    value = std::clamp(value, 0, 127);
    if (dir != 0) {
        const int n = freeNote(p, value, dir > 0 ? 1 : -1, tr->channel, tr->id);
        return n >= 0 ? n : tr->note;
    }
    for (int d = 0; d <= 127; ++d)
        for (int n : {value - d, value + d})
            if (n >= 0 && n <= 127 && noteOwner(p, n, tr->channel, tr->id) == nullptr) return n;
    return tr->note;
}

std::string fieldCommand(const Model& m, const MenuTarget& t, FieldKey k, int value, const std::string& text, uint32_t gesture)
{
    const FieldDef* def = findDef(t.kind, k);
    if (def == nullptr || !targetValid(m, t)) return {};
    if (def->type == FieldType::Range || def->type == FieldType::Select || def->type == FieldType::Check)
        value = std::clamp(value, def->min, def->type == FieldType::Check ? 1 : def->max);

    auto step = [&](const char* key, nlohmann::json v) { return cmd::modifyStep(t.track, t.step, {{key, v}}, gesture); };
    auto track = [&](const char* key, nlohmann::json v) { return cmd::modifyTrack(t.track, {{key, v}}, gesture); };
    switch (k) {
        case FieldKey::StepActive:          return step("active", value != 0);
        case FieldKey::StepVelocity:        return step("velocity", value);
        case FieldKey::StepProbability:     return step("probability", value);
        case FieldKey::StepRatchets:        return step("ratchets", value);
        case FieldKey::StepMicro:           return step("microtiming", value);
        case FieldKey::StepGate:            return step("gate", value);
        case FieldKey::StepClear:           return cmd::clearStep(t.track, t.step);
        case FieldKey::TrackName: {
            const std::string n = trimmed(text);
            return n.empty() || n.size() > kMaxNameBytes ? std::string() : track("name", n);
        }
        case FieldKey::TrackColor: {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "#%06x", static_cast<unsigned>(value) & 0xFFFFFFu);
            return track("color", hex);
        }
        case FieldKey::TrackNote:           return track("note", value);
        case FieldKey::TrackChannel:        return track("channel", value - 1);
        case FieldKey::TrackResolution:     return track("resolution", resolutionName(static_cast<Resolution>(value)));
        case FieldKey::TrackLength:         return track("length", value);
        case FieldKey::TrackPhase:          return track("phase", value);
        case FieldKey::TrackVelocityPct:    return track("velocityPct", value);
        case FieldKey::TrackProbabilityPct: return track("probabilityPct", value);
        case FieldKey::TrackSwing:          return track("swing", value);
        case FieldKey::TrackDurationPct:    return track("durationPct", value);
        case FieldKey::TrackMute:           return track("mute", value != 0);
        case FieldKey::TrackSolo:           return track("solo", value != 0);
        case FieldKey::PatternName: {
            const std::string n = trimmed(text);
            return n.empty() || n.size() > kMaxNameBytes ? std::string() : cmd::renamePattern(t.pattern, n);
        }
        case FieldKey::PatternPlayNow:      return cmd::selectPattern(t.pattern, true);
        case FieldKey::PatternCopy:         return cmd::patternAction("copyPattern", t.pattern);
        case FieldKey::PatternPaste:        return m.hasClipboard ? cmd::patternAction("pastePattern", t.pattern) : std::string();
        case FieldKey::PatternDuplicate:    return cmd::patternAction("duplicatePattern", t.pattern);
        case FieldKey::PatternClear:        return cmd::patternAction("clearPattern", t.pattern);
    }
    return {};
}

} // namespace puls::gui
