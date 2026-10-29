#include "EditorLayout.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace puls::gui {

namespace {
constexpr float kCtlH = 22;           // toolbar control height
constexpr float kSettingsW = 260, kSettingsTitleH = 20, kSettingsOptionH = 22, kSettingsGap = 8;
constexpr float kCellsPad = 8;        // trailing space after the last step / row
} // namespace

void EditorLayout::update(float w, float h, int tracks, int stepCount, bool confirm)
{
    width = w;
    height = h;
    trackCount = std::max(0, tracks);
    steps = std::max(1, stepCount);
    confirming = confirm;

    // Toolbar, left to right; transport lives in the bank bar so the toolbar fits the minimum width.
    toolbar = {0, 0, w, kToolbarH};
    const float cy = (kToolbarH - kCtlH) * 0.5f;
    float x = kPad;
    auto take = [&](float cw, float gap) { const Rect r{x, cy, cw, kCtlH}; x += cw + gap; return r; };
    undo = take(28, 4);
    redo = take(28, 12);
    enable = take(40, 4);
    audition = take(28, 8);
    patternName = take(68, 12);
    trackName = take(58, 4);
    keys = take(28, 6);
    resLabel = take(26, 0);
    resolution = take(64, 12);
    swingLabel = take(40, 0);
    swing = take(90, 4);
    swingValue = take(24, 12);
    trackCountText = take(64, 0);
    removeTrack = take(24, 4);
    addTrack = take(24, 12);
    settings = take(28, 4);
    help = take(28, 0);

    bank = {0, kToolbarH, w, kBankH};
    bankName = {slot(15).right() + 12, bank.y, 64, kBankH};
    transport = {w - kPad - 130, bank.y, 130, kBankH};
    kit = {bankName.right() + 8, bank.y + 5, std::max(60.0f, transport.x - 8 - bankName.right() - 8), kCtlH};

    // Grid. The horizontal scrollbar only takes space when the steps do not fit.
    const float top = kToolbarH + kBankH;
    cellW = std::max(kMinCellW, std::floor((w - kLabelW - kCellsPad) / kVisibleSteps));
    const float contentW = steps * cellW + kCellsPad;
    const bool needH = contentW > w - kLabelW;
    grid = {0, top, w, std::max(0.0f, h - top - (needH ? kScrollbarH : 0))};
    cells = {kLabelW, grid.y, std::max(0.0f, w - kLabelW), grid.h};
    hscroll = needH ? Rect{kLabelW, grid.bottom(), cells.w, kScrollbarH} : Rect{};

    confirmBar = confirm ? Rect{kPad, grid.y + 6, 330, 30} : Rect{};
    confirmText = {confirmBar.x + 10, confirmBar.y, 170, confirmBar.h};
    confirmYes = {confirmText.right() + 4, confirmBar.y + 4, 70, kCtlH};
    confirmNo = {confirmYes.right() + 6, confirmBar.y + 4, 66, kCtlH};

    scrollBy(0, 0);
}

void EditorLayout::scrollBy(float dx, float dy) noexcept
{
    scrollX = std::clamp(scrollX + dx, 0.0f, maxScrollX());
    scrollY = std::clamp(scrollY + dy, 0.0f, maxScrollY());
}

void EditorLayout::ensureVisible(int t, int s) noexcept
{
    const Rect c = cell(t, s);
    float dx = 0, dy = 0;
    if (c.x < cells.x) dx = c.x - cells.x;
    else if (c.right() > cells.right()) dx = c.right() - cells.right();
    if (c.y < grid.y) dy = c.y - grid.y;
    else if (c.y + kRowH > grid.bottom()) dy = c.y + kRowH - grid.bottom();
    scrollBy(dx, dy);
}

float EditorLayout::maxScrollX() const noexcept { return std::max(0.0f, steps * cellW + kCellsPad - cells.w); }
float EditorLayout::maxScrollY() const noexcept { return std::max(0.0f, trackCount * kRowH + kCellsPad - grid.h); }

Rect EditorLayout::slot(int i) const noexcept { return {kPad + i * 30.0f, kToolbarH + 5, 26, kCtlH}; }

Rect EditorLayout::row(int t) const noexcept { return {0, grid.y + t * kRowH - scrollY, width, kRowH - 1}; }
Rect EditorLayout::label(int t) const noexcept { const Rect r = row(t); return {0, r.y, kLabelW, r.h}; }
Rect EditorLayout::swatch(int t) const noexcept { const Rect r = row(t); return {8, r.cy() - 4, 8, 8}; }
Rect EditorLayout::name(int t) const noexcept { const Rect r = row(t); return {22, r.y, mute(t).x - 26, r.h}; }
Rect EditorLayout::length(int t) const noexcept { const Rect r = row(t); return {kLabelW - 6 - 40, r.cy() - 9, 40, 18}; }
Rect EditorLayout::solo(int t) const noexcept { const Rect l = length(t); return {l.x - 6 - 20, l.y, 20, l.h}; }
Rect EditorLayout::mute(int t) const noexcept { const Rect s = solo(t); return {s.x - 4 - 20, s.y, 20, s.h}; }
Rect EditorLayout::cell(int t, int s) const noexcept { const Rect r = row(t); return {kLabelW + s * cellW - scrollX, r.y, cellW, r.h}; }

