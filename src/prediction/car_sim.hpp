#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// Shared step for the listen-server host and the client.
// This is the analog of CharacterMovementComponent: the same input
// and the same previous state always produce the same next state.
// The client never supplies a position.

struct DriveInput
{
    std::uint32_t tick = 0;
    int throttle = 0;
    int steer = 0;
    bool dash = false;
};

struct CarState
{
    std::uint32_t tick = 0;
    float x = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    float speed = 0.0f;
    float cooldown = 0.0f;
};

struct StepResult
{
    CarState state{};
    bool dashApplied = false;
};

constexpr float kSimDt = 1.0f / 30.0f;
constexpr float kSimAccel = 18.0f;
constexpr float kSimBrake = 24.0f;
constexpr float kSimDrag = 6.0f;
constexpr float kSimMaxSpeed = 12.0f;
constexpr float kSimTurnRate = 140.0f;
constexpr float kSimDashBoost = 6.0f;
constexpr float kSimDashCooldown = 0.45f;
constexpr float kSimBound = 20.0f;
constexpr float kCorrectionSnap = 1.2f;

inline CarState SpawnCar(int slot)
{
    CarState state{};
    if (slot == 1)
    {
        state.z = 6.0f;
        state.yaw = 180.0f;
    }
    else
    {
        state.z = -6.0f;
        state.yaw = 0.0f;
    }
    return state;
}

inline float MoveTowards(float value, float target, float maxDelta)
{
    if (std::fabs(value - target) <= maxDelta)
    {
        return target;
    }
    return value < target ? value + maxDelta : value - maxDelta;
}

inline StepResult StepCar(CarState state, DriveInput input)
{
    StepResult result{};
    const int throttle = input.throttle > 0 ? 1 : (input.throttle < 0 ? -1 : 0);
    const int steer = input.steer > 0 ? 1 : (input.steer < 0 ? -1 : 0);

    if (input.dash && state.cooldown <= 0.0001f)
    {
        state.speed += kSimDashBoost;
        state.cooldown = kSimDashCooldown;
        result.dashApplied = true;
    }

    if (throttle > 0)
    {
        state.speed += kSimAccel * kSimDt;
    }
    else if (throttle < 0)
    {
        state.speed -= kSimBrake * kSimDt;
    }
    else
    {
        state.speed = MoveTowards(state.speed, 0.0f, kSimDrag * kSimDt);
    }

    const float maxSpeed = kSimMaxSpeed;
    state.speed = std::clamp(state.speed, -maxSpeed * 0.4f, maxSpeed);

    const float speedFactor = std::clamp(std::fabs(state.speed) / 3.0f, 0.0f, 1.0f);
    float turn = static_cast<float>(steer) * kSimTurnRate * kSimDt * speedFactor;
    if (state.speed < 0.0f)
    {
        turn = -turn;
    }
    state.yaw += turn;

    const float rad = state.yaw * 3.14159265f / 180.0f;
    state.x += std::sin(rad) * state.speed * kSimDt;
    state.z += std::cos(rad) * state.speed * kSimDt;

    if (state.x > kSimBound || state.x < -kSimBound ||
        state.z > kSimBound || state.z < -kSimBound)
    {
        state.x = std::clamp(state.x, -kSimBound, kSimBound);
        state.z = std::clamp(state.z, -kSimBound, kSimBound);
        state.speed = 0.0f;
    }

    state.cooldown = std::max(0.0f, state.cooldown - kSimDt);
    state.tick = input.tick;
    result.state = state;
    return result;
}
