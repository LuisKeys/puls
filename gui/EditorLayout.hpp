#pragma once
#include <cstdint>

namespace puls::gui {

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool  contains(float px, float py) const noexcept { return w > 0 && px >= x && px < x + w && py >= y && py < y + h; }
    float right() const noexcept { return x + w; }
    float bottom() const noexcept { return y + h; }
    float cx() const noexcept { return x + w * 0.5f; }
    float cy() const noexcept { return y + h * 0.5f; }
};

enum class Item : uint8_t {
    None,
    Undo, Redo, Enable, Audition, Keys, Resolution, Swing, RemoveTrack, AddTrack, Settings, Help,
    ConfirmYes, ConfirmNo,
    Slot,              // index = bank slot
    Kit,               // the template dropdown
    Label, Mute, Solo, Length, // index = track index
    Cell,              // index = track index, step
    HScrollbar,
    ResolutionOption,  // index = Resolution
    AnchorOption,      // index = 0 restart, 1 host
    SwitchOption,      // index = 0 bar, 1 beat, 2 now
    Popup,             // inside an open popup, but not on an option
};

struct Hit {
    Item item = Item::None;
    int  index = -1;
    int  step = -1;
};

enum class Popup : uint8_t { None, Resolution, Settings, Templates };   // Templates: see TemplateList

// Geometry of the editor in logical pixels; the UI multiplies by the host scale factor when drawing.
// No DPF/DGL here, so layout and hit-testing are unit-tested headless.
class EditorLayout {
public:
    static constexpr float kToolbarH = 38, kBankH = 32, kLabelW = 200, kRowH = 30, kMinCellW = 22;
    static constexpr float kScrollbarH = 10, kPad = 10, kResOptionH = 22;
    static constexpr float kMinWidth = 800, kMinHeight = 240;
    static constexpr int   kVisibleSteps = 16;   // steps that fit the default width before scrolling
    static constexpr int   kResolutionCount = 8;

    // Recomputes everything; keeps the scroll offsets (clamped to the new content).
    void update(float width, float height, int trackCount, int steps, bool confirming);
    void scrollBy(float dx, float dy) noexcept;
    void ensureVisible(int t, int s) noexcept;   // scrolls the least needed to show the cell
    Hit  hitTest(float x, float y, Popup popup) const noexcept;

    float width = 0, height = 0;
    int   trackCount = 0, steps = 64;
    bool  confirming = false;
    float cellW = kMinCellW;
    float scrollX = 0, scrollY = 0;

    // Toolbar.
    Rect toolbar, undo, redo, enable, audition, patternName, trackName, keys, resLabel, resolution, swingLabel, swing, swingValue,
         trackCountText, removeTrack, addTrack, settings, help;
    // Inline confirmation for track removal, floating over the top-left of the grid.
    Rect confirmBar, confirmText, confirmYes, confirmNo;
    // Pattern bank and transport.
    Rect bank, bankName, kit, transport;
    Rect slot(int i) const noexcept;
    // Grid: rows scroll vertically, cells scroll both ways, labels stay on the left.
    Rect grid, cells, hscroll;
    Rect row(int t) const noexcept;
    Rect label(int t) const noexcept;
    Rect swatch(int t) const noexcept;
    Rect name(int t) const noexcept;
    Rect mute(int t) const noexcept;
    Rect solo(int t) const noexcept;
    Rect length(int t) const noexcept;
    Rect cell(int t, int s) const noexcept;
    Rect hthumb() const noexcept;
    float maxScrollX() const noexcept;
    float maxScrollY() const noexcept;
    float scrollPerThumbPx() const noexcept;   // horizontal scroll per pixel of thumb drag
    // Popups.
    Rect resolutionPopup() const noexcept;
    Rect resolutionOption(int i) const noexcept;
    Rect settingsPanel() const noexcept;
    Rect anchorOption(int i) const noexcept;
    Rect switchOption(int i) const noexcept;
    float settingsTitleY(int group) const noexcept;   // baseline area of the group titles
};

} // namespace puls::gui
