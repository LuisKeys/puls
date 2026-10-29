#include "TemplateList.hpp"
#include <algorithm>

namespace puls::gui {

namespace {
constexpr float kCtlH = 22, kGap = 4, kButtonsGap = 8;

float rowHeight(const TemplateList::Row& r) { return r.entry < 0 ? TemplateList::kHeaderH : TemplateList::kRowH; }
} // namespace

void TemplateList::open(const Rect& anchor, float winW, float winH, std::vector<TemplateEntry> entries)
{
    open_ = true;
    anchor_ = anchor;
    winW_ = winW;
    winH_ = winH;
    mode_ = TemplateMode::List;
    scroll = 0;
    setEntries(std::move(entries));
}

void TemplateList::place(const Rect& anchor, float winW, float winH)
{
    anchor_ = anchor;
    winW_ = winW;
    winH_ = winH;
    layout();
}

void TemplateList::setEntries(std::vector<TemplateEntry> entries)
{
    entries_ = std::move(entries);
    rows_.clear();
    bool builtIn = false, user = false;
    for (const TemplateEntry& e : entries_) (e.user ? user : builtIn) = true;
    if (builtIn) {
        rows_.push_back({-1, "Built-in"});
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
            if (!entries_[i].user) rows_.push_back({i, nullptr});
    }
    rows_.push_back({-1, user ? "My templates" : "My templates (none saved yet)"});
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        if (entries_[i].user) rows_.push_back({i, nullptr});
    layout();
}

void TemplateList::setMode(TemplateMode m)
{
    mode_ = m;
    layout();
}

void TemplateList::layout()
{
    const float margin = EditorLayout::kPad;
    const float x = std::max(margin, std::min(anchor_.x, winW_ - margin - kW));
    const float y = anchor_.bottom() + 2;
    line1 = line2 = field = ok = cancel = list = saveRow = Rect{};
    const float inner = kW - 2 * kPad;

    if (mode_ == TemplateMode::List) {
        float content = 0;
        for (const Row& r : rows_) content += rowHeight(r);
        const float avail = winH_ - margin - y - (2 * kPad + kGap + kRowH);
        const float listH = std::max(kRowH, std::min(content, avail));
        list = {x + kPad, y + kPad, inner, listH};
        saveRow = {list.x, list.bottom() + kGap, inner, kRowH};
        panel = {x, y, kW, saveRow.bottom() + kPad - y};
        scrollBy(0);
        return;
    }
    line1 = {x + kPad, y + kPad, inner, kLineH};
    float bottom = line1.bottom();
    if (mode_ == TemplateMode::Save) {
        field = {x + kPad, bottom + kGap, inner, kCtlH};
        bottom = field.bottom();
    } else {
        line2 = {x + kPad, bottom, inner, kLineH};
        bottom = line2.bottom();
    }
    cancel = {x + kW - kPad - kButtonW, bottom + kButtonsGap, kButtonW, kCtlH};
    ok = {cancel.x - 6 - kButtonW, cancel.y, kButtonW, kCtlH};
    panel = {x, y, kW, cancel.bottom() + kPad - y};
}

float TemplateList::maxScroll() const noexcept
{
    float content = 0;
    for (const Row& r : rows_) content += rowHeight(r);
    return std::max(0.0f, content - list.h);
}

void TemplateList::scrollBy(float dy) noexcept
{
    scroll = std::clamp(scroll + dy, 0.0f, maxScroll());
}

Rect TemplateList::row(int i) const noexcept
{
    float y = list.y - scroll;
    for (int k = 0; k < i && k < static_cast<int>(rows_.size()); ++k) y += rowHeight(rows_[k]);
    return i >= 0 && i < static_cast<int>(rows_.size()) ? Rect{list.x, y, list.w, rowHeight(rows_[i])} : Rect{};
}

Rect TemplateList::deleteBox(int i) const noexcept
{
    if (i < 0 || i >= static_cast<int>(rows_.size()) || rows_[i].entry < 0 || !entries_[rows_[i].entry].user) return {};
    const Rect r = row(i);
    return {r.right() - kDeleteW, r.y + 1, kDeleteW, r.h - 2};
}

TemplateHit TemplateList::hitTest(float x, float y) const noexcept
{
    if (!open_ || !panel.contains(x, y)) return {};
    switch (mode_) {
        case TemplateMode::List:
            if (list.contains(x, y)) {
                for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                    if (!row(i).contains(x, y)) continue;
                    if (rows_[i].entry < 0) break;
                    if (deleteBox(i).contains(x, y)) return {TemplatePart::Delete, rows_[i].entry};
                    return {TemplatePart::Entry, rows_[i].entry};
                }
            }
            if (saveRow.contains(x, y)) return {TemplatePart::SaveRow};
            break;
        case TemplateMode::Save:
            if (field.contains(x, y)) return {TemplatePart::Field};
            [[fallthrough]];
        case TemplateMode::Confirm:
            if (ok.contains(x, y)) return {TemplatePart::Ok};
            if (cancel.contains(x, y)) return {TemplatePart::Cancel};
            break;
    }
    return {TemplatePart::Panel};
}

} // namespace puls::gui
