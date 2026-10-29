#pragma once
#include <atomic>
#include <cstddef>

namespace puls {

// Wait-free single-producer / single-consumer ring buffer with fixed capacity (N must be a power of two).
// push() fails (returns false) when full; nothing is ever allocated or locked.
template <typename T, size_t N>
class SpscQueue {
    static_assert((N & (N - 1)) == 0 && N >= 2, "N must be a power of two");
public:
    bool push(const T& v) noexcept
    {
        const size_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) == N) return false;
        items_[w & (N - 1)] = v;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) noexcept
    {
        const size_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        out = items_[r & (N - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }
private:
    T items_[N]{};
    std::atomic<size_t> write_{0}, read_{0};
};

} // namespace puls
