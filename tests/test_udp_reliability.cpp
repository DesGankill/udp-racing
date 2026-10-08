#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include <winsock2.h>

#include "../src/experiment_profiles.hpp"
#include "../src/packet_handler.h"
#include "../src/reliable_client.hpp"

namespace
{

int gFailures = 0;

void Expect(bool condition, const char* expression, int line)
{
    if (condition)
    {
        return;
    }

    std::cerr
        << "FAIL " << __FILE__ << ":" << line
        << " " << expression << '\n';
    ++gFailures;
}

#define EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)

struct ServerShared
{
    SOCKET socket = INVALID_SOCKET;
    NetworkImpairment* impairment = nullptr;
    ServerSession* session = nullptr;
    std::atomic<bool> stop{false};
};

void ServerLoop(ServerShared* shared)
{
    while (!shared->stop.load())
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(shared->socket, &readSet);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;

        const int ready = select(
            0,
            &readSet,
            nullptr,
            nullptr,
            &timeout);

        if (ready <= 0)
        {
            continue;
        }

        char buffer[1024];
        sockaddr_in client{};
        int clientSize = sizeof(client);

        const int received = recvfrom(
            shared->socket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&client),
            &clientSize);

        if (received == SOCKET_ERROR)
        {
            continue;
        }

        handlePacket(
            shared->socket,
            buffer,
            received,
            client,
            clientSize,
            *shared->impairment,
            *shared->session);
    }
}

void RunCase(
    const char* name,
    int forcedIncoming,
    int forcedOutgoing,
    bool expectDelivered,
    std::uint32_t expectAttempts,
    std::uint32_t expectShots)
{
    std::cout << "\nCASE " << name << '\n';

    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    EXPECT(serverSocket != INVALID_SOCKET);

    BOOL reuse = TRUE;
    setsockopt(
        serverSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse),
        sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(54021);

    const int bound = bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address));
    EXPECT(bound != SOCKET_ERROR);

    ExperimentProfile profile = ReliableProfile("baseline");
    NetworkImpairment impairment(ConditionsFromProfile(profile));
    impairment.ForceIncomingDrops(forcedIncoming);
    impairment.ForceOutgoingDrops(forcedOutgoing);
    ServerSession session;

    ServerShared shared{};
    shared.socket = serverSocket;
    shared.impairment = &impairment;
    shared.session = &session;

    std::thread server(ServerLoop, &shared);

    SOCKET clientSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    EXPECT(clientSocket != INVALID_SOCKET);

    AdaptiveTimeout timeout;
    ReliableClient client(clientSocket, address, timeout);
    const ShootDelivery delivery = client.SendShoot(1, 1);

    shared.stop.store(true);
    server.join();

    EXPECT(delivery.delivered == expectDelivered);
    EXPECT(delivery.failed == !expectDelivered);
    EXPECT(delivery.attempts == expectAttempts);
    EXPECT(session.ShotsApplied() == expectShots);

    if (expectDelivered)
    {
        EXPECT(delivery.retransmissions + 1 == expectAttempts);
    }

    closesocket(clientSocket);
    closesocket(serverSocket);

    std::cout
        << name
        << " delivered="
        << delivery.delivered
        << " attempts="
        << delivery.attempts
        << " shots="
        << session.ShotsApplied()
        << '\n';
}

} // namespace

int main()
{
    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return EXIT_FAILURE;
    }

    RunCase("no_loss", 0, 0, true, 1, 1);
    RunCase("lost_shoot", 1, 0, true, 2, 1);
    RunCase("lost_ack", 0, 1, true, 2, 1);
    RunCase("exhausted", 20, 0, false, 5, 0);

    WSACleanup();

    if (gFailures != 0)
    {
        std::cerr << gFailures << " UDP reliability checks failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "UDP reliability tests passed\n";
    return EXIT_SUCCESS;
}
