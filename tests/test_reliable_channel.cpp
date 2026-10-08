#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../src/reliability/reliable_channel.hpp"
#include "../src/server_session.hpp"

namespace
{

int gFailures = 0;

void Expect(bool condition, const char* expression, int line)
{
    if (condition)
    {
        return;
    }

    std::cerr
        << "FAIL " << __FILE__ << ":" << line
        << " " << expression << '\n';
    ++gFailures;
}

#define EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)

bool ContainsSequence(
    const std::vector<PendingPacket>& packets,
    std::uint16_t sequence)
{
    for (const PendingPacket& packet : packets)
    {
        if (packet.sequence == sequence)
        {
            return true;
        }
    }

    return false;
}

const PendingPacket* FindSequence(
    const std::vector<PendingPacket>& packets,
    std::uint16_t sequence)
{
    for (const PendingPacket& packet : packets)
    {
        if (packet.sequence == sequence)
        {
            return &packet;
        }
    }

    return nullptr;
}

} // namespace

int main()
{
    {
        ReliableChannel channel;
        EXPECT(channel.OnSent(1, {0x04, 0x00, 0x01}, 0));
        EXPECT(channel.PendingCount() == 1);
        EXPECT(channel.FailedCount() == 0);
        EXPECT(channel.IsPending(1));
        EXPECT(!channel.OnSent(1, {0x01}, 10));
        EXPECT(channel.PendingCount() == 1);
    }

    {
        ReliableChannel channel(3);
        EXPECT(channel.OnSent(1, {0x04, 0x00, 0x01}, 0));

        const auto none =
            channel.CollectForRetransmission(50'000, 200'000);
        EXPECT(none.empty());
        EXPECT(channel.PendingCount() == 1);

        const auto due =
            channel.CollectForRetransmission(250'000, 200'000);
        EXPECT(due.size() == 1);
        EXPECT(due.front().attempts == 2);
        EXPECT(due.front().rawBytes.size() == 3);
        EXPECT(channel.PendingCount() == 1);

        const auto tooSoon =
            channel.CollectForRetransmission(250'000, 200'000);
        EXPECT(tooSoon.empty());

        const auto stillEarly =
            channel.CollectForRetransmission(449'999, 200'000);
        EXPECT(stillEarly.empty());
    }

    {
        ReliableChannel channel(5);
        EXPECT(channel.OnSent(4, {9}, 0));

        const auto second =
            channel.CollectForRetransmission(100'000, 100'000);
        EXPECT(second.size() == 1);
        EXPECT(second.front().attempts == 2);

        const auto third =
            channel.CollectForRetransmission(200'000, 100'000);
        EXPECT(third.size() == 1);
        EXPECT(third.front().attempts == 3);

        const auto fourth =
            channel.CollectForRetransmission(300'000, 100'000);
        EXPECT(fourth.size() == 1);
        EXPECT(fourth.front().attempts == 4);

        const auto fifth =
            channel.CollectForRetransmission(400'000, 100'000);
        EXPECT(fifth.size() == 1);
        EXPECT(fifth.front().attempts == 5);
        EXPECT(channel.PendingCount() == 1);
        EXPECT(channel.FailedCount() == 0);
    }

    {
        ReliableChannel channel(2);
        EXPECT(channel.OnSent(7, {}, 0));

        const auto retransmit =
            channel.CollectForRetransmission(300'000, 200'000);
        EXPECT(retransmit.size() == 1);
        EXPECT(retransmit.front().attempts == 2);

        const auto failed =
            channel.CollectForRetransmission(600'000, 200'000);
        EXPECT(failed.empty());
        EXPECT(channel.FailedCount() == 1);
        EXPECT(channel.PendingCount() == 0);
        EXPECT(!channel.IsPending(7));
    }

    {
        ReliableChannel channel;
        EXPECT(channel.OnSent(3, {1, 2}, 1'000));
        EXPECT(channel.PendingCount() == 1);

        const AckResult ack = channel.OnAckReceived(3, 1'500);
        EXPECT(ack.accepted);
        EXPECT(ack.attempts == 1);
        EXPECT(ack.firstSentAtUs == 1'000);
        EXPECT(!ReliableChannel::IsRttSampleAmbiguous(ack.attempts));
        EXPECT(channel.PendingCount() == 0);
        EXPECT(channel.FailedCount() == 0);

        const AckResult duplicate = channel.OnAckReceived(3, 1'600);
        EXPECT(!duplicate.accepted);
        EXPECT(channel.PendingCount() == 0);
        EXPECT(channel.FailedCount() == 0);
    }

    {
        ReliableChannel channel;
        EXPECT(channel.OnSent(1, {1}, 0));
        const AckResult unknown = channel.OnAckReceived(99, 10);
        EXPECT(!unknown.accepted);
        EXPECT(channel.PendingCount() == 1);
        EXPECT(channel.IsPending(1));
    }

    {
        ReliableChannel channel;
        EXPECT(channel.OnSent(10, {1}, 0));
        EXPECT(channel.OnSent(11, {2}, 5));
        EXPECT(channel.OnSent(12, {3}, 8));
        EXPECT(channel.PendingCount() == 3);

        EXPECT(channel.OnAckReceived(11, 20));
        EXPECT(channel.PendingCount() == 2);
        EXPECT(!channel.IsPending(11));
        EXPECT(channel.IsPending(10));
        EXPECT(channel.IsPending(12));

        EXPECT(channel.OnAckReceived(10, 30));
        EXPECT(channel.OnAckReceived(12, 40));
        EXPECT(channel.PendingCount() == 0);
    }

    {
        ReliableChannel channel(4);
        EXPECT(channel.OnSent(1, {1}, 0));
        EXPECT(channel.OnSent(2, {2}, 0));
        EXPECT(channel.OnSent(3, {3}, 1'000'000));

        const auto due =
            channel.CollectForRetransmission(200'000, 100'000);
        EXPECT(due.size() == 2);
        EXPECT(ContainsSequence(due, 1));
        EXPECT(ContainsSequence(due, 2));
        EXPECT(!ContainsSequence(due, 3));
        EXPECT(FindSequence(due, 1)->attempts == 2);
        EXPECT(FindSequence(due, 2)->attempts == 2);
        EXPECT(channel.PendingCount() == 3);
    }

    {
        ReliableChannel channel(2, 1'000);
        EXPECT(channel.OnSent(1, {1, 2, 3}, 0));
        const auto first =
            channel.CollectForRetransmission(300'000, 200'000);
        EXPECT(first.size() == 1);
        EXPECT(first.front().attempts == 2);

        EXPECT(channel.OnSent(2, {4, 5}, 300'000));
        const auto second =
            channel.CollectForRetransmission(600'000, 200'000);
        EXPECT(second.size() == 1);
        EXPECT(second.front().sequence == 2);
        EXPECT(second.front().attempts == 2);
        EXPECT(second.front().rawBytes.size() == 2);
        EXPECT(channel.FailedCount() == 1);
        EXPECT(channel.PendingCount() == 1);
        EXPECT(channel.IsPending(2));
    }

    {
        ReliableChannel channel(5, 1'000);
        EXPECT(channel.OnSent(65535, {1}, 0));
        EXPECT(channel.OnSent(0, {2}, 0));
        EXPECT(channel.PendingCount() == 2);

        EXPECT(channel.OnAckReceived(65535, 50));
        EXPECT(!channel.IsPending(65535));
        EXPECT(channel.IsPending(0));
        EXPECT(channel.PendingCount() == 1);

        const AckResult stale = channel.OnAckReceived(65535, 60);
        EXPECT(!stale.accepted);
        EXPECT(channel.IsPending(0));

        EXPECT(!channel.OnSent(65535, {3}, 100));
        EXPECT(channel.OnSent(65535, {3}, 1'050));
        EXPECT(channel.IsPending(65535));
        EXPECT(channel.IsPending(0));

        EXPECT(channel.OnAckReceived(0, 1'100));
        EXPECT(channel.IsPending(65535));
        EXPECT(!channel.IsPending(0));
    }

    {
        ReliableChannel channel(5, 5'000);
        EXPECT(channel.OnSent(5, {7}, 0));
        const auto retransmit =
            channel.CollectForRetransmission(1'000, 1'000);
        EXPECT(retransmit.size() == 1);
        EXPECT(retransmit.front().attempts == 2);

        const AckResult ack = channel.OnAckReceived(5, 1'200);
        EXPECT(ack.accepted);
        EXPECT(ReliableChannel::IsRttSampleAmbiguous(ack.attempts));
        EXPECT(!channel.OnAckReceived(5, 1'300));
    }

    {
        ServerSession session(8);
        ReliableChannel channel(5, 1'000);
        EXPECT(channel.OnSent(4, {1, 2, 3}, 0));

        const auto due =
            channel.CollectForRetransmission(200'000, 100'000);
        EXPECT(due.size() == 1);

        const ShootDecision first = session.OnShoot(4, 2, true);
        EXPECT(first.effectApplied);
        EXPECT(!first.duplicate);
        EXPECT(first.sendAck);
        EXPECT(first.acknowledgedSequence == 4);
        EXPECT(session.ShotsApplied() == 1);

        const auto again =
            channel.CollectForRetransmission(400'000, 100'000);
        EXPECT(again.size() == 1);
        EXPECT(again.front().sequence == 4);

        const ShootDecision duplicate = session.OnShoot(4, 2, true);
        EXPECT(duplicate.duplicate);
        EXPECT(!duplicate.effectApplied);
        EXPECT(duplicate.sendAck);
        EXPECT(session.ShotsApplied() == 1);

        const AckResult ack = channel.OnAckReceived(4, 450'000);
        EXPECT(ack.accepted);
        EXPECT(ack.attempts == 3);
        EXPECT(channel.PendingCount() == 0);
        EXPECT(ReliableChannel::IsRttSampleAmbiguous(ack.attempts));
    }

    {
        ServerSession session;
        ReliableChannel channel(2, 1'000);
        EXPECT(channel.OnSent(9, {1}, 0));
        EXPECT(channel.CollectForRetransmission(200'000, 100'000).size() == 1);
        EXPECT(channel.CollectForRetransmission(400'000, 100'000).empty());
        EXPECT(channel.FailedCount() == 1);
        EXPECT(channel.PendingCount() == 0);
        EXPECT(session.ShotsApplied() == 0);
        EXPECT(!channel.OnAckReceived(9, 500'000));
    }

    {
        ServerSession session(4);
        const ShootDecision first = session.OnShoot(1, 1, true);
        EXPECT(first.effectApplied);
        EXPECT(session.ShotsApplied() == 1);

        const ShootDecision duplicate = session.OnShoot(1, 1, true);
        EXPECT(duplicate.duplicate);
        EXPECT(!duplicate.effectApplied);
        EXPECT(duplicate.sendAck);
        EXPECT(session.ShotsApplied() == 1);

        EXPECT(session.OnShoot(65535, 1, true).effectApplied);
        EXPECT(session.OnShoot(0, 1, true).effectApplied);
        EXPECT(session.OnShoot(65535, 1, true).duplicate);
        EXPECT(session.ShotsApplied() == 3);

        EXPECT(session.OnShoot(2, 1, false).effectApplied);
        EXPECT(!session.OnShoot(2, 1, false).sendAck);
    }

    if (gFailures != 0)
    {
        std::cerr << gFailures << " reliable-channel checks failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Reliable channel tests passed\n";
    return EXIT_SUCCESS;
}
