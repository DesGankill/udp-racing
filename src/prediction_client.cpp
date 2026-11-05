#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <conio.h>

#include "prediction/predictor.hpp"
#include "protocol.h"

namespace
{
constexpr unsigned short kPort = 54010;

struct Options
{
    std::string host = "127.0.0.1";
    bool predict = true;
    bool script = false;
    bool cheat = false;
    int lagMs = 0;
    int lossPercent = 0;
    int ticks = 45;
    std::string logPath;
};

struct DelayedPacket
{
    std::chrono::steady_clock::time_point due{};
    std::vector<std::uint8_t> bytes;
};

Options ParseArgs(int argc, char* argv[])
{
    Options options{};
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--no-predict") options.predict = false;
        else if (arg == "--script") options.script = true;
        else if (arg == "--cheat") options.cheat = true;
        else if (arg == "--lag-ms" && i + 1 < argc) options.lagMs = std::stoi(argv[++i]);
        else if (arg == "--loss" && i + 1 < argc) options.lossPercent = std::stoi(argv[++i]);
        else if (arg == "--ticks" && i + 1 < argc) options.ticks = std::stoi(argv[++i]);
        else if (arg == "--log" && i + 1 < argc) options.logPath = argv[++i];
        else if (arg.rfind("--", 0) != 0) options.host = arg;
    }
    if (options.lagMs < 0) options.lagMs = 0;
    if (options.lossPercent < 0) options.lossPercent = 0;
    if (options.lossPercent > 100) options.lossPercent = 100;
    return options;
}

bool Drop(std::mt19937& rng, int lossPercent)
{
    if (lossPercent <= 0)
    {
        return false;
    }
    std::uniform_int_distribution<int> roll(0, 99);
    return roll(rng) < lossPercent;
}
}

