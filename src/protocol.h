#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include <winsock2.h>

constexpr std::uint16_t kProtocolVersion = 1;
constexpr std::size_t kPacketHeaderSize = 8;
constexpr std::uint8_t kAckNotRequired = 0;
constexpr std::uint8_t kAckRequired = 1;

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
    std::uint8_t requiresAck;
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

struct AckPayload
{
    std::uint16_t acknowledgedSequence;
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

inline void WriteU8(
    std::vector<std::uint8_t>& out,
    std::uint8_t value)
{
    out.push_back(value);
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

inline void WriteU32(
    std::vector<std::uint8_t>& out,
    std::uint32_t value)
{
    out.push_back(
        static_cast<std::uint8_t>(value >> 24));
    out.push_back(
        static_cast<std::uint8_t>(value >> 16));
    out.push_back(
        static_cast<std::uint8_t>(value >> 8));
    out.push_back(
        static_cast<std::uint8_t>(value));
}

inline void WriteF32(
    std::vector<std::uint8_t>& out,
    float value)
{
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32(out, bits);
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

inline std::optional<std::uint8_t> ReadU8(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    if (current == nullptr || end == nullptr || current >= end)
    {
        return std::nullopt;
    }

    const std::uint8_t value = *current;
    ++current;
    return value;
}

inline std::optional<std::uint16_t> ReadU16(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    if (current == nullptr ||
        end == nullptr ||
        end - current < 2)
    {
        return std::nullopt;
    }

    const std::uint16_t value =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(current[0]) << 8) |
            static_cast<std::uint16_t>(current[1]));

    current += 2;
    return value;
}

inline std::optional<std::uint32_t> ReadU32(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    if (current == nullptr ||
        end == nullptr ||
        end - current < 4)
    {
        return std::nullopt;
    }

    const std::uint32_t value =
        (static_cast<std::uint32_t>(current[0]) << 24) |
        (static_cast<std::uint32_t>(current[1]) << 16) |
        (static_cast<std::uint32_t>(current[2]) << 8) |
        static_cast<std::uint32_t>(current[3]);

    current += 4;
    return value;
}

inline std::optional<float> ReadF32(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    const auto bits = ReadU32(current, end);
    if (!bits)
    {
        return std::nullopt;
    }

    float result{};
    const std::uint32_t value = *bits;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

inline std::optional<std::uint64_t> ReadU64(
    const std::uint8_t*& current,
    const std::uint8_t* end)
{
    if (current == nullptr ||
        end == nullptr ||
        end - current < 8)
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

struct ParsedHeader
{
    std::uint8_t type = 0;
    std::uint16_t sequence = 0;
    std::uint16_t payloadSize = 0;
    std::uint16_t version = 0;
    std::uint8_t requiresAck = 0;
    const std::uint8_t* payload = nullptr;
};

inline bool IsKnownPacketType(std::uint8_t type)
{
    switch (type)
    {
        case static_cast<std::uint8_t>(PacketType::Movement):
        case static_cast<std::uint8_t>(PacketType::Shoot):
        case static_cast<std::uint8_t>(PacketType::State):
        case static_cast<std::uint8_t>(PacketType::Ack):
        case static_cast<std::uint8_t>(PacketType::Ping):
        case static_cast<std::uint8_t>(PacketType::Pong):
            return true;
        default:
            return false;
    }
}

inline void AppendHeader(
    std::vector<std::uint8_t>& out,
    PacketType type,
    std::uint16_t sequence,
    std::uint16_t payloadSize,
    std::uint8_t requiresAck)
{
    WriteU8(out, static_cast<std::uint8_t>(type));
    WriteU16(out, sequence);
    WriteU16(out, payloadSize);
    WriteU16(out, kProtocolVersion);
    WriteU8(out, requiresAck);
}

// Validates type, version, header length and payload length before the caller
// reads payload fields. Rejects truncated buffers, trailing bytes, an unknown
// requiresAck value and an unsupported protocol version.
inline std::optional<ParsedHeader> ParseHeader(
    const std::uint8_t* data,
    std::size_t size)
{
    if (data == nullptr || size < kPacketHeaderSize)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = data;
    const std::uint8_t* end = data + size;

    const auto type = ReadU8(current, end);
    const auto sequence = ReadU16(current, end);
    const auto payloadSize = ReadU16(current, end);
    const auto version = ReadU16(current, end);
    const auto requiresAck = ReadU8(current, end);

    if (!type || !sequence || !payloadSize || !version || !requiresAck)
    {
        return std::nullopt;
    }

    if (*version != kProtocolVersion)
    {
        return std::nullopt;
    }

    if (*requiresAck != kAckNotRequired &&
        *requiresAck != kAckRequired)
    {
        return std::nullopt;
    }

    const std::size_t expectedSize =
        kPacketHeaderSize +
        static_cast<std::size_t>(*payloadSize);

    if (size != expectedSize || current + *payloadSize != end)
    {
        return std::nullopt;
    }

    ParsedHeader header{};
    header.type = *type;
    header.sequence = *sequence;
    header.payloadSize = *payloadSize;
    header.version = *version;
    header.requiresAck = *requiresAck;
    header.payload = current;
    return header;
}

struct MovementData
{
    std::uint16_t sequence = 0;
    std::uint8_t requiresAck = kAckNotRequired;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ShootData
{
    std::uint16_t sequence = 0;
    std::uint8_t requiresAck = kAckNotRequired;
    std::uint8_t weaponId = 0;
};

struct StateData
{
    std::uint16_t sequence = 0;
    std::uint8_t requiresAck = kAckNotRequired;
    std::uint16_t playerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct AckData
{
    std::uint16_t sequence = 0;
    std::uint16_t acknowledgedSequence = 0;
};

struct PingData
{
    std::uint16_t sequence = 0;
    std::uint64_t clientSendTimeUs = 0;
};

struct PongData
{
    std::uint16_t sequence = 0;
    std::uint64_t clientSendTimeUs = 0;
    std::uint64_t serverReceiveTimeUs = 0;
    std::uint64_t serverSendTimeUs = 0;
};

inline std::vector<std::uint8_t> SerializeMovement(
    std::uint16_t sequence,
    float x,
    float y,
    float z,
    std::uint8_t requiresAck = kAckNotRequired)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kPacketHeaderSize + 12);

    AppendHeader(
        packet,
        PacketType::Movement,
        sequence,
        12,
        requiresAck);

    WriteF32(packet, x);
    WriteF32(packet, y);
    WriteF32(packet, z);
    return packet;
}

inline std::optional<MovementData> ParseMovement(
    const std::uint8_t* data,
    std::size_t size)
{
    const auto header = ParseHeader(data, size);
    if (!header ||
        header->type != static_cast<std::uint8_t>(PacketType::Movement) ||
        header->payloadSize != 12)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = header->payload;
    const std::uint8_t* end = data + size;

    const auto x = ReadF32(current, end);
    const auto y = ReadF32(current, end);
    const auto z = ReadF32(current, end);

    if (!x || !y || !z || current != end)
    {
        return std::nullopt;
    }

    return MovementData{
        header->sequence,
        header->requiresAck,
        *x,
        *y,
        *z
    };
}

inline std::vector<std::uint8_t> SerializeShoot(
    std::uint16_t sequence,
    std::uint8_t weaponId,
    std::uint8_t requiresAck = kAckRequired)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kPacketHeaderSize + 1);

    AppendHeader(
        packet,
        PacketType::Shoot,
        sequence,
        1,
        requiresAck);

    WriteU8(packet, weaponId);
    return packet;
}

inline std::optional<ShootData> ParseShoot(
    const std::uint8_t* data,
    std::size_t size)
{
    const auto header = ParseHeader(data, size);
    if (!header ||
        header->type != static_cast<std::uint8_t>(PacketType::Shoot) ||
        header->payloadSize != 1)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = header->payload;
    const std::uint8_t* end = data + size;
    const auto weaponId = ReadU8(current, end);

    if (!weaponId || current != end)
    {
        return std::nullopt;
    }

    return ShootData{
        header->sequence,
        header->requiresAck,
        *weaponId
    };
}

inline std::vector<std::uint8_t> SerializeState(
    std::uint16_t sequence,
    std::uint16_t playerId,
    float x,
    float y,
    float z)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kPacketHeaderSize + 14);

    AppendHeader(
        packet,
        PacketType::State,
        sequence,
        14,
        kAckNotRequired);

    WriteU16(packet, playerId);
    WriteF32(packet, x);
    WriteF32(packet, y);
    WriteF32(packet, z);
    return packet;
}

