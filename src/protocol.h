#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <winsock2.h>

constexpr std::uint16_t kProtocolVersion = 1;

enum class PacketType : std::uint8_t
{
    Movement = 1,
    Shoot = 2,
    State = 3,
    Ack = 4,
    Ping = 5,
    Pong = 6
};

#pragma pack(push, 1)

struct PacketHeader
{
    std::uint8_t type;
    std::uint16_t sequence;
    std::uint16_t payloadSize;
    std::uint16_t version;
};

struct MovementPayload
{
    float x;
    float y;
    float z;
};

struct ShootPayload
{
    std::uint8_t weaponId;
};

struct StatePayload
{
    std::uint16_t playerId;
    float x;
    float y;
    float z;
};

#pragma pack(pop)

inline std::uint32_t floatToNetwork(float value)
{
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(float));
    return htonl(bits);
}

inline float floatFromNetwork(std::uint32_t value)
{
    value = ntohl(value);

    float result{};
    std::memcpy(&result, &value, sizeof(float));

    return result;
}

inline void WriteU16(
    std::vector<std::uint8_t>& out,
    std::uint16_t value)
{
    out.push_back(
        static_cast<std::uint8_t>(value >> 8));

    out.push_back(
        static_cast<std::uint8_t>(value));
}

inline void WriteU64(
    std::vector<std::uint8_t>& out,
    std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(
            static_cast<std::uint8_t>(
                value >> shift));
    }
}

inline std::optional<std::uint16_t> ReadU16(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    if (end - current < 2)
    {
        return std::nullopt;
    }

    std::uint16_t value =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(current[0]) << 8) |
            static_cast<std::uint16_t>(current[1]));

    current += 2;

    return value;
}

inline std::optional<std::uint64_t> ReadU64(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    if (end - current < 8)
    {
        return std::nullopt;
    }

    std::uint64_t value = 0;

    for (int i = 0; i < 8; ++i)
    {
        value =
            (value << 8) |
            static_cast<std::uint64_t>(*current);

        ++current;
    }

    return value;
}

inline std::vector<std::uint8_t> SerializePing(
    std::uint16_t sequence,
    std::uint64_t clientSendTimeUs)
{
    std::vector<std::uint8_t> packet;

    packet.reserve(15);

    packet.push_back(
        static_cast<std::uint8_t>(
            PacketType::Ping));

    WriteU16(packet, sequence);
    WriteU16(packet, 8);
    WriteU16(packet, kProtocolVersion);
    WriteU64(packet, clientSendTimeUs);

    return packet;
}
struct PingData
{
    std::uint16_t sequence;
    std::uint64_t clientSendTimeUs;
};

struct PongData
{
    std::uint16_t sequence;
    std::uint64_t clientSendTimeUs;
    std::uint64_t serverReceiveTimeUs;
    std::uint64_t serverSendTimeUs;
};

inline std::optional<PingData> ParsePing(
    const std::uint8_t* data,
    std::size_t size)
{
    if (data == nullptr || size < 7)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = data;
    const std::uint8_t* end = data + size;

    const auto packetType = *current++;

    if (packetType !=
        static_cast<std::uint8_t>(PacketType::Ping))
    {
        return std::nullopt;
    }

    auto sequence = ReadU16(current, end);
    auto payloadSize = ReadU16(current, end);
    auto version = ReadU16(current, end);

    if (!sequence ||
        !payloadSize ||
        !version)
    {
        return std::nullopt;
    }

    if (*version != kProtocolVersion)
    {
        return std::nullopt;
    }

    if (*payloadSize != 8)
    {
        return std::nullopt;
    }

    if (size != 7 + static_cast<std::size_t>(*payloadSize))
    {
        return std::nullopt;
    }

    auto clientSendTimeUs =
        ReadU64(current, end);

    if (!clientSendTimeUs ||
        current != end)
    {
        return std::nullopt;
    }

    return PingData{
        *sequence,
        *clientSendTimeUs
    };
}

inline std::vector<std::uint8_t> SerializePong(
    const PongData& pong)
{
    std::vector<std::uint8_t> packet;

    packet.reserve(31);

    packet.push_back(
        static_cast<std::uint8_t>(
            PacketType::Pong));

    WriteU16(packet, pong.sequence);
    WriteU16(packet, 24);
    WriteU16(packet, kProtocolVersion);

    WriteU64(packet, pong.clientSendTimeUs);
    WriteU64(packet, pong.serverReceiveTimeUs);
    WriteU64(packet, pong.serverSendTimeUs);

    return packet;
}

inline std::optional<PongData> ParsePong(
    const std::uint8_t* data,
    std::size_t size)
{
    if (data == nullptr || size < 7)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = data;
    const std::uint8_t* end = data + size;

    const auto packetType = *current++;

    if (packetType !=
        static_cast<std::uint8_t>(PacketType::Pong))
    {
        return std::nullopt;
    }

    auto sequence = ReadU16(current, end);
    auto payloadSize = ReadU16(current, end);
    auto version = ReadU16(current, end);

    if (!sequence ||
        !payloadSize ||
        !version)
    {
        return std::nullopt;
    }

    if (*version != kProtocolVersion)
    {
        return std::nullopt;
    }

    if (*payloadSize != 24)
    {
        return std::nullopt;
    }

    if (size != 7 + static_cast<std::size_t>(*payloadSize))
    {
        return std::nullopt;
    }

    auto clientSendTimeUs =
        ReadU64(current, end);

    auto serverReceiveTimeUs =
        ReadU64(current, end);

    auto serverSendTimeUs =
        ReadU64(current, end);

    if (!clientSendTimeUs ||
        !serverReceiveTimeUs ||
        !serverSendTimeUs ||
        current != end)
    {
        return std::nullopt;
    }

    return PongData{
        *sequence,
        *clientSendTimeUs,
        *serverReceiveTimeUs,
        *serverSendTimeUs
    };
}