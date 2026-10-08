#include "reliability/reliable_channel.hpp"

#include <utility>

ReliableChannel::ReliableChannel(
    std::uint32_t maxAttempts,
    std::uint64_t sequenceReuseGuardUs)
    : maxAttempts_(maxAttempts),
      sequenceReuseGuardUs_(sequenceReuseGuardUs)
{
}

bool ReliableChannel::OnSent(
    std::uint16_t sequence,
    std::vector<std::uint8_t> rawBytes,
    std::uint64_t nowUs)
{
    if (pending_.find(sequence) != pending_.end())
    {
        return false;
    }

    if (IsReuseBlocked(sequence, nowUs))
    {
        return false;
    }

    retiredAtUs_.erase(sequence);

    PendingPacket packet{};
    packet.sequence = sequence;
    packet.rawBytes = std::move(rawBytes);
    packet.firstSentAtUs = nowUs;
    packet.lastSentAtUs = nowUs;
    packet.attempts = 1;

    pending_.emplace(sequence, std::move(packet));
    return true;
}

AckResult ReliableChannel::OnAckReceived(
    std::uint16_t sequence,
    std::uint64_t nowUs)
{
    const auto it = pending_.find(sequence);
    if (it == pending_.end())
    {
        return {};
    }

    AckResult result{};
    result.accepted = true;
    result.attempts = it->second.attempts;
    result.firstSentAtUs = it->second.firstSentAtUs;
    result.lastSentAtUs = it->second.lastSentAtUs;

    pending_.erase(it);
    Retire(sequence, nowUs);
    return result;
}

std::vector<PendingPacket> ReliableChannel::CollectForRetransmission(
    std::uint64_t nowUs,
    std::uint64_t adaptiveTimeoutUs)
{
    std::vector<std::uint16_t> toFail;
    std::vector<PendingPacket> due;
    toFail.reserve(pending_.size());
    due.reserve(pending_.size());

    for (auto& entry : pending_)
    {
        PendingPacket& packet = entry.second;

        if (nowUs < packet.lastSentAtUs)
        {
            continue;
        }

        const std::uint64_t elapsedUs = nowUs - packet.lastSentAtUs;
        if (elapsedUs < adaptiveTimeoutUs)
        {
            continue;
        }

        if (packet.attempts >= maxAttempts_)
        {
            toFail.push_back(packet.sequence);
            continue;
        }

        packet.attempts += 1;
        packet.lastSentAtUs = nowUs;
        due.push_back(packet);
    }

    for (const std::uint16_t sequence : toFail)
    {
        pending_.erase(sequence);
        failed_.push_back(sequence);
        Retire(sequence, nowUs);
    }

    return due;
}

std::size_t ReliableChannel::PendingCount() const
{
    return pending_.size();
}

std::size_t ReliableChannel::FailedCount() const
{
    return failed_.size();
}

bool ReliableChannel::IsPending(std::uint16_t sequence) const
{
    return pending_.find(sequence) != pending_.end();
}

bool ReliableChannel::IsRttSampleAmbiguous(std::uint32_t attempts)
{
    return attempts != 1;
}

void ReliableChannel::Retire(
    std::uint16_t sequence,
    std::uint64_t nowUs)
{
    retiredAtUs_[sequence] = nowUs;
}

bool ReliableChannel::IsReuseBlocked(
    std::uint16_t sequence,
    std::uint64_t nowUs) const
{
    const auto it = retiredAtUs_.find(sequence);
    if (it == retiredAtUs_.end())
    {
        return false;
    }

    if (nowUs < it->second)
    {
        return true;
    }

    return (nowUs - it->second) < sequenceReuseGuardUs_;
}
