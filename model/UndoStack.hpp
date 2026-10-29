#pragma once
#include "Pattern.hpp"
#include <cstdint>
#include <deque>

namespace puls {

// Snapshot-based history of the Pattern (settings are not undoable). Main thread only.
// Consecutive records that share a non-zero coalescing key (e.g. one velocity drag) keep only the first
// "before" state, so the whole gesture is a single undo step.
class UndoStack {
public:
    static constexpr size_t kLimit = 100;

    void record(const Pattern& before, uint64_t key = 0)
    {
        redo_.clear();
        if (key != 0 && key == lastKey_ && !undo_.empty()) return;
        lastKey_ = key;
        undo_.push_back(before);
        if (undo_.size() > kLimit) undo_.pop_front();
    }

    // Swap `current` with the previous / next state. Returns false if there is nothing to do.
    bool undo(Pattern& current)
    {
        if (undo_.empty()) return false;
        redo_.push_back(current);
        current = undo_.back();
        undo_.pop_back();
        lastKey_ = 0;
        return true;
    }
    bool redo(Pattern& current)
    {
        if (redo_.empty()) return false;
        undo_.push_back(current);
        current = redo_.back();
        redo_.pop_back();
        lastKey_ = 0;
        return true;
    }

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void clear() { undo_.clear(); redo_.clear(); lastKey_ = 0; }

private:
    std::deque<Pattern> undo_, redo_;
    uint64_t lastKey_ = 0;
};

} // namespace puls
