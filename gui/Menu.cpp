#include "Menu.hpp"
#include "Commands.hpp"
#include "Serialization.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace puls::gui {

const uint32_t kPalette[kPaletteSize] = {
    0xFF6256, 0xFFAB50, 0xE3D65C, 0x79D86A, 0x48D9B1, 0x50BAF1, 0x9C8CF5, 0xE77FCE,   // the default track colors
    0xFF8A80, 0xFFD180, 0xC6E377, 0x3FBF7F, 0x26C6DA, 0x7986CB, 0xB39DDB, 0xA4A8B1,
};

namespace {

constexpr int kResolutions = static_cast<int>(Resolution::Count);

const FieldDef kStepFields[] = {
    {FieldKey::StepActive, FieldType::Check, "Active"},
    {FieldKey::StepVelocity, FieldType::Range, "Velocity", 1, 127},
    {FieldKey::StepProbability, FieldType::Range, "Probability %", 0, 100},
    {FieldKey::StepRatchets, FieldType::Range, "Ratchets", 1, 8},
    {FieldKey::StepMicro, FieldType::Range, "Micro-timing %", -50, 50},
    {FieldKey::StepGate, FieldType::Range, "Gate %", 1, 100},
    {FieldKey::StepClear, FieldType::Action, "Clear step"},
};

const FieldDef kTrackFields[] = {
    {FieldKey::TrackName, FieldType::Text, "Name"},
    {FieldKey::TrackColor, FieldType::Color, "Color"},
    {FieldKey::TrackNote, FieldType::Range, "MIDI note", 0, 127},
    {FieldKey::TrackChannel, FieldType::Range, "Channel", 1, 16},
    {FieldKey::TrackResolution, FieldType::Select, "Resolution", 0, kResolutions - 1},
    {FieldKey::TrackLength, FieldType::Range, "Length", 1, kMaxSteps},
    {FieldKey::TrackPhase, FieldType::Range, "Phase", 0, kMaxSteps - 1, 1},
    {FieldKey::TrackVelocityPct, FieldType::Range, "Velocity %", 0, 200, 1},
    {FieldKey::TrackProbabilityPct, FieldType::Range, "Probability %", 0, 100, 1},
    {FieldKey::TrackSwing, FieldType::Range, "Swing %", 0, 75, 1},
    {FieldKey::TrackDurationPct, FieldType::Range, "Duration %", 1, 100, 1},
    {FieldKey::TrackMute, FieldType::Check, "Mute", 0, 1, 1},
    {FieldKey::TrackSolo, FieldType::Check, "Solo", 0, 1, 1},
};

const FieldDef kPatternFields[] = {
    {FieldKey::PatternName, FieldType::Text, "Name"},
    {FieldKey::PatternPlayNow, FieldType::Action, "Play now"},
    {FieldKey::PatternCopy, FieldType::Action, "Copy"},
    {FieldKey::PatternPaste, FieldType::Action, "Paste (replaces this pattern)"},
    {FieldKey::PatternDuplicate, FieldType::Action, "Duplicate to next free slot"},
    {FieldKey::PatternClear, FieldType::Action, "Clear steps"},
};

template <size_t N> constexpr int countOf(const FieldDef (&)[N]) { return static_cast<int>(N); }

float rowHeight(FieldType t) { return t == FieldType::Color ? Menu::kColorRowH : t == FieldType::Action ? Menu::kActionRowH : Menu::kRowH; }

const FieldDef* findDef(MenuKind kind, FieldKey k)
{
    const FieldDef* defs = kind == MenuKind::Step ? kStepFields : kind == MenuKind::Track ? kTrackFields : kPatternFields;
    const int n = kind == MenuKind::Step ? countOf(kStepFields) : kind == MenuKind::Track ? countOf(kTrackFields) : countOf(kPatternFields);
    for (int i = 0; i < n; ++i)
        if (defs[i].key == k) return &defs[i];
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
        case MenuKind::Step:    fields = kStepFields;    count = countOf(kStepFields); break;
        case MenuKind::Track:   fields = kTrackFields;   count = countOf(kTrackFields); break;
        case MenuKind::Pattern: fields = kPatternFields; count = countOf(kPatternFields); break;
        case MenuKind::None:    close(); return;
    }
    // Lay out each column top to bottom (relative to the panel), then place the panel inside the window.
    int columns = 1;
    float bottom[2] = {kTitleH + 4, kTitleH + 4};
    for (int i = 0; i < count; ++i) {
        const int c = std::clamp(fields[i].column, 0, 1);
        columns = std::max(columns, c + 1);
        const float h = rowHeight(fields[i].type);
        rows_[i] = {c * kColW + kPad, bottom[c], kColW - 2 * kPad, h};
        bottom[c] += h + 2;
    }
    const float w = columns * kColW, h = std::max(bottom[0], bottom[1]) + 8;
    const float px = std::clamp(x, 4.0f, std::max(4.0f, winW - w - 4));
    const float py = std::clamp(y, 4.0f, std::max(4.0f, winH - h - 4));
    panel = {px, py, w, h};
    title = {px + kPad, py + 4, w - 2 * kPad, kTitleH - 6};
    for (int i = 0; i < count; ++i) {
        rows_[i].x += px;
        rows_[i].y += py;
    }
}

