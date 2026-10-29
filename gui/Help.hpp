#pragma once
#include "EditorLayout.hpp"

namespace puls::gui {

struct HelpRow {
    const char* key;
    const char* text;
};

struct HelpSection {
    const char*    title;
    const HelpRow* rows;
    int            count;
};

extern const HelpSection kHelpSections[];
extern const int         kHelpSectionCount;

enum class HelpHit : uint8_t { Outside, Panel, Close };

// Geometry of the help overlay: a centered panel with a header (title + close button) over a scrolling body.
// Row heights depend on wrapped text, so the body's content height is measured while drawing and fed back
// through setContentHeight to clamp the scroll.
class HelpLayout {
public:
    static constexpr float kMaxW = 640, kMargin = 16, kHeaderH = 34, kKeyW = 220, kPad = 16;

    void    update(float winW, float winH) noexcept;
    HelpHit hitTest(float x, float y) const noexcept;
    void    setContentHeight(float h) noexcept { contentH_ = h; scrollBy(0); }
    void    scrollBy(float dy) noexcept;

    Rect  panel, header, close, body;
    float scroll = 0;

private:
    float contentH_ = 0;
};

} // namespace puls::gui
