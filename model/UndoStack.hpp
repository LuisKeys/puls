#pragma once
#include "Pattern.hpp"
#include <cstdint>
#include <deque>

namespace puls {

// Snapshot-based history of single patterns of the bank (settings are not undoable). Main thread only.
// Consecutive records that share a non-zero coalescing key (e.g. one velocity drag) keep only the first
// "before" state, so the whole gesture is a single undo step.
class UndoStack {
public:
    static constexpr size_t kLimit = 100;

    void record(int index, const Pattern& before, uint64_t key = 0)
    {
        redo_.clear();
        if (key != 0 && key == lastKey_ && !undo_.empty()) return;
        lastKey_ = key;
        undo_.push_back(Entry{index, before});
        if (undo_.size() > kLimit) undo_.pop_front();
    }

    // Swap the recorded pattern with its slot in `bank` (an array of Patterns). Returns the slot index, or -1
    // if there is nothing to do.
    int undo(Pattern* bank) { return step(undo_, redo_, bank); }
    int redo(Pattern* bank) { return step(redo_, undo_, bank); }

    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    void clear() { undo_.clear(); redo_.clear(); lastKey_ = 0; }

private:
    struct Entry { int index; Pattern pattern; };

    int step(std::deque<Entry>& from, std::deque<Entry>& to, Pattern* bank)
    {
        if (from.empty()) return -1;
        Entry e = std::move(from.back());
        from.pop_back();
        to.push_back(Entry{e.index, bank[e.index]});
        bank[e.index] = e.pattern;
        lastKey_ = 0;
        return e.index;
    }

    std::deque<Entry> undo_, redo_;
    uint64_t lastKey_ = 0;
};

} // namespace puls
