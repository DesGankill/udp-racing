#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_set>

// Remembers recently applied reliable commands.
// Sequence comparison uses 16-bit serial arithmetic, so the window stays
// correct when sequence numbers wrap past 65535.
//
// Observe returns true when the command must not be applied again:
// the sequence is already in the window, or it is older than the window
// and can no longer be distinguished safely from a duplicate.
// A sequence inside the window that has not been seen is treated as new.
//
// The window does not survive process restart and does not provide an
// exactly-once guarantee outside its capacity.
class DedupWindow
{
public:
    explicit DedupWindow(std::uint16_t windowSize = 256);

    bool Observe(std::uint16_t sequence);

    std::size_t Size() const;

private:
    void Trim();

    std::uint16_t windowSize_;
    bool initialized_ = false;
    std::uint16_t latest_ = 0;
    std::unordered_set<std::uint16_t> seen_;
    std::deque<std::uint16_t> order_;
};
