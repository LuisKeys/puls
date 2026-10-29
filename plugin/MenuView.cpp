#include "MenuView.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace puls::gui;
using DGL_NAMESPACE::NanoVG;

bool applyEditKey(TextEdit& edit, uint key)
{
    switch (key) {
        case DGL_NAMESPACE::kKeyBackspace: edit.backspace(); return true;
        case DGL_NAMESPACE::kKeyDelete:    edit.del(); return true;
        case DGL_NAMESPACE::kKeyLeft:      edit.left(); return true;
        case DGL_NAMESPACE::kKeyRight:     edit.right(); return true;
        case DGL_NAMESPACE::kKeyHome:      edit.home(); return true;
        case DGL_NAMESPACE::kKeyEnd:       edit.end(); return true;
        default:                           return false;
    }
}

void MenuView::open(const MenuTarget& t, float x, float y, float winW, float winH)
{
    menu_.open(t, x, y, winW, winH);
    editField_ = dragField_ = wheelField_ = -1;
    dragPending_ = false;
    hover_ = {};
}

void MenuView::close(const puls::Model& m)
{
    if (!isOpen()) return;
    if (editField_ >= 0) commitEdit(m);
    flushDrag(m);
    dragField_ = -1;
    menu_.close();
    hover_ = {};
}

void MenuView::sync(const puls::Model& m)
{
    if (isOpen() && !targetValid(m, menu_.target)) {
        editField_ = dragField_ = -1;
        dragPending_ = false;
        menu_.close();
        hover_ = {};
    }
}

void MenuView::idle(const puls::Model& m) { flushDrag(m); }

void MenuView::flushDrag(const puls::Model& m)
{
    if (!dragPending_ || dragField_ < 0) return;
    dragPending_ = false;
    sendField(m, dragField_, dragValue_, {}, dragGesture_);
}

void MenuView::sendField(const puls::Model& m, int field, int value, const std::string& text, uint32_t gesture)
{
    const std::string c = fieldCommand(m, menu_.target, menu_.fields[field].key, value, text, gesture);
    if (!c.empty()) send_(c);
}

void MenuView::beginEdit(const puls::Model& m, int field)
{
    if (editField_ == field) return;
    const FieldDef& f = menu_.fields[field];
    if (f.type == FieldType::Text)
        text_.begin(fieldText(m, menu_.target, f.key), kMaxNameBytes, false);
    else
        text_.begin(std::to_string(fieldValue(m, menu_.target, f.key)), 4, true);
    editField_ = field;
}

void MenuView::commitEdit(const puls::Model& m)
{
    const int i = editField_;
    editField_ = -1;
    if (i < 0 || !targetValid(m, menu_.target)) return;
    const FieldDef& f = menu_.fields[i];
    if (f.type == FieldType::Text) {
        if (text_.text() != fieldText(m, menu_.target, f.key))
            sendField(m, i, 0, text_.text(), ++gestureSeq_);
        return;
    }
    const std::string& s = text_.text();
    if (s.empty() || s == "-") return;   // nothing typed: keep the value
    const int v = std::clamp(static_cast<int>(std::strtol(s.c_str(), nullptr, 10)), f.min, f.max);
    if (v != fieldValue(m, menu_.target, f.key))
        sendField(m, i, v, {}, ++gestureSeq_);
}

