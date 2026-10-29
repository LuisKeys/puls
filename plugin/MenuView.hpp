#pragma once
#include "Menu.hpp"
#include "Paint.hpp"
#include "TextEdit.hpp"
#include <functional>
#include <string>

// Applies an editing key (Backspace, Delete, arrows, Home, End) to a text field. False for any other key.
bool applyEditKey(puls::gui::TextEdit& edit, uint key);

// The right-click popovers (step, track, pattern) and their controls. Values are read from the Model on every
// draw; edits go out as Bridge commands through `send`. Slider drags are coalesced (flushed by idle() and on
// release) and share one gesture id, so a drag is one undo step. Gesture ids come from the editor's counter so
// they never collide with the grid's. The keyboard button next to the MIDI note closes the menu and calls `openKeys`.
class MenuView {
public:
    using Send = std::function<void(const std::string&)>;
    using OpenKeys = std::function<void(uint32_t track)>;

    MenuView(Send send, OpenKeys openKeys, uint32_t& gestureSeq)
        : send_(std::move(send)), openKeys_(std::move(openKeys)), gestureSeq_(gestureSeq) {}

    bool isOpen() const noexcept { return menu_.isOpen(); }
    bool dragging() const noexcept { return dragField_ >= 0; }
    bool editing() const noexcept { return editField_ >= 0; }
    bool contains(float x, float y) const noexcept { return isOpen() && menu_.panel.contains(x, y); }
    puls::gui::MenuPart hoveredPart() const noexcept { return hover_.part; }

    void open(const puls::gui::MenuTarget& t, float x, float y, float winW, float winH);
    void close(const puls::Model& m);                 // commits a text edit in progress
    void sync(const puls::Model& m);                  // closes the menu if its target is gone (no commit)
    void idle(const puls::Model& m);                  // flushes a coalesced slider value
    void draw(DGL_NAMESPACE::NanoVG& vg, const puls::Model& m);

    // False when the press was outside the panel: the menu closes and the caller may handle the click.
    bool mouseDown(float x, float y, const puls::Model& m);
    void mouseUp(const puls::Model& m);
    void motion(float x, float y, const puls::Model& m);
    bool hover(float x, float y);                     // true when what is hovered changed
    bool scroll(float x, float y, float dy, const puls::Model& m);
    bool key(uint key, const puls::Model& m);         // only while open; true when consumed
    bool character(const char* utf8);

private:
    void flushDrag(const puls::Model& m);
    void beginEdit(const puls::Model& m, int field);
    void commitEdit(const puls::Model& m);
    void sendField(const puls::Model& m, int field, int value, const std::string& text, uint32_t gesture);
    bool hot(puls::gui::MenuPart part, int field, int sub = -1) const noexcept
    {
        return hover_.part == part && hover_.field == field && (sub < 0 || hover_.sub == sub);
    }

    puls::gui::Menu     menu_;
    puls::gui::TextEdit text_;
    Send                send_;
    OpenKeys            openKeys_;
    uint32_t&           gestureSeq_;
    int                 editField_ = -1;
    int                 dragField_ = -1, dragValue_ = 0;
    bool                dragPending_ = false;
    uint32_t            dragGesture_ = 0;
    int                 wheelField_ = -1;
    uint32_t            wheelGesture_ = 0;
    puls::gui::MenuHit  hover_;
};
