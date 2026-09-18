#pragma once

#include <cstdint>
#include <cstring>
#include <winsock2.h>

constexpr uint16_t kProtocolVersion = 1;

enum class PacketType : uint8_t
{
    Movement = 1,
    Shoot = 2,
    State = 3,
    Ack = 4
};

#pragma pack(push, 1)

struct PacketHeader
{
    uint8_t type;
    uint16_t sequence;
    uint16_t payloadSize;
    uint16_t version;
};

struct MovementPayload
{
    float x;
    float y;
    float z;
};

struct ShootPayload
{
    uint8_t weaponId;
};

struct StatePayload
{
    uint16_t playerId;
    float x;
    float y;
    float z;
};

#pragma pack(pop)

inline uint32_t floatToNetwork(float value)
{
    uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(float));
    return htonl(bits);
}

inline float floatFromNetwork(uint32_t value)
{
    value = ntohl(value);

    float result{};
    std::memcpy(&result, &value, sizeof(float));

    return result;
}