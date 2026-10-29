#pragma once
#include "Pattern.hpp"
#include <string>
#include <vector>

namespace puls::gui {

struct GridPos {
    int track = 0;   // index into the pattern's tracks
    int step = 0;
};

struct GridRect {
    int  t0 = 0, t1 = -1, s0 = 0, s1 = -1;   // inclusive; empty when t1 < t0
    bool empty() const noexcept { return t1 < t0 || s1 < s0; }
    bool contains(int t, int s) const noexcept { return t >= t0 && t <= t1 && s >= s0 && s <= s1; }
    int  cells() const noexcept { return empty() ? 0 : (t1 - t0 + 1) * (s1 - s0 + 1); }
};

using StepBlock = std::vector<std::vector<Step>>;   // rows = tracks, columns = steps

// Keyboard/pointer step selection: a rectangle between an anchor and a cursor (track index x step).
class Selection {
public:
    bool     active() const noexcept { return active_; }
    GridPos  cursor() const noexcept { return cursor_; }
    GridRect rect() const noexcept;
    bool     multi() const noexcept { const GridRect r = rect(); return r.t1 > r.t0 || r.s1 > r.s0; }

    void clear() noexcept { active_ = false; }
    void set(GridPos p) noexcept { active_ = true; anchor_ = cursor_ = p; }
    void extendTo(GridPos p) noexcept { if (!active_) set(p); else cursor_ = p; }
    void collapse() noexcept { anchor_ = cursor_; }
    void select(GridPos anchor, GridPos cursor) noexcept { active_ = true; anchor_ = anchor; cursor_ = cursor; }
    // Arrow keys. The first press only places the cursor on (startTrack, 0); later ones move it, clamped to the
    // grid. With `extend` the anchor stays put (Shift+arrows), otherwise it follows the cursor.
    void move(int dt, int ds, bool extend, int trackCount, int startTrack) noexcept;
    // Keeps the selection inside the pattern after tracks were removed; clears it when there are none.
    void clamp(int trackCount) noexcept;

private:
    bool    active_ = false;
    GridPos anchor_, cursor_;
};

// Bridge commands acting on the selected block of pattern `p` (one atomic editSteps each, so one undo step).
// Empty when there is nothing to do.
std::string toggleCommand(const Pattern& p, const GridRect& r);   // all active -> all off, otherwise all on
std::string clearCommand(const Pattern& p, const GridRect& r);    // every property back to its default
StepBlock   copyBlock(const Pattern& p, const GridRect& r);
// Pastes `b` with its top-left cell at `at`; cells past the last track or step are dropped.
// `pasted` receives the area that was written.
std::string pasteCommand(const Pattern& p, const StepBlock& b, GridPos at, GridRect& pasted);

} // namespace puls::gui
