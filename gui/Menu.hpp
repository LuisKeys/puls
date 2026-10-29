#pragma once
#include "EditorLayout.hpp"
#include "Model.hpp"
#include <string>

namespace puls::gui {

enum class MenuKind : uint8_t { None, Step, Track, Pattern };
enum class FieldType : uint8_t { Range, Text, Color, Select, Check, Action };

enum class FieldKey : uint8_t {
    StepActive, StepVelocity, StepProbability, StepRatchets, StepMicro, StepGate, StepClear,
    TrackName, TrackColor, TrackNote, TrackChannel, TrackResolution, TrackLength,
    TrackPhase, TrackVelocityPct, TrackProbabilityPct, TrackSwing, TrackDurationPct, TrackMute, TrackSolo,
    PatternName, PatternPlayNow, PatternCopy, PatternPaste, PatternDuplicate, PatternClear,
};

// min/max are in shown units (the channel is 1-based on screen, 0-based in the model).
struct FieldDef {
    FieldKey    key;
    FieldType   type;
    const char* label;
    int         min = 0, max = 0;
    int         column = 0;
};

// Step and track menus belong to the pattern that was selected when they opened (`pattern`);
// a pattern menu targets bank slot `pattern`.
struct MenuTarget {
    MenuKind kind = MenuKind::None;
    uint32_t track = 0;
    int      step = 0;
    int      pattern = 0;
};

enum class MenuPart : uint8_t { None, Panel, Slider, Number, Text, Swatch, Prev, Next, Check, Action, Keys };

struct MenuHit {
    MenuPart part = MenuPart::None;
    int      field = -1;
    int      sub = -1;   // swatch index
};

constexpr int kPaletteSize = 16;
extern const uint32_t kPalette[kPaletteSize];
constexpr size_t kMaxNameBytes = 15;

// A popover of labelled controls (one or two columns), positioned inside the window. Geometry only.
class Menu {
public:
    static constexpr float kColW = 270, kRowH = 24, kColorRowH = 42, kActionRowH = 26, kTitleH = 28;
    static constexpr float kLabelW = 96, kNumberW = 44, kKeysW = 24, kPad = 10, kSwatch = 16, kSwatchStep = 19;
    static constexpr int   kMaxFields = 16;

    void open(const MenuTarget& t, float x, float y, float winW, float winH);
    void close() noexcept { target = {}; count = 0; }
    bool isOpen() const noexcept { return target.kind != MenuKind::None; }

    MenuHit hitTest(float x, float y) const noexcept;
    int     sliderValueAt(int field, float x) const noexcept;

    MenuTarget      target;
    const FieldDef* fields = nullptr;
    int             count = 0;
    Rect            panel, title;

    Rect row(int i) const noexcept { return rows_[i]; }
    Rect control(int i) const noexcept;
    Rect slider(int i) const noexcept;    // Range: the track
    Rect number(int i) const noexcept;    // Range: the value box (click to type)
    Rect keys(int i) const noexcept;      // MIDI note: opens the note keyboard; empty for other fields
    Rect textBox(int i) const noexcept;   // Text
    Rect swatch(int i, int c) const noexcept;
    Rect prev(int i) const noexcept;      // Select
    Rect next(int i) const noexcept;
    Rect check(int i) const noexcept;

private:
    Rect rows_[kMaxFields];
};

bool        targetValid(const Model& m, const MenuTarget& t);
std::string menuTitle(const Model& m, const MenuTarget& t);
int         fieldValue(const Model& m, const MenuTarget& t, FieldKey k);        // shown units; Color = 0xRRGGBB
std::string fieldText(const Model& m, const MenuTarget& t, FieldKey k);         // Text fields
bool        fieldEnabled(const Model& m, const MenuTarget& t, FieldKey k);      // Actions may be unavailable
std::string fieldValueText(FieldKey k, int value);                              // e.g. a resolution name

// The Bridge command that sets field `k` to `value` (shown units) or `text`, or performs the action. Values are
// clamped to the field's range. Empty when there is nothing valid to send (e.g. an empty name).
std::string fieldCommand(const Model& m, const MenuTarget& t, FieldKey k, int value, const std::string& text, uint32_t gesture);

} // namespace puls::gui
