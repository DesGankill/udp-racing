#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "prediction/car_sim.hpp"

// Autonomous proxy: saved moves plus optional local prediction.
// Simulated proxies are not stored here. They only render server snapshots.
class OwnerProxy
{
public:
    explicit OwnerProxy(CarState spawn)
        : predicted_(spawn),
          shown_(spawn),
          server_(spawn)
    {
    }

    void SetPrediction(bool enabled)
    {
        enabled_ = enabled;
        if (!enabled_)
        {
            predicted_ = server_;
            shown_ = server_;
        }
    }

    bool PredictionEnabled() const
    {
        return enabled_;
    }

    void NoteInput(DriveInput input)
    {
        snapped_ = false;
        pending_.push_back(input);
        if (!enabled_)
        {
            return;
        }

        predicted_ = StepCar(predicted_, input).state;
        shown_ = predicted_;
    }

    // Local-only position cheat. It is not part of any saved move.
    void Cheat()
    {
        predicted_.x += 8.0f;
        shown_ = predicted_;
    }

    // Server reconciliation. Inputs at or before ackTick are dropped.
    // The rest are replayed on top of the authoritative pose.
    void OnServer(CarState authoritative, std::uint32_t ackTick)
    {
        server_ = authoritative;
        pending_.erase(
            std::remove_if(
                pending_.begin(),
                pending_.end(),
                [ackTick](const DriveInput& input) {
                    return input.tick <= ackTick;
                }),
            pending_.end());

        if (!enabled_)
        {
            predicted_ = server_;
            shown_ = server_;
            lastError_ = 0.0f;
            snapped_ = false;
            return;
        }

        CarState replay = server_;
        for (const DriveInput& input : pending_)
        {
            replay = StepCar(replay, input).state;
        }

        const float dx = replay.x - shown_.x;
        const float dz = replay.z - shown_.z;
        lastError_ = std::sqrt(dx * dx + dz * dz);
        snapped_ = lastError_ > kCorrectionSnap;
        predicted_ = replay;
        shown_ = replay;
    }

    const CarState& Shown() const
    {
        return shown_;
    }

    const CarState& Server() const
    {
        return server_;
    }

    std::size_t Pending() const
    {
        return pending_.size();
    }

    float LastError() const
    {
        return lastError_;
    }

    bool Snapped() const
    {
        return snapped_;
    }

private:
    CarState predicted_{};
    CarState shown_{};
    CarState server_{};
    std::vector<DriveInput> pending_{};
    bool enabled_ = true;
    float lastError_ = 0.0f;
    bool snapped_ = false;
};

// Network smoothing for the other player. It never feeds the simulation.
struct RemoteProxy
{
    bool has = false;
    CarState raw{};
    float x = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;

    void Push(const CarState& state)
    {
        raw = state;
        if (!has)
        {
            x = state.x;
            z = state.z;
            yaw = state.yaw;
            has = true;
            return;
        }

        x += (state.x - x) * 0.35f;
        z += (state.z - z) * 0.35f;
        yaw += (state.yaw - yaw) * 0.35f;
    }
};
