#pragma once

#include <cstdint>

#include "reliability/dedup_window.hpp"

struct ShootDecision
{
    bool duplicate = false;
    bool effectApplied = false;
    bool sendAck = false;
    std::uint16_t acknowledgedSequence = 0;
};

// Applies SHOOT once inside the deduplication window.
// A duplicate still requests an ACK when the command required one,
// but it does not change the shot counter.
class ServerSession
{
public:
    explicit ServerSession(std::uint16_t dedupWindowSize = 256)
        : dedup_(dedupWindowSize)
    {
    }

    ShootDecision OnShoot(
        std::uint16_t sequence,
        std::uint8_t weaponId,
        bool requiresAck)
    {
        ShootDecision decision{};
        decision.acknowledgedSequence = sequence;
        decision.sendAck = requiresAck;
        decision.duplicate = dedup_.Observe(sequence);

        if (!decision.duplicate)
        {
            ++shotsApplied_;
            lastWeaponId_ = weaponId;
            decision.effectApplied = true;
        }

        return decision;
    }

    std::uint32_t ShotsApplied() const
    {
        return shotsApplied_;
    }

    std::uint8_t LastWeaponId() const
    {
        return lastWeaponId_;
    }

private:
    DedupWindow dedup_;
    std::uint32_t shotsApplied_ = 0;
    std::uint8_t lastWeaponId_ = 0;
};