inline std::optional<StateData> ParseState(
    const std::uint8_t* data,
    std::size_t size)
{
    const auto header = ParseHeader(data, size);
    if (!header ||
        header->type != static_cast<std::uint8_t>(PacketType::State) ||
        header->payloadSize != 14 ||
        header->requiresAck != kAckNotRequired)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = header->payload;
    const std::uint8_t* end = data + size;

    const auto playerId = ReadU16(current, end);
    const auto x = ReadF32(current, end);
    const auto y = ReadF32(current, end);
    const auto z = ReadF32(current, end);

    if (!playerId || !x || !y || !z || current != end)
    {
        return std::nullopt;
    }

    return StateData{
        header->sequence,
        header->requiresAck,
        *playerId,
        *x,
        *y,
        *z
    };
}

inline std::vector<std::uint8_t> SerializeAck(
    std::uint16_t acknowledgedSequence)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kPacketHeaderSize + 2);

    AppendHeader(
        packet,
        PacketType::Ack,
        acknowledgedSequence,
        2,
        kAckNotRequired);

    WriteU16(packet, acknowledgedSequence);
    return packet;
}

inline std::optional<AckData> ParseAck(
    const std::uint8_t* data,
    std::size_t size)
{
    const auto header = ParseHeader(data, size);
    if (!header ||
        header->type != static_cast<std::uint8_t>(PacketType::Ack) ||
        header->payloadSize != 2 ||
        header->requiresAck != kAckNotRequired)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = header->payload;
    const std::uint8_t* end = data + size;
    const auto acknowledgedSequence = ReadU16(current, end);

    if (!acknowledgedSequence || current != end)
    {
        return std::nullopt;
    }

    if (*acknowledgedSequence != header->sequence)
    {
        return std::nullopt;
    }

    return AckData{
        header->sequence,
        *acknowledgedSequence
    };
}

