#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/prediction/predictor.hpp"
#include "../src/protocol.h"

namespace
{
bool Close(float left, float right)
{
    return std::fabs(left - right) < 0.0001f;
}

DriveInput Command(std::uint32_t tick, int throttle, bool dash = false)
{
    DriveInput input{};
    input.tick = tick;
    input.throttle = throttle;
    input.dash = dash;
    return input;
}
}

int main()
{
    {
        CarState left = SpawnCar(2);
        CarState right = SpawnCar(2);
        for (std::uint32_t tick = 1; tick <= 20; ++tick)
        {
            left = StepCar(left, Command(tick, 1, tick == 5)).state;
            right = StepCar(right, Command(tick, 1, tick == 5)).state;
        }
        assert(Close(left.x, right.x));
        assert(Close(left.z, right.z));
        assert(Close(left.speed, right.speed));
    }

    {
        CarState state = SpawnCar(2);
        const StepResult first = StepCar(state, Command(1, 1, true));
        const StepResult second = StepCar(first.state, Command(2, 1, true));
        assert(first.dashApplied);
        assert(!second.dashApplied);
        assert(second.state.cooldown > 0.0f);
    }

    {
        OwnerProxy proxy(SpawnCar(2));
        CarState server = SpawnCar(2);
        for (std::uint32_t tick = 1; tick <= 5; ++tick)
        {
            const DriveInput input = Command(tick, 1, tick == 3);
            proxy.NoteInput(input);
            if (tick <= 3)
            {
                server = StepCar(server, input).state;
            }
        }

        proxy.OnServer(server, 3);
        CarState full = SpawnCar(2);
        for (std::uint32_t tick = 1; tick <= 5; ++tick)
        {
            full = StepCar(full, Command(tick, 1, tick == 3)).state;
        }
        assert(Close(proxy.Shown().x, full.x));
        assert(Close(proxy.Shown().z, full.z));
        assert(proxy.Pending() == 2);
    }

    {
        OwnerProxy proxy(SpawnCar(2));
        const DriveInput input = Command(1, 1);
        proxy.NoteInput(input);
        proxy.Cheat();
        const float cheated = proxy.Shown().x;
        CarState server = StepCar(SpawnCar(2), input).state;
        proxy.OnServer(server, 1);
        assert(std::fabs(cheated - server.x) > 1.0f);
        assert(Close(proxy.Shown().x, server.x));
        assert(proxy.Snapped());
    }

    {
        OwnerProxy proxy(SpawnCar(2));
        proxy.SetPrediction(false);
        proxy.NoteInput(Command(1, 1));
        assert(Close(proxy.Shown().z, SpawnCar(2).z));
        CarState server = StepCar(SpawnCar(2), Command(1, 1)).state;
        proxy.OnServer(server, 1);
        assert(Close(proxy.Shown().z, server.z));
    }

    {
        const auto packet = SerializeInput(9, 15, 1, -1, 1);
        const auto parsed = ParseInput(packet.data(), packet.size());
        assert(parsed);
        assert(parsed->tick == 15);
        assert(parsed->throttle == 1);
        assert(parsed->steer == -1);
        assert(parsed->dash == 1);
        assert(packet.size() == kPacketHeaderSize + kInputPayloadSize);

        SnapshotData snapshot{};
        snapshot.sequence = 3;
        snapshot.playerId = 2;
        snapshot.ackTick = 15;
        snapshot.x = 1.5f;
        snapshot.z = -4.0f;
        snapshot.yaw = 12.0f;
        snapshot.speed = 3.0f;
        snapshot.cooldown = 0.2f;
        snapshot.dashApplied = 0;
        const auto bytes = SerializeSnapshot(snapshot);
        const auto back = ParseSnapshot(bytes.data(), bytes.size());
        assert(back);
        assert(back->playerId == 2);
        assert(back->ackTick == 15);
        assert(Close(back->x, 1.5f));
        assert(Close(back->z, -4.0f));
        assert(back->dashApplied == 0);
    }

    std::cout << "PREDICTION_TESTS_OK\n";
    return 0;
}
