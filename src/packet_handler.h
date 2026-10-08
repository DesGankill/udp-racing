#pragma once

#include <iostream>
#include <vector>
#include <winsock2.h>
#include <chrono>
#include <thread>
#include <random>

#include "protocol.h"

struct NetworkConditions
{
    int delayMs = 0;
    int jitterMs = 0;
    int lossPercent = 0;
};

inline bool ConsumeIncomingDatagram(
    const NetworkConditions& conditions,
    std::uint16_t sequence,
    const char* label)
{
    if (conditions.lossPercent <= 0)
    {
        return true;
    }

    static std::mt19937 rng(12345);
    std::uniform_int_distribution<int> lossDistribution(1, 100);

    if (lossDistribution(rng) <= conditions.lossPercent)
    {
        std::cout
            << label
            << " dropped: sequence="
            << sequence
            << '\n';
        return false;
    }

    return true;
}

inline int ResolveDelayMs(const NetworkConditions& conditions)
{
    int totalDelayMs = conditions.delayMs;

    if (conditions.jitterMs > 0)
    {
        static std::mt19937 rng(12345);
        std::uniform_int_distribution<int> jitterDistribution(
            -conditions.jitterMs,
            conditions.jitterMs);

        totalDelayMs += jitterDistribution(rng);
        if (totalDelayMs < 0)
        {
            totalDelayMs = 0;
        }
    }

    return totalDelayMs;
}

inline void ApplyPathDelay(const NetworkConditions& conditions)
{
    const int totalDelayMs = ResolveDelayMs(conditions);

    std::cout
        << "Applying network delay: "
        << totalDelayMs
        << " ms\n";

    if (totalDelayMs <= 0)
    {
        return;
    }

    const auto delayStart = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(
        std::chrono::milliseconds(totalDelayMs));
    const auto delayEnd = std::chrono::steady_clock::now();

    const auto actualDelayUs =
        std::chrono::duration_cast<std::chrono::microseconds>(
            delayEnd - delayStart)
            .count();

    std::cout
        << "Requested delay: "
        << totalDelayMs
        << " ms, actual delay: "
        << actualDelayUs / 1000.0
        << " ms\n";
}

inline bool SendDatagram(
    SOCKET serverSocket,
    const std::vector<std::uint8_t>& packet,
    const sockaddr_in& clientAddress,
    int clientAddressSize,
    const char* label)
{
    const int sentBytes = sendto(
        serverSocket,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&clientAddress),
        clientAddressSize);

    if (sentBytes == SOCKET_ERROR)
    {
        std::cerr << label << " sendto failed\n";
        return false;
    }

    std::cout
        << label
        << " sent: "
        << sentBytes
        << " bytes\n";
    return true;
}

inline void handlePacket(
    SOCKET serverSocket,
    const char* buffer,
    int receivedBytes,
    const sockaddr_in& clientAddress,
    int clientAddressSize,
    const NetworkConditions& conditions)
{
    if (buffer == nullptr || receivedBytes <= 0)
    {
        std::cerr << "Packet is too small\n";
        return;
    }

    const auto* bytes =
        reinterpret_cast<const std::uint8_t*>(buffer);
    const auto size = static_cast<std::size_t>(receivedBytes);

    const auto header = ParseHeader(bytes, size);
    if (!header)
    {
        std::cerr << "Invalid or truncated packet\n";
        return;
    }

    std::cout
        << "\nReceived "
        << receivedBytes
        << " bytes\n";

    std::cout
        << "Packet type: "
        << static_cast<int>(header->type)
        << '\n';

    std::cout
        << "Sequence: "
        << header->sequence
        << '\n';

    std::cout
        << "Payload size: "
        << header->payloadSize
        << '\n';

    std::cout
        << "Protocol version: "
        << header->version
        << '\n';

    std::cout
        << "Requires ACK: "
        << static_cast<int>(header->requiresAck)
        << '\n';

    if (!IsKnownPacketType(header->type))
    {
        std::cerr << "Unknown packet type\n";
        return;
    }

    if (header->type ==
        static_cast<std::uint8_t>(PacketType::Movement))
    {
        const auto movement = ParseMovement(bytes, size);
        if (!movement)
        {
            std::cerr << "Invalid movement packet\n";
            return;
        }

        std::cout
            << "Movement: "
            << "x=" << movement->x
            << ", y=" << movement->y
            << ", z=" << movement->z
            << '\n';

        const auto response = SerializeState(
            movement->sequence,
            1,
            movement->x,
            movement->y,
            movement->z);

        SendDatagram(
            serverSocket,
            response,
            clientAddress,
            clientAddressSize,
            "State");

        if (movement->requiresAck == kAckRequired)
        {
            const auto ack = SerializeAck(movement->sequence);
            SendDatagram(
                serverSocket,
                ack,
                clientAddress,
                clientAddressSize,
                "ACK");
        }
        return;
    }

    if (header->type ==
        static_cast<std::uint8_t>(PacketType::Shoot))
    {
        const auto shoot = ParseShoot(bytes, size);
        if (!shoot)
        {
            std::cerr << "Invalid shoot packet\n";
            return;
        }

        std::cout
            << "Shoot: "
            << "weaponId="
            << static_cast<int>(shoot->weaponId)
            << '\n';

        if (shoot->requiresAck != kAckRequired)
        {
            return;
        }

        const auto ack = SerializeAck(shoot->sequence);
        SendDatagram(
            serverSocket,
            ack,
            clientAddress,
            clientAddressSize,
            "ACK");
        return;
    }

    if (header->type ==
        static_cast<std::uint8_t>(PacketType::Ping))
    {
        const auto ping = ParsePing(bytes, size);
        if (!ping)
        {
            std::cerr << "Invalid PING packet\n";
            return;
        }

        if (!ConsumeIncomingDatagram(
                conditions,
                ping->sequence,
                "PING"))
        {
            return;
        }

        const auto receiveTime = std::chrono::steady_clock::now();
        const auto serverReceiveTimeUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                receiveTime.time_since_epoch())
                .count();

        std::cout
            << "PING received: "
            << "sequence="
            << ping->sequence
            << ", clientSendTimeUs="
            << ping->clientSendTimeUs
            << '\n';

        ApplyPathDelay(conditions);

        const auto serverSendTime = std::chrono::steady_clock::now();
        const auto serverSendTimeUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                serverSendTime.time_since_epoch())
                .count();

        PongData pong{};
        pong.sequence = ping->sequence;
        pong.clientSendTimeUs = ping->clientSendTimeUs;
        pong.serverReceiveTimeUs =
            static_cast<std::uint64_t>(serverReceiveTimeUs);
        pong.serverSendTimeUs =
            static_cast<std::uint64_t>(serverSendTimeUs);

        const auto response = SerializePong(pong);
        SendDatagram(
            serverSocket,
            response,
            clientAddress,
            clientAddressSize,
            "PONG");
        return;
    }

    if (header->type ==
        static_cast<std::uint8_t>(PacketType::Ack) ||
        header->type ==
            static_cast<std::uint8_t>(PacketType::Pong) ||
        header->type ==
            static_cast<std::uint8_t>(PacketType::State))
    {
        std::cerr
            << "Unexpected packet type for the server\n";
        return;
    }

    std::cerr << "Unknown packet type\n";
}
