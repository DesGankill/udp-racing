#include "reliability/dedup_window.hpp"

DedupWindow::DedupWindow(std::uint16_t windowSize)
    : windowSize_(windowSize == 0 ? 1 : windowSize)
{
}

bool DedupWindow::Observe(std::uint16_t sequence)
{
    if (!initialized_)
    {
        initialized_ = true;
        latest_ = sequence;
        seen_.insert(sequence);
        order_.push_back(sequence);
        return false;
    }

    if (seen_.find(sequence) != seen_.end())
    {
        return true;
    }

    const auto delta = static_cast<std::int16_t>(
        static_cast<std::uint16_t>(sequence - latest_));

    if (delta > 0)
    {
        latest_ = sequence;
        seen_.insert(sequence);
        order_.push_back(sequence);
        Trim();
        return false;
    }

    if (delta == 0)
    {
        return true;
    }

    const auto age = static_cast<std::uint16_t>(latest_ - sequence);
    if (age >= windowSize_)
    {
        return true;
    }

    seen_.insert(sequence);
    order_.push_back(sequence);
    Trim();
    return false;
}

std::size_t DedupWindow::Size() const
{
    return seen_.size();
}

void DedupWindow::Trim()
{
    while (!order_.empty())
    {
        const auto age =
            static_cast<std::uint16_t>(latest_ - order_.front());

        if (age < windowSize_ && seen_.size() <= windowSize_)
        {
            break;
        }

        seen_.erase(order_.front());
        order_.pop_front();
    }
}