Rect Menu::control(int i) const noexcept
{
    const Rect r = rows_[i];
    const float keysW = fields[i].key == FieldKey::TrackNote ? kKeysW + 4 : 0;   // the keyboard button sits on the right
    return {r.x + kLabelW, r.y, r.w - kLabelW - keysW, r.h};
}
Rect Menu::slider(int i) const noexcept { const Rect c = control(i); return {c.x, c.y, c.w - kNumberW - 6, c.h}; }
Rect Menu::number(int i) const noexcept { const Rect c = control(i); return {c.right() - kNumberW, c.y + 2, kNumberW, c.h - 4}; }
Rect Menu::keys(int i) const noexcept
{
    if (fields[i].key != FieldKey::TrackNote) return {};
    const Rect r = rows_[i];
    return {r.right() - kKeysW, r.y + 2, kKeysW, r.h - 4};
}
Rect Menu::textBox(int i) const noexcept { const Rect c = control(i); return {c.x, c.y + 2, c.w, c.h - 4}; }
Rect Menu::prev(int i) const noexcept { const Rect c = control(i); return {c.x, c.y + 2, 22, c.h - 4}; }
Rect Menu::next(int i) const noexcept { const Rect c = control(i); return {c.x + 100, c.y + 2, 22, c.h - 4}; }
Rect Menu::check(int i) const noexcept { const Rect c = control(i); return {c.x, c.cy() - 8, 16, 16}; }
Rect Menu::swatch(int i, int c) const noexcept
{
    const Rect r = control(i);
    return {r.x + (c % 8) * kSwatchStep, r.y + 3 + (c / 8) * kSwatchStep, kSwatch, kSwatch};
}

MenuHit Menu::hitTest(float x, float y) const noexcept
{
    if (!isOpen() || !panel.contains(x, y)) return {};
    for (int i = 0; i < count; ++i) {
        if (!rows_[i].contains(x, y)) continue;
        switch (fields[i].type) {
            case FieldType::Range:
                if (keys(i).contains(x, y)) return {MenuPart::Keys, i};
                if (number(i).contains(x, y)) return {MenuPart::Number, i};
                if (slider(i).contains(x, y)) return {MenuPart::Slider, i};
                break;
            case FieldType::Text:
                if (textBox(i).contains(x, y)) return {MenuPart::Text, i};
                break;
            case FieldType::Color:
                for (int c = 0; c < kPaletteSize; ++c)
                    if (swatch(i, c).contains(x, y)) return {MenuPart::Swatch, i, c};
                break;
            case FieldType::Select:
                if (prev(i).contains(x, y)) return {MenuPart::Prev, i};
                if (next(i).contains(x, y) || control(i).contains(x, y)) return {MenuPart::Next, i};
                break;
            case FieldType::Check:  return {MenuPart::Check, i};    // the whole row toggles
            case FieldType::Action: return {MenuPart::Action, i};
        }
        return {MenuPart::Panel, i};
    }
    return {MenuPart::Panel};
}

int Menu::sliderValueAt(int i, float x) const noexcept
{
    const Rect s = slider(i);
    const FieldDef& f = fields[i];
    const float x0 = s.x + 6, x1 = s.right() - 6;
    const float u = x1 > x0 ? std::clamp((x - x0) / (x1 - x0), 0.0f, 1.0f) : 0.0f;
    return f.min + static_cast<int>(std::lround(u * (f.max - f.min)));
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