bool MenuView::mouseDown(float x, float y, const puls::Model& m)
{
    const MenuHit h = menu_.hitTest(x, y);
    if (h.part == MenuPart::None) {
        close(m);
        return false;
    }
    if (editField_ >= 0 && !(h.field == editField_ && (h.part == MenuPart::Text || h.part == MenuPart::Number)))
        commitEdit(m);
    if (h.field < 0) return true;
    if (h.part != MenuPart::Slider && h.part != MenuPart::Number) wheelField_ = -1;

    const FieldDef& f = menu_.fields[h.field];
    const int v = fieldValue(m, menu_.target, f.key);
    switch (h.part) {
        case MenuPart::Slider:
            dragField_ = h.field;
            dragGesture_ = ++gestureSeq_;
            dragValue_ = snapValue(m, menu_.target, f.key, menu_.sliderValueAt(h.field, x), 0);
            dragPending_ = dragValue_ != v;
            break;
        case MenuPart::Number:
        case MenuPart::Text:
            beginEdit(m, h.field);
            break;
        case MenuPart::Swatch:
            sendField(m, h.field, static_cast<int>(kPalette[h.sub]), {}, ++gestureSeq_);
            break;
        case MenuPart::Prev:
        case MenuPart::Next: {
            const int nv = std::clamp(v + (h.part == MenuPart::Next ? 1 : -1), f.min, f.max);
            if (nv != v) sendField(m, h.field, nv, {}, ++gestureSeq_);
            break;
        }
        case MenuPart::Check:
            sendField(m, h.field, v ? 0 : 1, {}, ++gestureSeq_);
            break;
        case MenuPart::Action:
            if (fieldEnabled(m, menu_.target, f.key)) {
                sendField(m, h.field, 0, {}, 0);
                close(m);
            }
            break;
        case MenuPart::Keys: {
            const uint32_t track = menu_.target.track;
            close(m);
            openKeys_(track);
            break;
        }
        default:
            break;
    }
    return true;
}

void MenuView::mouseUp(const puls::Model& m)
{
    flushDrag(m);
    dragField_ = -1;
}

void MenuView::motion(float x, float, const puls::Model& m)
{
    if (dragField_ < 0) return;
    const int v = snapValue(m, menu_.target, menu_.fields[dragField_].key, menu_.sliderValueAt(dragField_, x), 0);
    if (v != dragValue_) {
        dragValue_ = v;
        dragPending_ = true;
    }
}

bool MenuView::hover(float x, float y)
{
    const MenuHit h = menu_.hitTest(x, y);
    if (h.part == hover_.part && h.field == hover_.field && h.sub == hover_.sub) return false;
    hover_ = h;
    return true;
}

bool MenuView::scroll(float x, float y, float dy, const puls::Model& m)
{
    const MenuHit h = menu_.hitTest(x, y);
    if (h.part == MenuPart::None) return false;
    if (h.field < 0 || dy == 0.0f) return true;
    const FieldDef& f = menu_.fields[h.field];
    const bool range = f.type == FieldType::Range && (h.part == MenuPart::Slider || h.part == MenuPart::Number);
    if (!range && f.type != FieldType::Select) return true;
    const int v = fieldValue(m, menu_.target, f.key);
    const int d = dy > 0 ? 1 : -1;
    const int nv = std::clamp(snapValue(m, menu_.target, f.key, std::clamp(v + d, f.min, f.max), d), f.min, f.max);
    if (nv == v) return true;
    if (wheelField_ != h.field) {   // consecutive wheel steps on one control are one undo step
        wheelField_ = h.field;
        wheelGesture_ = ++gestureSeq_;
    }
    sendField(m, h.field, nv, {}, wheelGesture_);
    return true;
}

bool MenuView::key(uint key, const puls::Model& m)
{
    if (!isOpen()) return false;
    if (editField_ >= 0) {
        if (key == DGL_NAMESPACE::kKeyEnter || key == DGL_NAMESPACE::kKeyPadEnter)
            commitEdit(m);
        else if (key == DGL_NAMESPACE::kKeyEscape)
            editField_ = -1;
        else
            applyEditKey(text_, key);
        return true;   // every key belongs to the field while it has focus
    }
    if (key == DGL_NAMESPACE::kKeyEscape) {
        close(m);
        return true;
    }
    return false;
}

bool MenuView::character(const char* utf8)
{
    if (editField_ < 0) return false;
    text_.insert(utf8);
    return true;
}

// ---- drawing -----------------------------------------------------------------------------------------------------

