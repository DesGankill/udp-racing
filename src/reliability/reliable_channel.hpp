#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Serialized packet waiting for an acknowledgement.
// attempts counts transmissions, including the original send:
// 1 after OnSent, 2 after the first retransmission, and so on.
struct PendingPacket
{
    std::uint16_t sequence = 0;
    std::vector<std::uint8_t> rawBytes;
    std::uint64_t firstSentAtUs = 0;
    std::uint64_t lastSentAtUs = 0;
    std::uint32_t attempts = 1;
};

// Result of matching an ACK to a pending packet.
// Contextual bool conversion is true only when a pending entry was removed.
struct AckResult
{
    bool accepted = false;
    std::uint32_t attempts = 0;
    std::uint64_t firstSentAtUs = 0;
    std::uint64_t lastSentAtUs = 0;

    explicit operator bool() const
    {
        return accepted;
    }
};

// Tracks reliable packets. This class does not perform socket I/O.
//
// Transmission accounting:
// 1. OnSent stores the packet with attempts = 1.
// 2. The packet becomes due when now - lastSentAtUs >= RTO.
// 3. If it is due and attempts < maxAttempts, attempts increases by 1,
//    lastSentAtUs is refreshed, and one copy is returned for retransmission.
// 4. The same packet is not returned again until another full RTO elapses.
// 5. If it is due and attempts >= maxAttempts, it is removed from pending
//    and counted as finally undelivered. It is not returned.
// 6. maxAttempts is therefore the maximum number of transmissions.
//
// A repeated ACK for a sequence that is no longer pending is ignored.
// An ACK for an unknown sequence is ignored.
// A 16-bit sequence cannot be registered again until sequenceReuseGuardUs
// has elapsed after it left pending, so a delayed ACK cannot confirm a
// newer packet that reused the same number.
class ReliableChannel
{
public:
    explicit ReliableChannel(
        std::uint32_t maxAttempts = 5,
        std::uint64_t sequenceReuseGuardUs = 30'000'000);

    bool OnSent(
        std::uint16_t sequence,
        std::vector<std::uint8_t> rawBytes,
        std::uint64_t nowUs);

    AckResult OnAckReceived(
        std::uint16_t sequence,
        std::uint64_t nowUs = 0);

    // Copies due packets. Failed packets are erased before the result is
    // returned, so callers never hold pointers into pending_.
    std::vector<PendingPacket> CollectForRetransmission(
        std::uint64_t nowUs,
        std::uint64_t adaptiveTimeoutUs);

    std::size_t PendingCount() const;
    std::size_t FailedCount() const;
    bool IsPending(std::uint16_t sequence) const;

    // Karn's algorithm: an ACK for a retransmitted packet is ambiguous
    // because it is unclear which transmission it acknowledges.
    static bool IsRttSampleAmbiguous(std::uint32_t attempts);

private:
    void Retire(
        std::uint16_t sequence,
        std::uint64_t nowUs);

    bool IsReuseBlocked(
        std::uint16_t sequence,
        std::uint64_t nowUs) const;

    std::uint32_t maxAttempts_;
    std::uint64_t sequenceReuseGuardUs_;
    std::unordered_map<std::uint16_t, PendingPacket> pending_;
    std::vector<std::uint16_t> failed_;
    std::unordered_map<std::uint16_t, std::uint64_t> retiredAtUs_;
};
