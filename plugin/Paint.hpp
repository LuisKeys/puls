#pragma once
#include "NanoVG.hpp"
#include "EditorLayout.hpp"
#include "TextEdit.hpp"
#include <cmath>
#include <cstdint>

// Drawing helpers shared by the editor and its popovers. Colors are 0xRRGGBB; geometry is in logical pixels.
namespace paint {

using DGL_NAMESPACE::Color;
using DGL_NAMESPACE::NanoVG;
using puls::gui::Rect;

namespace col {
constexpr uint32_t bg = 0x08090b, panel = 0x101216, cell = 0x1a1c21, cellBeat = 0x20232a, cellOut = 0x0d0e11,
                   line = 0x252830, text = 0xa4a8b1, text2 = 0x676d76, accent = 0x50baf1, white = 0xffffff,
                   muteOn = 0xe3d65c, playing = 0x79d86a, danger = 0x8a2c2c, errBg = 0x3a1a1a, errText = 0xf2a0a0;
}

inline Color rgb(uint32_t c, float a = 1.0f)
{
    return Color(static_cast<int>((c >> 16) & 255), static_cast<int>((c >> 8) & 255), static_cast<int>(c & 255), a);
}

inline void fillRect(NanoVG& vg, const Rect& r, uint32_t c, float a = 1.0f, float radius = 0.0f)
{
    vg.beginPath();
    if (radius > 0.0f) vg.roundedRect(r.x, r.y, r.w, r.h, radius);
    else vg.rect(r.x, r.y, r.w, r.h);
    vg.fillColor(rgb(c, a));
    vg.fill();
}

inline void strokeRect(NanoVG& vg, const Rect& r, uint32_t c, float width, float radius, float a = 1.0f)
{
    vg.beginPath();
    vg.roundedRect(r.x + width * 0.5f, r.y + width * 0.5f, r.w - width, r.h - width, radius);
    vg.strokeColor(rgb(c, a));
    vg.strokeWidth(width);
    vg.stroke();
}

inline void label(NanoVG& vg, const Rect& r, const char* s, uint32_t c, int align = NanoVG::ALIGN_LEFT, float size = 12.0f, float a = 1.0f)
{
    vg.fontSize(size);
    vg.fillColor(rgb(c, a));
    vg.textAlign(align | NanoVG::ALIGN_MIDDLE);
    const float x = (align & NanoVG::ALIGN_CENTER) ? r.cx() : (align & NanoVG::ALIGN_RIGHT) ? r.right() : r.x;
    vg.text(x, r.cy(), s, nullptr);
}

inline void clippedLabel(NanoVG& vg, const Rect& r, const char* s, uint32_t c, float size = 12.0f)
{
    vg.save();
    vg.intersectScissor(r.x, r.y, r.w, r.h);
    label(vg, r, s, c, NanoVG::ALIGN_LEFT, size);
    vg.restore();
}

inline void button(NanoVG& vg, const Rect& r, const char* s, bool enabled, bool active, bool hot, float size = 12.0f, int align = NanoVG::ALIGN_CENTER)
{
    vg.save();
    if (!enabled) vg.globalAlpha(0.4f);
    fillRect(vg, r, active ? col::accent : col::cell, 1.0f, 4.0f);
    strokeRect(vg, r, active ? col::accent : (hot && enabled) ? col::text2 : col::line, 1.0f, 4.0f);
    const Rect t = align == NanoVG::ALIGN_LEFT ? Rect{r.x + 8, r.y, r.w - 16, r.h} : r;
    label(vg, t, s, active ? col::bg : col::text, align, size);
    vg.restore();
}

// A small piano (three white keys, two black ones) centered in r, for the buttons that open the note keyboard.
inline void pianoIcon(NanoVG& vg, const Rect& r, uint32_t c)
{
    const float w = 15, h = 11, x = std::floor(r.cx() - w * 0.5f) + 0.5f, y = std::floor(r.cy() - h * 0.5f) + 0.5f;
    vg.beginPath();
    vg.roundedRect(x, y, w, h, 1.5f);
    vg.moveTo(x + w / 3, y + h * 0.6f);
    vg.lineTo(x + w / 3, y + h);
    vg.moveTo(x + 2 * w / 3, y + h * 0.6f);
    vg.lineTo(x + 2 * w / 3, y + h);
    vg.strokeColor(rgb(c));
    vg.strokeWidth(1.0f);
    vg.stroke();
    fillRect(vg, {x + w / 3 - 1.5f, y, 3, h * 0.6f}, c);
    fillRect(vg, {x + 2 * w / 3 - 1.5f, y, 3, h * 0.6f}, c);
}

inline void pianoButton(NanoVG& vg, const Rect& r, bool active, bool hot)
{
    button(vg, r, "", true, active, hot);
    pianoIcon(vg, r, active ? col::bg : col::text);
}

// A floating panel with a soft shadow.
inline void panel(NanoVG& vg, const Rect& r)
{
    vg.beginPath();
    vg.rect(r.x - 10, r.y - 4, r.w + 20, r.h + 18);
    vg.fillPaint(vg.boxGradient(r.x, r.y + 4, r.w, r.h, 6, 14, rgb(0, 0.5f), rgb(0, 0.0f)));
    vg.fill();
    fillRect(vg, r, col::panel, 1.0f, 6.0f);
    strokeRect(vg, r, col::line, 1.0f, 6.0f);
}

inline void checkBox(NanoVG& vg, const Rect& r, bool on, bool hot)
{
    fillRect(vg, r, on ? col::accent : col::cell, 1.0f, 3.0f);
    strokeRect(vg, r, on ? col::accent : hot ? col::text2 : col::line, 1.0f, 3.0f);
    if (!on) return;
    vg.beginPath();
    vg.moveTo(r.x + 4, r.cy());
    vg.lineTo(r.x + r.w * 0.42f, r.bottom() - 4.5f);
    vg.lineTo(r.right() - 3.5f, r.y + 4);
    vg.strokeColor(rgb(col::bg));
    vg.strokeWidth(2.0f);
    vg.stroke();
}

// A text box showing either a value or, while it has focus, the edit with its caret (or "select all" highlight).
inline void textField(NanoVG& vg, const Rect& r, const char* value, const puls::gui::TextEdit* edit, bool hot, int align = NanoVG::ALIGN_LEFT)
{
    fillRect(vg, r, col::cell, 1.0f, 4.0f);
    strokeRect(vg, r, edit ? col::accent : hot ? col::text2 : col::line, 1.0f, 4.0f);
    const Rect in{r.x + 5, r.y, r.w - 10, r.h};
    vg.save();
    vg.intersectScissor(r.x + 2, r.y, r.w - 4, r.h);
    if (edit == nullptr) {
        label(vg, in, value, col::text, align);
    } else {
        const std::string& s = edit->text();
        vg.fontSize(12.0f);
        DGL_NAMESPACE::Rectangle<float> b;
        const float textW = s.empty() ? 0.0f : vg.textBounds(0, 0, s.c_str(), nullptr, b);
        const float x0 = align == NanoVG::ALIGN_RIGHT ? in.right() - textW : in.x;
        if (edit->allSelected() && !s.empty())
            fillRect(vg, {x0 - 1, r.cy() - 8, textW + 2, 16}, col::accent, 0.35f, 2.0f);
        label(vg, {x0, in.y, textW, in.h}, s.c_str(), col::white);
        const float cx = edit->cursor() == 0 ? 0.0f : vg.textBounds(0, 0, s.c_str(), s.c_str() + edit->cursor(), b);
        fillRect(vg, {x0 + cx, r.cy() - 7, 1, 14}, col::white);
    }
    vg.restore();
}

} // namespace paint
