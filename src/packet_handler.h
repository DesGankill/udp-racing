#pragma once

#include <iostream>
#include <cstring>
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

inline void handlePacket(
    SOCKET serverSocket,
    const char* buffer,
    int receivedBytes,
    const sockaddr_in& clientAddress,
    int clientAddressSize,
    const NetworkConditions& conditions)
{
    if (receivedBytes < static_cast<int>(sizeof(PacketHeader)))
    {
        std::cerr << "Packet is too small\n";
        return;
    }

    PacketHeader header{};

    std::memcpy(
        &header,
        buffer,
        sizeof(PacketHeader));

    const uint16_t sequence =
        ntohs(header.sequence);

    const uint16_t payloadSize =
        ntohs(header.payloadSize);

    const uint16_t version =
        ntohs(header.version);

    std::cout
        << "\nReceived "
        << receivedBytes
        << " bytes\n";

    std::cout
        << "Packet type: "
        << static_cast<int>(header.type)
        << '\n';

    std::cout
        << "Sequence: "
        << sequence
        << '\n';

    std::cout
        << "Payload size: "
        << payloadSize
        << '\n';

    std::cout
        << "Protocol version: "
        << version
        << '\n';

    if (version != kProtocolVersion)
    {
        std::cerr
            << "Unsupported protocol version\n";
        return;
    }

    if (header.type ==
        static_cast<uint8_t>(PacketType::Movement))
    {
        if (payloadSize != sizeof(MovementPayload))
        {
            std::cerr
                << "Invalid movement payload size\n";
            return;
        }

        if (receivedBytes <
            static_cast<int>(
                sizeof(PacketHeader) +
                sizeof(MovementPayload)))
        {
            std::cerr
                << "Movement packet is incomplete\n";
            return;
        }

        MovementPayload movement{};

        uint32_t xNetwork{};
        uint32_t yNetwork{};
        uint32_t zNetwork{};

        std::memcpy(
            &xNetwork,
            buffer + sizeof(PacketHeader),
            sizeof(uint32_t));

        std::memcpy(
            &yNetwork,
            buffer + sizeof(PacketHeader) +
                sizeof(uint32_t),
            sizeof(uint32_t));

        std::memcpy(
            &zNetwork,
            buffer + sizeof(PacketHeader) +
                sizeof(uint32_t) * 2,
            sizeof(uint32_t));

        movement.x = floatFromNetwork(xNetwork);
        movement.y = floatFromNetwork(yNetwork);
        movement.z = floatFromNetwork(zNetwork);

        std::cout
            << "Movement: "
            << "x=" << movement.x
            << ", y=" << movement.y
            << ", z=" << movement.z
            << '\n';

        PacketHeader responseHeader{};

        responseHeader.type =
            static_cast<uint8_t>(PacketType::State);

        responseHeader.sequence =
            htons(sequence);

        responseHeader.payloadSize =
            htons(sizeof(StatePayload));

        responseHeader.version =
            htons(kProtocolVersion);

        StatePayload state{};

        state.playerId = htons(1);

        xNetwork = floatToNetwork(movement.x);
        yNetwork = floatToNetwork(movement.y);
        zNetwork = floatToNetwork(movement.z);

        char responseBuffer[
            sizeof(PacketHeader) +
            sizeof(StatePayload)];

        std::memcpy(
            responseBuffer,
            &responseHeader,
            sizeof(PacketHeader));

        std::memcpy(
            responseBuffer +
                sizeof(PacketHeader),
            &state.playerId,
            sizeof(uint16_t));

        std::memcpy(
            responseBuffer +
                sizeof(PacketHeader) +
                sizeof(uint16_t),
            &xNetwork,
            sizeof(uint32_t));

        std::memcpy(
            responseBuffer +
                sizeof(PacketHeader) +
                sizeof(uint16_t) +
                sizeof(uint32_t),
            &yNetwork,
            sizeof(uint32_t));

        std::memcpy(
            responseBuffer +
                sizeof(PacketHeader) +
                sizeof(uint16_t) +
                sizeof(uint32_t) * 2,
            &zNetwork,
            sizeof(uint32_t));

        const int sentBytes = sendto(
            serverSocket,
            responseBuffer,
            sizeof(responseBuffer),
            0,
            reinterpret_cast<const sockaddr*>(
                &clientAddress),
            clientAddressSize);

        if (sentBytes == SOCKET_ERROR)
        {
            std::cerr << "sendto failed\n";
        }
        else
        {
            std::cout
                << "State sent: "
                << sentBytes
                << " bytes\n";
        }
    }
    else if (header.type ==
             static_cast<uint8_t>(PacketType::Shoot))
    {
        if (payloadSize != sizeof(ShootPayload))
        {
            std::cerr
                << "Invalid shoot payload size\n";
            return;
        }

        if (receivedBytes <
            static_cast<int>(
                sizeof(PacketHeader) +
                sizeof(ShootPayload)))
        {
            std::cerr
                << "Shoot packet is incomplete\n";
            return;
        }

        ShootPayload shoot{};

        std::memcpy(
            &shoot,
            buffer + sizeof(PacketHeader),
            sizeof(ShootPayload));

        std::cout
            << "Shoot: "
            << "weaponId="
            << static_cast<int>(shoot.weaponId)
            << '\n';

        PacketHeader responseHeader{};

        responseHeader.type =
            static_cast<uint8_t>(PacketType::Ack);

        responseHeader.sequence =
            htons(sequence);

        responseHeader.payloadSize =
            htons(0);

        responseHeader.version =
            htons(kProtocolVersion);

        const int sentBytes = sendto(
            serverSocket,
            reinterpret_cast<const char*>(
                &responseHeader),
            sizeof(responseHeader),
            0,
            reinterpret_cast<const sockaddr*>(
                &clientAddress),
            clientAddressSize);

        if (sentBytes == SOCKET_ERROR)
        {
            std::cerr << "sendto failed\n";
        }
        else
        {
            std::cout
                << "ACK sent: "
                << sentBytes
                << " bytes\n";
        }
    }
    else if (header.type ==
             static_cast<uint8_t>(PacketType::Ping))
    {
        const auto ping = ParsePing(
            reinterpret_cast<const uint8_t*>(buffer),
            static_cast<std::size_t>(receivedBytes));

        if (!ping)
        {
            std::cerr
                << "Invalid PING packet\n";
            return;
        }

        if (conditions.lossPercent > 0)
        {
            static std::mt19937 rng(12345);

            std::uniform_int_distribution<int>
                lossDistribution(1, 100);

            if (lossDistribution(rng) <=
                conditions.lossPercent)
            {
                std::cout
                    << "PING dropped: sequence="
                    << ping->sequence
                    << '\n';

                return;
            }
        }

        const auto receiveTime =
            std::chrono::steady_clock::now();

        const auto serverReceiveTimeUs =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                receiveTime.time_since_epoch())
                .count();

        std::cout
            << "PING received: "
            << "sequence="
            << ping->sequence
            << ", clientSendTimeUs="
            << ping->clientSendTimeUs
            << '\n';

        PongData pong{};

        pong.sequence =
            ping->sequence;

        pong.clientSendTimeUs =
            ping->clientSendTimeUs;

        pong.serverReceiveTimeUs =
            static_cast<std::uint64_t>(
                serverReceiveTimeUs);

        int totalDelayMs =
            conditions.delayMs;

        if (conditions.jitterMs > 0)
        {
            static std::mt19937 rng(12345);

            std::uniform_int_distribution<int>
                jitterDistribution(
                    -conditions.jitterMs,
                    conditions.jitterMs);

            totalDelayMs +=
                jitterDistribution(rng);

            if (totalDelayMs < 0)
            {
                totalDelayMs = 0;
            }
        }

        std::cout
            << "Applying network delay: "
            << totalDelayMs
            << " ms\n";

        if (totalDelayMs > 0)
        {
            const auto delayStart =
                std::chrono::steady_clock::now();

            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    totalDelayMs));

            const auto delayEnd =
                std::chrono::steady_clock::now();

            const auto actualDelayUs =
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    delayEnd - delayStart)
                    .count();

            std::cout
                << "Requested delay: "
                << totalDelayMs
                << " ms, actual delay: "
                << actualDelayUs / 1000.0
                << " ms\n";
        }

        // Время фактической отправки PONG.
        // Оно должно фиксироваться после искусственной задержки.
        const auto serverSendTime =
            std::chrono::steady_clock::now();

        const auto serverSendTimeUs =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                serverSendTime.time_since_epoch())
                .count();

        pong.serverSendTimeUs =
            static_cast<std::uint64_t>(
                serverSendTimeUs);

        const auto response =
            SerializePong(pong);

        const int sentBytes = sendto(
            serverSocket,
            reinterpret_cast<const char*>(
                response.data()),
            static_cast<int>(
                response.size()),
            0,
            reinterpret_cast<const sockaddr*>(
                &clientAddress),
            clientAddressSize);

        if (sentBytes == SOCKET_ERROR)
        {
            std::cerr
                << "sendto PONG failed\n";
        }
        else
        {
            std::cout
                << "PONG sent: "
                << sentBytes
                << " bytes\n";
        }
    }
    else
    {
        std::cerr
            << "Unknown packet type\n";
    }
}