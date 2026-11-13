#pragma once
#include "EditorLayout.hpp"
#include "Model.hpp"
#include "vs/ui/Popover.hpp"
#include "vs/ui/Theme.hpp"
#include <string>

namespace puls::gui {

enum class MenuKind : uint8_t { None, Step, Track, Pattern };

using vs::ui::FieldDef;      // generic field: opaque `id` (a FieldKey here), type, label, range, column
using vs::ui::FieldType;
using MenuPart = vs::ui::PopoverPart;
using MenuHit = vs::ui::PopoverHit;

enum class FieldKey : uint8_t {
    StepActive, StepVelocity, StepProbability, StepRatchets, StepMicro, StepGate, StepClear,
    TrackName, TrackColor, TrackNote, TrackChannel, TrackResolution, TrackLength,
    TrackPhase, TrackVelocityPct, TrackProbabilityPct, TrackSwing, TrackDurationPct, TrackMute, TrackSolo,
    PatternName, PatternPlayNow, PatternCopy, PatternPaste, PatternDuplicate, PatternClear,
};

inline FieldKey keyOf(const FieldDef& f) noexcept { return static_cast<FieldKey>(f.id); }

// Step and track menus belong to the pattern that was selected when they opened (`pattern`);
// a pattern menu targets bank slot `pattern`.
struct MenuTarget {
    MenuKind kind = MenuKind::None;
    uint32_t track = 0;
    int      step = 0;
    int      pattern = 0;
};

constexpr int kPaletteSize = vs::ui::theme::kSwatchCount;
inline constexpr const uint32_t* kPalette = vs::ui::theme::kSwatches;
constexpr size_t kMaxNameBytes = 15;

// A popover of labelled controls (one or two columns), positioned inside the window. The layout and hit-testing are
// vs::ui::Popover; this adds which fields a step, track or pattern menu has and what they target.
class Menu : public vs::ui::Popover {
public:
    void open(const MenuTarget& t, float x, float y, float winW, float winH);
    void close() noexcept { target = {}; Popover::close(); }
    bool isOpen() const noexcept { return target.kind != MenuKind::None; }

    MenuTarget target;
};

bool        targetValid(const Model& m, const MenuTarget& t);
std::string menuTitle(const Model& m, const MenuTarget& t);
int         fieldValue(const Model& m, const MenuTarget& t, FieldKey k);        // shown units; Color = 0xRRGGBB
std::string fieldText(const Model& m, const MenuTarget& t, FieldKey k);         // Text fields
bool        fieldEnabled(const Model& m, const MenuTarget& t, FieldKey k);      // Actions may be unavailable
std::string fieldValueText(FieldKey k, int value);                              // e.g. a resolution name
// Where a slider (dir 0: the nearest) or a wheel step (dir +1 / -1: the next one that way, from `value`) may
// land. MIDI notes skip the ones other tracks use on the same channel (current value when there is none);
// other fields keep `value`.
int         snapValue(const Model& m, const MenuTarget& t, FieldKey k, int value, int dir);

// The Bridge command that sets field `k` to `value` (shown units) or `text`, or performs the action. Values are
// clamped to the field's range. Empty when there is nothing valid to send (e.g. an empty name).
std::string fieldCommand(const Model& m, const MenuTarget& t, FieldKey k, int value, const std::string& text, uint32_t gesture);

} // namespace puls::gui