int main(int argc, char* argv[])
{
    const Options options = ParseArgs(argc, argv);
    std::ofstream log;
    if (!options.logPath.empty())
    {
        log.open(options.logPath, std::ios::out | std::ios::trunc);
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    u_long nonBlocking = 1;
    ioctlsocket(clientSocket, FIONBIO, &nonBlocking);

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(kPort);
    serverAddress.sin_addr.s_addr = inet_addr(options.host.c_str());

    OwnerProxy self(SpawnCar(2));
    self.SetPrediction(options.predict);
    RemoteProxy other{};
    std::mt19937 rng(12345);
    std::vector<DelayedPacket> outbound;
    std::vector<DelayedPacket> inbound;
    std::uint16_t sequence = 1;
    std::uint32_t tick = 0;
    bool cheatDone = false;
    std::vector<std::uint32_t> dashTicks;
    int latchedThrottle = 0;
    int latchedSteer = 0;

    auto emit = [&](const std::string& line) {
        std::cout << line << '\n';
        if (log.is_open())
        {
            log << line << '\n';
        }
    };

    emit(std::string("mode=") + (options.predict ? "predict" : "no-predict") +
        " lag_ms=" + std::to_string(options.lagMs) +
        " loss=" + std::to_string(options.lossPercent) +
        " one_way, rtt_about=" + std::to_string(options.lagMs * 2));

    const auto start = std::chrono::steady_clock::now();
    while (static_cast<int>(tick) < options.ticks)
    {
        const auto frameDeadline =
            start + std::chrono::milliseconds(33 * (tick + 1));
        std::this_thread::sleep_until(frameDeadline);
        ++tick;

        DriveInput input{};
        input.tick = tick;
        if (options.script)
        {
            input.throttle = 1;
            input.dash = tick == 12 || tick == 13;
            if (options.cheat && tick == 20 && !cheatDone)
            {
                self.Cheat();
                cheatDone = true;
                emit("cheat local x += 8, not sent");
            }
        }
        else if (_kbhit())
        {
            const int key = _getch();
            if (key == 'w' || key == 'W') latchedThrottle = 1;
            if (key == 's' || key == 'S') latchedThrottle = -1;
            if (key == 'a' || key == 'A') latchedSteer = -1;
            if (key == 'd' || key == 'D') latchedSteer = 1;
            if (key == 'x' || key == 'X')
            {
                latchedThrottle = 0;
                latchedSteer = 0;
            }
            if (key == 'f' || key == 'F') input.dash = true;
            if (key == 'c' || key == 'C')
            {
                self.Cheat();
                emit("cheat local x += 8, not sent");
            }
            if (key == 'p' || key == 'P')
            {
                self.SetPrediction(!self.PredictionEnabled());
                emit(std::string("prediction ") +
                    (self.PredictionEnabled() ? "on" : "off"));
            }
            if (key == 'q' || key == 'Q')
            {
                break;
            }
        }
        if (!options.script)
        {
            input.throttle = latchedThrottle;
            input.steer = latchedSteer;
        }

        if (input.dash)
        {
            dashTicks.push_back(input.tick);
        }
        self.NoteInput(input);

        if (!Drop(rng, options.lossPercent))
        {
            DelayedPacket packet{};
            packet.due = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(options.lagMs);
            packet.bytes = SerializeInput(
                sequence++,
                input.tick,
                static_cast<std::int8_t>(input.throttle),
                static_cast<std::int8_t>(input.steer),
                input.dash ? 1 : 0);
            outbound.push_back(std::move(packet));
        }

        const auto now = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < outbound.size();)
        {
            if (outbound[i].due > now)
            {
                ++i;
                continue;
            }
            sendto(
                clientSocket,
                reinterpret_cast<const char*>(outbound[i].bytes.data()),
                static_cast<int>(outbound[i].bytes.size()),
                0,
                reinterpret_cast<const sockaddr*>(&serverAddress),
                sizeof(serverAddress));
            outbound.erase(outbound.begin() + static_cast<std::ptrdiff_t>(i));
        }

        while (true)
        {
            char buffer[512];
            sockaddr_in from{};
            int fromSize = sizeof(from);
            const int received = recvfrom(
                clientSocket,
                buffer,
                sizeof(buffer),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromSize);
            if (received == SOCKET_ERROR)
            {
                break;
            }
            if (Drop(rng, options.lossPercent))
            {
                continue;
            }
            DelayedPacket packet{};
            packet.due = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(options.lagMs);
            packet.bytes.assign(
                reinterpret_cast<const std::uint8_t*>(buffer),
                reinterpret_cast<const std::uint8_t*>(buffer) + received);
            inbound.push_back(std::move(packet));
        }

        for (std::size_t i = 0; i < inbound.size();)
        {
            if (inbound[i].due > now)
            {
                ++i;
                continue;
            }
            const auto snapshot = ParseSnapshot(
                inbound[i].bytes.data(),
                inbound[i].bytes.size());
            inbound.erase(inbound.begin() + static_cast<std::ptrdiff_t>(i));
            if (!snapshot)
            {
                continue;
            }
            if (snapshot->playerId == 1)
            {
                CarState remote{};
                remote.tick = snapshot->ackTick;
                remote.x = snapshot->x;
                remote.z = snapshot->z;
                remote.yaw = snapshot->yaw;
                remote.speed = snapshot->speed;
                remote.cooldown = snapshot->cooldown;
                other.Push(remote);
                continue;
            }
            if (snapshot->playerId != 2)
            {
                continue;
            }

            CarState authoritative{};
            authoritative.tick = snapshot->ackTick;
            authoritative.x = snapshot->x;
            authoritative.z = snapshot->z;
            authoritative.yaw = snapshot->yaw;
            authoritative.speed = snapshot->speed;
            authoritative.cooldown = snapshot->cooldown;
            self.OnServer(authoritative, snapshot->ackTick);
            for (std::size_t dashIndex = 0; dashIndex < dashTicks.size();)
            {
                if (dashTicks[dashIndex] != snapshot->ackTick)
                {
                    ++dashIndex;
                    continue;
                }
                emit(snapshot->dashApplied
                    ? "server accepted dash at tick " + std::to_string(snapshot->ackTick)
                    : "server rejected dash at tick " + std::to_string(snapshot->ackTick) + ": cooldown");
                dashTicks.erase(dashTicks.begin() + static_cast<std::ptrdiff_t>(dashIndex));
            }
        }

        const CarState& shown = self.Shown();
        std::string line =
            "tick=" + std::to_string(tick) +
            " predict=" + std::string(self.PredictionEnabled() ? "on" : "off") +
            " display_x=" + std::to_string(shown.x) +
            " display_z=" + std::to_string(shown.z) +
            " server_x=" + std::to_string(self.Server().x) +
            " server_z=" + std::to_string(self.Server().z) +
            " pending=" + std::to_string(self.Pending()) +
            " error=" + std::to_string(self.LastError()) +
            (self.Snapped() ? " correction=snap" : "");
        if (other.has)
        {
            line += " other_raw_x=" + std::to_string(other.raw.x) +
                " other_smooth_x=" + std::to_string(other.x);
        }
        emit(line);
    }

    closesocket(clientSocket);
    WSACleanup();
    return 0;
}
