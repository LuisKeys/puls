#include "WindowIcon.hpp"

#include <cmath>
#include <vector>

#if defined(__linux__) && __has_include(<X11/Xlib.h>)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#define PULS_WINDOW_ICON 1
#endif

namespace puls {

#ifdef PULS_WINDOW_ICON
namespace {

constexpr int kSize = 128;

struct Rgba { float r, g, b, a; };

bool inEllipse(float x, float y, float cx, float cy, float rx, float ry)
{
    const float dx = (x - cx) / rx, dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
}

float distToSegment(float x, float y, float x0, float y0, float x1, float y1)
{
    const float vx = x1 - x0, vy = y1 - y0;
    const float t = std::fmax(0.0f, std::fmin(1.0f, ((x - x0) * vx + (y - y0) * vy) / (vx * vx + vy * vy)));
    return std::hypot(x - (x0 + t * vx), y - (y0 + t * vy));
}

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

std::vector<unsigned long> iconData()
{
    std::vector<unsigned long> data;
    data.reserve(2 + kSize * kSize);
    data.push_back(kSize);
    data.push_back(kSize);
    for (int py = 0; py < kSize; ++py)
        for (int px = 0; px < kSize; ++px)
        {
            Rgba sum{0, 0, 0, 0};
            for (int sy = 0; sy < 2; ++sy)
                for (int sx = 0; sx < 2; ++sx)
                {
                    const Rgba s = drumAt(px + 0.25f + 0.5f * sx, py + 0.25f + 0.5f * sy);
                    sum.r += s.r * s.a; sum.g += s.g * s.a; sum.b += s.b * s.a; sum.a += s.a;
                }
            const float a = sum.a / 4.0f;
            const auto ch = [&](float v) { return static_cast<unsigned long>(std::lround(std::fmin(255.0f, v)));};
            // _NET_WM_ICON is non-premultiplied ARGB.
            const float inv = sum.a > 0 ? 255.0f / sum.a : 0.0f;
            data.push_back((ch(a * 255.0f) << 24) | (ch(sum.r * inv) << 16) | (ch(sum.g * inv) << 8) | ch(sum.b * inv));
        }
    return data;
}

} // namespace
#endif

bool setWindowIcon(uintptr_t window)
{
#ifdef PULS_WINDOW_ICON
    if (window == 0)
        return false;
    Display* const display = XOpenDisplay(nullptr);
    if (display == nullptr)
        return false;

    const Atom icon = XInternAtom(display, "_NET_WM_ICON", False);
    const std::vector<unsigned long> data = iconData();
    int applied = 0;

    Window w = static_cast<Window>(window);
    for (;;)
    {
        Window root = 0, parent = 0, *children = nullptr;
        unsigned int count = 0;
        if (!XQueryTree(display, w, &root, &parent, &children, &count))
            break;
        if (children != nullptr)
            XFree(children);
        if (w == root)
            break;
        XChangeProperty(display, w, icon, XA_CARDINAL, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()));
        ++applied;
        if (parent == 0 || parent == root)
            break;
        w = parent;
    }
    XFlush(display);
    XCloseDisplay(display);
    return applied >= 2;
#else
    (void) window;
    return false;
#endif
}

} // namespace puls
