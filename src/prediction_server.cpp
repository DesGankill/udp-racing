#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <conio.h>

#include "prediction/car_sim.hpp"
#include "protocol.h"

namespace
{
constexpr unsigned short kPort = 54010;
constexpr int kHostSlot = 1;
constexpr int kClientSlot = 2;

bool SendPacket(
    SOCKET socket,
    const std::vector<std::uint8_t>& packet,
    const sockaddr_in& address)
{
    const int sent = sendto(
        socket,
        reinterpret_cast<const char*>(packet.data()),
        static_cast<int>(packet.size()),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address));
    return sent != SOCKET_ERROR;
}

SnapshotData MakeSnapshot(
    std::uint16_t sequence,
    std::uint16_t playerId,
    const CarState& state,
    std::uint32_t ackTick,
    bool dashApplied)
{
    SnapshotData snapshot{};
    snapshot.sequence = sequence;
    snapshot.playerId = playerId;
    snapshot.ackTick = ackTick;
    snapshot.x = state.x;
    snapshot.z = state.z;
    snapshot.yaw = state.yaw;
    snapshot.speed = state.speed;
    snapshot.cooldown = state.cooldown;
    snapshot.dashApplied = dashApplied ? 1 : 0;
    return snapshot;
}
}

int main(int argc, char* argv[])
{
    bool headless = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--headless")
        {
            headless = true;
        }
    }

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(kPort);
    if (bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed on port " << kPort << '\n';
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    u_long nonBlocking = 1;
    ioctlsocket(serverSocket, FIONBIO, &nonBlocking);

    CarState host = SpawnCar(kHostSlot);
    CarState remote = SpawnCar(kClientSlot);
    std::uint32_t hostTick = 0;
    std::uint32_t remoteAck = 0;
    bool remoteDashApplied = false;
    bool clientKnown = false;
    sockaddr_in clientAddress{};
    std::uint16_t sequence = 1;

    std::cout
        << "Prediction listen server on port "
        << kPort
        << (headless ? " (headless host drives forward)\n" : "\n");
    std::cout
        << "Host keys: W/S throttle, A/D steer. The client is a second process.\n";

    const auto start = std::chrono::steady_clock::now();
    std::uint32_t frame = 0;

    while (true)
    {
        const auto frameDeadline =
            start + std::chrono::milliseconds(33 * (frame + 1));
        std::this_thread::sleep_until(frameDeadline);
        ++frame;

        std::vector<DriveInput> inbox;
        while (true)
        {
            char buffer[512];
            sockaddr_in from{};
            int fromSize = sizeof(from);
            const int received = recvfrom(
                serverSocket,
                buffer,
                sizeof(buffer),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromSize);
            if (received == SOCKET_ERROR)
            {
                break;
            }

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(buffer);
            const auto input = ParseInput(bytes, static_cast<std::size_t>(received));
            if (!input)
            {
                continue;
            }

            clientKnown = true;
            clientAddress = from;
            if (input->tick == 1 && remoteAck != 0)
            {
                remote = SpawnCar(kClientSlot);
                remoteAck = 0;
                remoteDashApplied = false;
                inbox.clear();
                std::cout << "client session reset\n" << std::flush;
            }
            if (input->tick <= remoteAck)
            {
                continue;
            }

            DriveInput command{};
            command.tick = input->tick;
            command.throttle = input->throttle;
            command.steer = input->steer;
            command.dash = input->dash != 0;
            inbox.push_back(command);
        }

        for (std::size_t i = 0; i < inbox.size(); ++i)
        {
            for (std::size_t j = i + 1; j < inbox.size(); ++j)
            {
                if (inbox[j].tick < inbox[i].tick)
                {
                    std::swap(inbox[i], inbox[j]);
                }
            }
        }

        remoteDashApplied = false;
        for (const DriveInput& command : inbox)
        {
            if (command.tick <= remoteAck)
            {
                continue;
            }
            const StepResult stepped = StepCar(remote, command);
            remote = stepped.state;
            remoteAck = command.tick;
            remoteDashApplied = stepped.dashApplied;
        }

        DriveInput hostInput{};
        hostInput.tick = ++hostTick;
        if (headless)
        {
            hostInput.throttle = 1;
            hostInput.steer = (frame / 30) % 2 == 0 ? 1 : -1;
        }
        else
        {
            static int throttle = 0;
            static int steer = 0;
            if (_kbhit())
            {
                const int key = _getch();
                if (key == 'w' || key == 'W') throttle = 1;
                if (key == 's' || key == 'S') throttle = -1;
                if (key == 'a' || key == 'A') steer = -1;
                if (key == 'd' || key == 'D') steer = 1;
                if (key == 'x' || key == 'X')
                {
                    throttle = 0;
                    steer = 0;
                }
                if (key == 'q' || key == 'Q')
                {
                    break;
                }
            }
            hostInput.throttle = throttle;
            hostInput.steer = steer;
        }
        host = StepCar(host, hostInput).state;

        if (clientKnown)
        {
            const auto hostPacket = SerializeSnapshot(
                MakeSnapshot(sequence++, kHostSlot, host, hostTick, false));
            const auto remotePacket = SerializeSnapshot(
                MakeSnapshot(sequence++, kClientSlot, remote, remoteAck, remoteDashApplied));
            SendPacket(serverSocket, hostPacket, clientAddress);
            SendPacket(serverSocket, remotePacket, clientAddress);
        }

        if (frame % 15 == 0)
        {
            std::cout
                << "server tick=" << frame
                << " host x=" << host.x << " z=" << host.z
                << " client x=" << remote.x << " z=" << remote.z
                << " ack=" << remoteAck
                << " dash=" << (remoteDashApplied ? "accepted" : "no")
                << '\n';
        }
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
