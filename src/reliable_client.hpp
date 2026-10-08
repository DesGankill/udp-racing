#pragma once

#include <cstdint>
#include <vector>

#include <winsock2.h>

#include "reliability/adaptive_timeout.hpp"
#include "reliability/reliable_channel.hpp"

struct ShootDelivery
{
    std::uint16_t sequence = 0;
    std::uint32_t attempts = 0;
    std::uint32_t retransmissions = 0;
    bool delivered = false;
    bool deliveredFirstTry = false;
    bool failed = false;
    double timeToAckMs = -1.0;
    double rtoAfterMs = 0.0;
};

class ReliableClient
{
public:
    ReliableClient(
        SOCKET socket,
        const sockaddr_in& server,
        AdaptiveTimeout& timeout);

    ShootDelivery SendShoot(
        std::uint16_t sequence,
        std::uint8_t weaponId);

private:
    bool SendRaw(const std::vector<std::uint8_t>& bytes) const;

    SOCKET socket_;
    sockaddr_in server_{};
    AdaptiveTimeout& timeout_;
    ReliableChannel channel_;
};
