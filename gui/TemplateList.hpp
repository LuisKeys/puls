#pragma once
#include "EditorLayout.hpp"
#include <string>
#include <vector>

namespace puls::gui {

// The template dropdown under the bank bar's kit button: built-in and user templates by name (scrolling when they
// do not fit), a "save" row at the bottom, and two inline modes that replace the list: typing a name to save the
// pattern's tracks as a template, and confirming an action that would lose something. Geometry only.
struct TemplateEntry {
    std::string id, name;
    bool        user = false;
};

enum class TemplateMode : uint8_t { List, Save, Confirm };
enum class TemplatePart : uint8_t { None, Panel, Entry, Delete, SaveRow, Field, Ok, Cancel };

struct TemplateHit {
    TemplatePart part = TemplatePart::None;
    int          index = -1;   // entry index for Entry / Delete
};

class TemplateList {
public:
    static constexpr float kW = 290, kRowH = 22, kHeaderH = 20, kPad = 6, kDeleteW = 20, kButtonW = 74, kLineH = 18;

    // Opens in List mode under `anchor`, kept inside a winW x winH window.
    void open(const Rect& anchor, float winW, float winH, std::vector<TemplateEntry> entries);
    void place(const Rect& anchor, float winW, float winH);   // after a resize; keeps mode and scroll
    void close() noexcept { open_ = false; }
    bool isOpen() const noexcept { return open_; }
    // New entries (after a save or delete) keep the mode; the scroll is clamped.
    void setEntries(std::vector<TemplateEntry> entries);
    void setMode(TemplateMode m);

    TemplateHit hitTest(float x, float y) const noexcept;
    void        scrollBy(float dy) noexcept;

    TemplateMode mode() const noexcept { return mode_; }
    const std::vector<TemplateEntry>& entries() const noexcept { return entries_; }

    // List mode: rows are section headers ("Built-in", "My templates") and entries.
    struct Row { int entry = -1; const char* header = nullptr; };   // entry -1 = header
    const std::vector<Row>& rows() const noexcept { return rows_; }
    Rect  row(int i) const noexcept;         // scrolled; may lie outside `list`
    Rect  deleteBox(int i) const noexcept;   // × on a user entry's row, empty otherwise
    float maxScroll() const noexcept;

    Rect panel, list, saveRow;
    // Save and Confirm modes: one or two lines of text, the name field (Save), and the two buttons.
    Rect line1, line2, field, ok, cancel;
    float scroll = 0;

private:
    void layout();

    bool open_ = false;
    TemplateMode mode_ = TemplateMode::List;
    Rect anchor_;
    float winW_ = 0, winH_ = 0;
    std::vector<TemplateEntry> entries_;
    std::vector<Row> rows_;
};

} // namespace puls::gui
