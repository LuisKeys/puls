#include "WindowIcon.hpp"

#include "vs/ui/Icon.hpp"
#include "vs/ui/nvg/WindowIcon.hpp"

#include <cmath>
#include <vector>

namespace puls {

namespace {

constexpr int kSize = 128;

using vs::ui::distToSegment;
using vs::ui::inEllipse;
using vs::ui::Rgba;

// One sample of a snare drum with two crossed sticks, in a 128x128 box, topmost shape first.
Rgba drumAt(float x, float y)
{
    if (distToSegment(x, y, 26, 12, 70, 52) <= 4.5f || distToSegment(x, y, 102, 12, 58, 52) <= 4.5f)
        return {0.92f, 0.75f, 0.45f, 1.0f};                                  // sticks
    if (inEllipse(x, y, 64, 62, 38, 11))
        return {0.96f, 0.94f, 0.88f, 1.0f};                                  // head
    if (inEllipse(x, y, 64, 62, 44, 16))
        return {0.82f, 0.83f, 0.86f, 1.0f};                                  // top rim
    const bool inBody = x >= 20 && x <= 108 && y >= 62 && y <= 104;
    if (inBody || inEllipse(x, y, 64, 104, 44, 16))
    {
        const float shade = 1.0f - 0.4f * std::fabs(x - 64.0f) / 44.0f;
        if (y >= 98 || (y >= 62 && y < 68))
            return {0.78f * shade, 0.79f * shade, 0.82f * shade, 1.0f};      // metal rims
        return {0.82f * shade, 0.16f * shade, 0.16f * shade, 1.0f};          // shell
    }
    return {0, 0, 0, 0};
}

} // namespace

// The drum is Puls's own; rasterizing it to _NET_WM_ICON and setting it on the X11 windows is vs-ui.
bool setWindowIcon(uintptr_t window)
{
    static const std::vector<unsigned long> data = vs::ui::netWmIconData(kSize, drumAt);
    return vs::ui::setWindowIcon(window, data);
}

} // namespace puls