void MenuView::draw(NanoVG& vg, const puls::Model& m)
{
    if (!isOpen()) return;
    using namespace paint;
    const MenuTarget& t = menu_.target;
    panel(vg, menu_.panel);
    clippedLabel(vg, menu_.title, menuTitle(m, t).c_str(), col::white);

    for (int i = 0; i < menu_.count; ++i) {
        const FieldDef& f = menu_.fields[i];
        const Rect row = menu_.row(i);
        if (f.type != FieldType::Action)
            clippedLabel(vg, {row.x, row.y, Menu::kLabelW - 6, f.type == FieldType::Color ? Menu::kRowH : row.h}, f.label, col::text2);
        const int v = (dragField_ == i) ? dragValue_ : fieldValue(m, t, f.key);

        switch (f.type) {
            case FieldType::Range: {
                const Rect s = menu_.slider(i);
                const float x0 = s.x + 6, x1 = s.right() - 6;
                const auto at = [&](int val) { return x0 + (x1 - x0) * (val - f.min) / static_cast<float>(f.max - f.min); };
                const float zero = at(std::clamp(0, f.min, f.max)), pos = at(v);
                fillRect(vg, {x0, s.cy() - 2, x1 - x0, 4}, col::cell, 1.0f, 2.0f);
                fillRect(vg, {std::min(zero, pos), s.cy() - 2, std::fabs(pos - zero), 4}, col::accent, 1.0f, 2.0f);
                vg.beginPath();
                vg.circle(pos, s.cy(), 5.5f);
                vg.fillColor(rgb(hot(MenuPart::Slider, i) || dragField_ == i ? col::white : col::text));
                vg.fill();
                const std::string shown = std::to_string(v);
                textField(vg, menu_.number(i), shown.c_str(), editField_ == i ? &text_ : nullptr, hot(MenuPart::Number, i), NanoVG::ALIGN_RIGHT);
                if (menu_.keys(i).w > 0)
                    pianoButton(vg, menu_.keys(i), false, hot(MenuPart::Keys, i));
                break;
            }
            case FieldType::Text: {
                const std::string s = fieldText(m, t, f.key);
                textField(vg, menu_.textBox(i), s.c_str(), editField_ == i ? &text_ : nullptr, hot(MenuPart::Text, i));
                break;
            }
            case FieldType::Color:
                for (int c = 0; c < kPaletteSize; ++c) {
                    const Rect sw = menu_.swatch(i, c);
                    fillRect(vg, sw, kPalette[c], 1.0f, 3.0f);
                    if (kPalette[c] == static_cast<uint32_t>(v))
                        strokeRect(vg, {sw.x - 2, sw.y - 2, sw.w + 4, sw.h + 4}, col::white, 1.5f, 4.0f);
                    else if (hot(MenuPart::Swatch, i, c))
                        strokeRect(vg, sw, col::white, 1.0f, 3.0f, 0.6f);
                }
                break;
            case FieldType::Select: {
                const Rect p = menu_.prev(i), n = menu_.next(i);
                button(vg, p, "\xE2\x80\xB9", v > f.min, false, hot(MenuPart::Prev, i), 14.0f);   // ‹
                button(vg, n, "\xE2\x80\xBA", v < f.max, false, hot(MenuPart::Next, i), 14.0f);   // ›
                label(vg, {p.right(), p.y, n.x - p.right(), p.h}, fieldValueText(f.key, v).c_str(), col::text, NanoVG::ALIGN_CENTER);
                break;
            }
            case FieldType::Check:
                checkBox(vg, menu_.check(i), v != 0, hot(MenuPart::Check, i));
                break;
            case FieldType::Action:
                button(vg, {row.x, row.y + 2, row.w, row.h - 4}, f.label, fieldEnabled(m, t, f.key), false,
                       hot(MenuPart::Action, i), 12.0f, NanoVG::ALIGN_LEFT);
                break;
        }
    }
}