inline std::vector<std::uint8_t> SerializePing(
    std::uint16_t sequence,
    std::uint64_t clientSendTimeUs)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kPacketHeaderSize + 8);

    AppendHeader(
        packet,
        PacketType::Ping,
        sequence,
        8,
        kAckNotRequired);

    WriteU64(packet, clientSendTimeUs);
    return packet;
}

inline std::optional<PingData> ParsePing(
    const std::uint8_t* data,
    std::size_t size)
{
    const auto header = ParseHeader(data, size);
    if (!header ||
        header->type != static_cast<std::uint8_t>(PacketType::Ping) ||
        header->payloadSize != 8 ||
        header->requiresAck != kAckNotRequired)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = header->payload;
    const std::uint8_t* end = data + size;
    const auto clientSendTimeUs = ReadU64(current, end);

    if (!clientSendTimeUs || current != end)
    {
        return std::nullopt;
    }

    return PingData{
        header->sequence,
        *clientSendTimeUs
    };
}

inline std::vector<std::uint8_t> SerializePong(
    const PongData& pong)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(kPacketHeaderSize + 24);

    AppendHeader(
        packet,
        PacketType::Pong,
        pong.sequence,
        24,
        kAckNotRequired);

    WriteU64(packet, pong.clientSendTimeUs);
    WriteU64(packet, pong.serverReceiveTimeUs);
    WriteU64(packet, pong.serverSendTimeUs);
    return packet;
}

inline std::optional<PongData> ParsePong(
    const std::uint8_t* data,
    std::size_t size)
{
    const auto header = ParseHeader(data, size);
    if (!header ||
        header->type != static_cast<std::uint8_t>(PacketType::Pong) ||
        header->payloadSize != 24 ||
        header->requiresAck != kAckNotRequired)
    {
        return std::nullopt;
    }

    const std::uint8_t* current = header->payload;
    const std::uint8_t* end = data + size;

    const auto clientSendTimeUs = ReadU64(current, end);
    const auto serverReceiveTimeUs = ReadU64(current, end);
    const auto serverSendTimeUs = ReadU64(current, end);

    if (!clientSendTimeUs ||
        !serverReceiveTimeUs ||
        !serverSendTimeUs ||
        current != end)
    {
        return std::nullopt;
    }

    return PongData{
        header->sequence,
        *clientSendTimeUs,
        *serverReceiveTimeUs,
        *serverSendTimeUs
    };
}
