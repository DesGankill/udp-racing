#include "reliable_client.hpp"

#include <chrono>
#include <iostream>

#include "protocol.h"

namespace
{

std::uint64_t SteadyNowUs()
{
    const auto now = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch())
            .count());
}

} // namespace

ReliableClient::ReliableClient(
    SOCKET socket,
    const sockaddr_in& server,
    AdaptiveTimeout& timeout)
    : socket_(socket),
      server_(server),
      timeout_(timeout)
{
}

bool ReliableClient::SendRaw(
    const std::vector<std::uint8_t>& bytes) const
{
    const int sent = sendto(
        socket_,
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        0,
        reinterpret_cast<const sockaddr*>(&server_),
        sizeof(server_));

    if (sent == SOCKET_ERROR)
    {
        std::cerr << "sendto failed\n";
        return false;
    }

    std::cout << "Sent " << sent << " bytes\n";
    return true;
}

ShootDelivery ReliableClient::SendShoot(
    std::uint16_t sequence,
    std::uint8_t weaponId)
{
    ShootDelivery delivery{};
    delivery.sequence = sequence;

    const auto packet = SerializeShoot(
        sequence,
        weaponId,
        kAckRequired);

    const std::uint64_t sentAtUs = SteadyNowUs();
    std::cout
        << "Sending SHOOT, sequence="
        << sequence
        << ", requiresAck=1\n";

    if (!SendRaw(packet))
    {
        delivery.failed = true;
        delivery.rtoAfterMs = timeout_.RtoMs();
        return delivery;
    }

    if (!channel_.OnSent(sequence, packet, sentAtUs))
    {
        std::cerr
            << "Reliable channel rejected sequence "
            << sequence
            << '\n';
        delivery.failed = true;
        delivery.rtoAfterMs = timeout_.RtoMs();
        return delivery;
    }

    const std::size_t failedBefore = channel_.FailedCount();

    for (int spin = 0; spin < 100000; ++spin)
    {
        const std::uint64_t nowUs = SteadyNowUs();
        const auto due = channel_.CollectForRetransmission(
            nowUs,
            timeout_.RtoUs());

        for (const PendingPacket& pending : due)
        {
            std::cout
                << "Retransmit SHOOT, sequence="
                << pending.sequence
                << ", attempts="
                << pending.attempts
                << '\n';

            if (SendRaw(pending.rawBytes))
            {
                ++delivery.retransmissions;
            }
        }

        if (channel_.FailedCount() > failedBefore)
        {
            delivery.failed = true;
            delivery.attempts = channel_.MaxAttempts();
            delivery.retransmissions =
                delivery.attempts > 0
                    ? delivery.attempts - 1
                    : 0;
            delivery.rtoAfterMs = timeout_.RtoMs();

            std::cout
                << "Выстрел не подтверждён сервером, sequence="
                << sequence
                << '\n';
            return delivery;
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 15000;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);

        const int selected = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout);

        if (selected == SOCKET_ERROR)
        {
            std::cerr << "select failed\n";
            delivery.failed = true;
            delivery.rtoAfterMs = timeout_.RtoMs();
            return delivery;
        }

        if (selected == 0)
        {
            continue;
        }

        char buffer[1024];
        sockaddr_in from{};
        int fromSize = sizeof(from);

        const int received = recvfrom(
            socket_,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&from),
            &fromSize);

        if (received == SOCKET_ERROR)
        {
            std::cerr << "recvfrom failed\n";
            continue;
        }

        const auto* bytes =
            reinterpret_cast<const std::uint8_t*>(buffer);
        const auto ack = ParseAck(
            bytes,
            static_cast<std::size_t>(received));

        if (!ack)
        {
            continue;
        }

        const std::uint64_t ackAtUs = SteadyNowUs();
        const AckResult result = channel_.OnAckReceived(
            ack->acknowledgedSequence,
            ackAtUs);

        if (!result || ack->acknowledgedSequence != sequence)
        {
            continue;
        }

        const double rttMs =
            static_cast<double>(ackAtUs - result.firstSentAtUs) /
            1000.0;

        if (!ReliableChannel::IsRttSampleAmbiguous(result.attempts))
        {
            timeout_.OnSample(rttMs);
        }

        delivery.delivered = true;
        delivery.deliveredFirstTry = result.attempts == 1;
        delivery.attempts = result.attempts;
        delivery.retransmissions =
            result.attempts > 0 ? result.attempts - 1 : 0;
        delivery.timeToAckMs = rttMs;
        delivery.rtoAfterMs = timeout_.RtoMs();

        std::cout
            << "ACK received, acknowledgedSequence="
            << ack->acknowledgedSequence
            << ", attempts="
            << result.attempts
            << ", timeToAckMs="
            << rttMs
            << ", rtoMs="
            << delivery.rtoAfterMs
            << '\n';

        return delivery;
    }

    delivery.failed = true;
    delivery.rtoAfterMs = timeout_.RtoMs();
    std::cout
        << "Выстрел не подтверждён сервером, sequence="
        << sequence
        << '\n';
    return delivery;
}