Rect EditorLayout::hthumb() const noexcept
{
    if (hscroll.w <= 0) return {};
    const float contentW = steps * cellW + kCellsPad;
    const float tw = std::max(30.0f, hscroll.w * cells.w / contentW);
    const float m = maxScrollX();
    return {hscroll.x + (m > 0 ? (hscroll.w - tw) * scrollX / m : 0), hscroll.y + 2, tw, hscroll.h - 4};
}

float EditorLayout::scrollPerThumbPx() const noexcept
{
    const float free = hscroll.w - hthumb().w;
    return free > 0 ? maxScrollX() / free : 0;
}

Rect EditorLayout::resolutionPopup() const noexcept
{
    return {resolution.x, resolution.bottom() + 2, resolution.w + 10, kResolutionCount * kResOptionH + 8};
}
Rect EditorLayout::resolutionOption(int i) const noexcept
{
    const Rect p = resolutionPopup();
    return {p.x + 4, p.y + 4 + i * kResOptionH, p.w - 8, kResOptionH};
}

Rect EditorLayout::settingsPanel() const noexcept
{
    const float h = kSettingsGap + 2 * kSettingsTitleH + 5 * kSettingsOptionH + 2 * kSettingsGap;
    return {std::max(kPad, settings.right() - kSettingsW), kToolbarH + 2, kSettingsW, h};
}
float EditorLayout::settingsTitleY(int group) const noexcept
{
    const Rect p = settingsPanel();
    return group == 0 ? p.y + kSettingsGap : anchorOption(1).bottom() + kSettingsGap;
}
Rect EditorLayout::anchorOption(int i) const noexcept
{
    const Rect p = settingsPanel();
    return {p.x + 6, p.y + kSettingsGap + kSettingsTitleH + i * kSettingsOptionH, p.w - 12, kSettingsOptionH};
}
Rect EditorLayout::switchOption(int i) const noexcept
{
    const Rect p = settingsPanel();
    return {p.x + 6, settingsTitleY(1) + kSettingsTitleH + i * kSettingsOptionH, p.w - 12, kSettingsOptionH};
}

Hit EditorLayout::hitTest(float x, float y, Popup popup) const noexcept
{
    // An open popup owns the pointer: anything outside it is "None", which closes it.
    if (popup == Popup::Templates) return {};   // TemplateList hit-tests its own panel
    if (popup == Popup::Resolution) {
        for (int i = 0; i < kResolutionCount; ++i)
            if (resolutionOption(i).contains(x, y)) return {Item::ResolutionOption, i};
        return resolutionPopup().contains(x, y) ? Hit{Item::Popup} : Hit{};
    }
    if (popup == Popup::Settings) {
        for (int i = 0; i < 2; ++i)
            if (anchorOption(i).contains(x, y)) return {Item::AnchorOption, i};
        for (int i = 0; i < 3; ++i)
            if (switchOption(i).contains(x, y)) return {Item::SwitchOption, i};
        return settingsPanel().contains(x, y) ? Hit{Item::Popup} : Hit{};
    }

    if (confirming) {
        if (confirmYes.contains(x, y)) return {Item::ConfirmYes};
        if (confirmNo.contains(x, y)) return {Item::ConfirmNo};
        if (confirmBar.contains(x, y)) return {};
    }

    if (y < kToolbarH) {
        const std::pair<const Rect*, Item> items[] = {
            {&undo, Item::Undo}, {&redo, Item::Redo}, {&enable, Item::Enable}, {&audition, Item::Audition}, {&keys, Item::Keys}, {&resolution, Item::Resolution},
            {&swing, Item::Swing}, {&removeTrack, Item::RemoveTrack}, {&addTrack, Item::AddTrack},
            {&settings, Item::Settings}, {&help, Item::Help},
        };
        for (const auto& [r, item] : items)
            if (r->contains(x, y)) return {item};
        return {};
    }

    if (bank.contains(x, y)) {
        for (int i = 0; i < 16; ++i)
            if (slot(i).contains(x, y)) return {Item::Slot, i};
        return kit.contains(x, y) ? Hit{Item::Kit} : Hit{};
    }

    if (hscroll.contains(x, y)) return {Item::HScrollbar};
    if (!grid.contains(x, y)) return {};

    const int t = static_cast<int>(std::floor((y - grid.y + scrollY) / kRowH));
    if (t < 0 || t >= trackCount) return {};
    if (y >= row(t).bottom()) return {};   // the 1 px gap between rows
    if (x < kLabelW) {
        if (mute(t).contains(x, y)) return {Item::Mute, t};
        if (solo(t).contains(x, y)) return {Item::Solo, t};
        if (length(t).contains(x, y)) return {Item::Length, t};
        return {Item::Label, t};
    }
    const int s = static_cast<int>(std::floor((x - kLabelW + scrollX) / cellW));
    if (s < 0 || s >= steps) return {};
    return {Item::Cell, t, s};
}

} // namespace puls::gui
