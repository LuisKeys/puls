#pragma once
#include <atomic>

namespace puls {

// Wait-free single-producer / single-consumer triple buffer. The producer (main thread) fills writeSlot() and
// calls publish(); the consumer (audio thread) calls acquire() once per block and reads the returned slot for
// the whole block. No allocation, no locks; a slot is never written while the consumer can read it.
template <typename T>
class SnapshotExchange {
public:
    // Producer side.
    T& writeSlot() noexcept { return slots_[write_]; }
    void publish() noexcept
    {
        const int prev = middle_.exchange(write_ | kFresh, std::memory_order_acq_rel);
        write_ = prev & kMask;
    }

    // Consumer side. `fresh` tells whether a newer snapshot than the previous call was picked up.
    const T& acquire(bool& fresh) noexcept
    {
        fresh = false;
        if (middle_.load(std::memory_order_acquire) & kFresh) {
            const int prev = middle_.exchange(read_, std::memory_order_acq_rel);
            read_ = prev & kMask;
            fresh = true;
        }
        return slots_[read_];
    }

private:
    static constexpr int kMask = 3, kFresh = 4;
    T slots_[3]{};
    int write_ = 0;                    // producer-private
    int read_ = 1;                     // consumer-private
    std::atomic<int> middle_{2};       // slot index, | kFresh when it holds an unread publication
};

} // namespace puls
