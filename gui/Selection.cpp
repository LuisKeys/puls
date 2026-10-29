#include "Selection.hpp"
#include "Commands.hpp"
#include <algorithm>

namespace puls::gui {

using json = nlohmann::json;

GridRect Selection::rect() const noexcept
{
    if (!active_) return {};
    return {std::min(anchor_.track, cursor_.track), std::max(anchor_.track, cursor_.track),
            std::min(anchor_.step, cursor_.step), std::max(anchor_.step, cursor_.step)};
}

void Selection::move(int dt, int ds, bool extend, int trackCount, int startTrack) noexcept
{
    if (trackCount <= 0) { clear(); return; }
    if (!active_) {
        set({std::clamp(startTrack, 0, trackCount - 1), 0});
        return;
    }
    cursor_ = {std::clamp(cursor_.track + dt, 0, trackCount - 1), std::clamp(cursor_.step + ds, 0, kMaxSteps - 1)};
    if (!extend) anchor_ = cursor_;
}

void Selection::clamp(int trackCount) noexcept
{
    if (!active_) return;
    if (trackCount <= 0) { clear(); return; }
    anchor_.track = std::min(anchor_.track, trackCount - 1);
    cursor_.track = std::min(cursor_.track, trackCount - 1);
}

namespace {

// The selection clipped to the pattern's tracks.
GridRect clipped(const Pattern& p, GridRect r)
{
    r.t0 = std::max(r.t0, 0);
    r.t1 = std::min(r.t1, static_cast<int>(p.trackCount) - 1);
    r.s0 = std::max(r.s0, 0);
    r.s1 = std::min(r.s1, kMaxSteps - 1);
    return r;
}

std::string edit(const Pattern& p, const GridRect& r, const json& props)
{
    json cells = json::array();
    for (int t = r.t0; t <= r.t1; ++t)
        for (int s = r.s0; s <= r.s1; ++s)
            cells.push_back({{"track", p.tracks[t].id}, {"step", s}, {"props", props}});
    return cells.empty() ? std::string() : cmd::editSteps(cells);
}

} // namespace

std::string toggleCommand(const Pattern& p, const GridRect& sel)
{
    const GridRect r = clipped(p, sel);
    if (r.empty()) return {};
    bool allOn = true;
    for (int t = r.t0; t <= r.t1 && allOn; ++t)
        for (int s = r.s0; s <= r.s1 && allOn; ++s)
            allOn = p.tracks[t].steps[s].active;
    return edit(p, r, {{"active", !allOn}});
}

std::string clearCommand(const Pattern& p, const GridRect& sel)
{
    const GridRect r = clipped(p, sel);
    return r.empty() ? std::string() : edit(p, r, cmd::stepProps(Step{}));
}

StepBlock copyBlock(const Pattern& p, const GridRect& sel)
{
    const GridRect r = clipped(p, sel);
    StepBlock b;
    if (r.empty()) return b;
    for (int t = r.t0; t <= r.t1; ++t)
        b.emplace_back(p.tracks[t].steps + r.s0, p.tracks[t].steps + r.s1 + 1);
    return b;
}

std::string pasteCommand(const Pattern& p, const StepBlock& b, GridPos at, GridRect& pasted)
{
    pasted = {};
    json cells = json::array();
    int lastT = -1, lastS = -1;
    for (size_t dt = 0; dt < b.size(); ++dt) {
        const int t = at.track + static_cast<int>(dt);
        if (t < 0 || t >= p.trackCount) break;
        for (size_t ds = 0; ds < b[dt].size(); ++ds) {
            const int s = at.step + static_cast<int>(ds);
            if (s < 0 || s >= kMaxSteps) break;
            cells.push_back({{"track", p.tracks[t].id}, {"step", s}, {"props", cmd::stepProps(b[dt][ds])}});
            lastT = std::max(lastT, t);
            lastS = std::max(lastS, s);
        }
    }
    if (cells.empty()) return {};
    pasted = {at.track, lastT, at.step, lastS};
    return cmd::editSteps(cells);
}

} // namespace puls::gui
