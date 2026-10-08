#pragma once

#include <iostream>
#include <vector>
#include <winsock2.h>
#include <chrono>

#include "network_impairment.hpp"
#include "protocol.h"
#include "server_session.hpp"

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
    NetworkImpairment& impairment,
    ServerSession& session)
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

        if (impairment.Conditions().impairReliable &&
            impairment.DropIncoming(shoot->sequence, "SHOOT"))
        {
            return;
        }

        const ShootDecision decision = session.OnShoot(
            shoot->sequence,
            shoot->weaponId,
            shoot->requiresAck == kAckRequired);

        if (decision.duplicate)
        {
            std::cout
                << "Duplicate SHOOT ignored, sequence="
                << shoot->sequence
                << ", weaponId="
                << static_cast<int>(shoot->weaponId)
                << '\n';
        }
        else
        {
            std::cout
                << "Shoot applied: weaponId="
                << static_cast<int>(shoot->weaponId)
                << ", shotsApplied="
                << session.ShotsApplied()
                << '\n';
        }

        if (!decision.sendAck)
        {
            return;
        }

        if (impairment.Conditions().impairReliable &&
            impairment.DropOutgoing(
                decision.acknowledgedSequence,
                "ACK"))
        {
            return;
        }

        if (impairment.Conditions().impairReliable)
        {
            impairment.ApplyDelay();
        }

        const auto ack = SerializeAck(decision.acknowledgedSequence);
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

        if (impairment.DropIncoming(ping->sequence, "PING"))
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

        impairment.ApplyDelay();

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
