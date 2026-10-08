#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../src/protocol.h"

int main()
{
    {
        const std::uint16_t sequence = 42;
        const std::uint64_t sendTime = 123456789ULL;

        const auto buffer =
            SerializePing(sequence, sendTime);

        const auto parsed =
            ParsePing(buffer.data(), buffer.size());

        assert(parsed.has_value());
        assert(parsed->sequence == sequence);
        assert(parsed->clientSendTimeUs == sendTime);
    }

    {
        PongData pong{};
        pong.sequence = 77;
        pong.clientSendTimeUs = 1000ULL;
        pong.serverReceiveTimeUs = 2000ULL;
        pong.serverSendTimeUs = 3000ULL;

        const auto buffer =
            SerializePong(pong);

        const auto parsed =
            ParsePong(buffer.data(), buffer.size());

        assert(parsed.has_value());
        assert(parsed->sequence == 77);
        assert(parsed->clientSendTimeUs == 1000ULL);
        assert(parsed->serverReceiveTimeUs == 2000ULL);
        assert(parsed->serverSendTimeUs == 3000ULL);
    }

    {
        const auto buffer =
            SerializePing(1, 123456789ULL);

        auto invalidBuffer = buffer;

        invalidBuffer[3] = 0;
        invalidBuffer[4] = 7;

        const auto parsed =
            ParsePing(
                invalidBuffer.data(),
                invalidBuffer.size());

        assert(!parsed.has_value());
    }

    {
        const auto buffer =
            SerializePing(1, 123456789ULL);

        auto invalidBuffer = buffer;

        invalidBuffer[5] = 0;
        invalidBuffer[6] = 2;

        const auto parsed =
            ParsePing(
                invalidBuffer.data(),
                invalidBuffer.size());

        assert(!parsed.has_value());
    }

    {
        const auto buffer = SerializeAck(1000);

        assert(buffer.size() == kPacketHeaderSize + 2);
        assert(buffer[0] == static_cast<std::uint8_t>(PacketType::Ack));
        assert(buffer[7] == kAckNotRequired);

        const auto parsed = ParseAck(buffer.data(), buffer.size());
        assert(parsed.has_value());
        assert(parsed->sequence == 1000);
        assert(parsed->acknowledgedSequence == 1000);
    }

    {
        const auto shoot = SerializeShoot(7, 3, kAckRequired);
        const auto parsed = ParseShoot(shoot.data(), shoot.size());
        assert(parsed.has_value());
        assert(parsed->sequence == 7);
        assert(parsed->weaponId == 3);
        assert(parsed->requiresAck == kAckRequired);
        assert(shoot.size() == kPacketHeaderSize + 1);
    }

    {
        const auto movement = SerializeMovement(
            8,
            1.5f,
            -2.0f,
            3.25f,
            kAckNotRequired);
        const auto parsed = ParseMovement(
            movement.data(),
            movement.size());
        assert(parsed.has_value());
        assert(parsed->sequence == 8);
        assert(parsed->requiresAck == kAckNotRequired);
        assert(parsed->x == 1.5f);
        assert(parsed->y == -2.0f);
        assert(parsed->z == 3.25f);
    }

    {
        const auto state = SerializeState(9, 4, 10.0f, 20.0f, 0.0f);
        const auto parsed = ParseState(state.data(), state.size());
        assert(parsed.has_value());
        assert(parsed->playerId == 4);
        assert(parsed->x == 10.0f);
        assert(parsed->y == 20.0f);
        assert(parsed->z == 0.0f);
        assert(parsed->requiresAck == kAckNotRequired);
    }

    {
        assert(!ParseAck(nullptr, 10).has_value());

        const auto ack = SerializeAck(1);
        assert(!ParseAck(ack.data(), 3).has_value());

        auto truncated = ack;
        truncated.pop_back();
        assert(!ParseAck(truncated.data(), truncated.size()).has_value());

        auto badVersion = ack;
        badVersion[5] = 0;
        badVersion[6] = 2;
        assert(!ParseAck(badVersion.data(), badVersion.size()).has_value());

        auto badType = ack;
        badType[0] = 99;
        assert(!ParseHeader(badType.data(), badType.size()).has_value() ||
               !IsKnownPacketType(99));
        assert(!ParseAck(badType.data(), badType.size()).has_value());

        auto ackRequiresAck = ack;
        ackRequiresAck[7] = kAckRequired;
        assert(!ParseAck(
                   ackRequiresAck.data(),
                   ackRequiresAck.size())
                    .has_value());

        std::vector<std::uint8_t> unknown = ack;
        unknown[0] = 50;
        const auto header = ParseHeader(unknown.data(), unknown.size());
        assert(header.has_value());
        assert(!IsKnownPacketType(header->type));
    }

    {
        const auto ping = SerializePing(1, 5);
        assert(ping.size() == kPacketHeaderSize + 8);
        assert(ping[7] == kAckNotRequired);
        const auto parsed = ParsePing(ping.data(), ping.size());
        assert(parsed.has_value());
        assert(parsed->sequence == 1);
    }

    std::cout << "Protocol tests passed\n";

    return 0;
}