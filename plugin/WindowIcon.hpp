#pragma once

#include <cstdint>

namespace puls {

// Sets a drum icon (_NET_WM_ICON) on the native window `window` and its X11 ancestors below the root, so the host's
// title bar shows it instead of the default. Returns true once it reached a window above our own (the host's);
// false means it has not yet (not X11, or not reparented) and the caller may try again later.
bool setWindowIcon(uintptr_t window);

} // namespace puls
