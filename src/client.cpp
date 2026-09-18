#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <winsock2.h>

#include "protocol.h"

int main()
{
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

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");
    serverAddress.sin_port = htons(54000);

    for (uint16_t sequence = 1; sequence <= 10; ++sequence)
    {
        bool sendMovement = (sequence % 2 == 1);

        PacketHeader header{};
        header.type = static_cast<uint8_t>(
            sendMovement
                ? PacketType::Movement
                : PacketType::Shoot);

        header.sequence = htons(sequence);

        if (sendMovement)
        {
            header.payloadSize = htons(sizeof(MovementPayload));
        }
        else
        {
            header.payloadSize = htons(sizeof(ShootPayload));
        }

        header.version = htons(kProtocolVersion);

        char buffer[1024];
        int packetSize = 0;

        if (sendMovement)
        {
            MovementPayload movement{};
            movement.x = 10.0f + sequence;
            movement.y = 20.0f;
            movement.z = 0.0f;

            std::memcpy(
                buffer,
                &header,
                sizeof(PacketHeader));

            uint32_t xNetwork = floatToNetwork(movement.x);
uint32_t yNetwork = floatToNetwork(movement.y);
uint32_t zNetwork = floatToNetwork(movement.z);

std::memcpy(
    buffer + sizeof(PacketHeader),
    &xNetwork,
    sizeof(uint32_t));

std::memcpy(
    buffer + sizeof(PacketHeader) + sizeof(uint32_t),
    &yNetwork,
    sizeof(uint32_t));

std::memcpy(
    buffer + sizeof(PacketHeader) + sizeof(uint32_t) * 2,
    &zNetwork,
    sizeof(uint32_t));

            packetSize =
                sizeof(PacketHeader) +
                sizeof(MovementPayload);

            std::cout << "Sending MOVEMENT, sequence="
                      << sequence
                      << '\n';
        }
        else
        {
            ShootPayload shoot{};
            shoot.weaponId = 1;

            std::memcpy(
                buffer,
                &header,
                sizeof(PacketHeader));

            std::memcpy(
                buffer + sizeof(PacketHeader),
                &shoot,
                sizeof(ShootPayload));

            packetSize =
                sizeof(PacketHeader) +
                sizeof(ShootPayload);

            std::cout << "Sending SHOOT, sequence="
                      << sequence
                      << '\n';
        }

        int sentBytes = sendto(
            clientSocket,
            buffer,
            packetSize,
            0,
            reinterpret_cast<sockaddr*>(&serverAddress),
            sizeof(serverAddress));

        if (sentBytes == SOCKET_ERROR)
        {
            std::cerr << "sendto failed\n";
            break;
        }

        std::cout << "Sent "
                  << sentBytes
                  << " bytes\n";

        char responseBuffer[1024];

        sockaddr_in responseAddress{};
        int responseAddressSize =
            sizeof(responseAddress);

        int receivedBytes = recvfrom(
            clientSocket,
            responseBuffer,
            sizeof(responseBuffer),
            0,
            reinterpret_cast<sockaddr*>(&responseAddress),
            &responseAddressSize);

        if (receivedBytes == SOCKET_ERROR)
        {
            std::cerr << "recvfrom failed\n";
            break;
        }

        if (receivedBytes >=
            static_cast<int>(sizeof(PacketHeader)))
        {
            PacketHeader responseHeader{};

            std::memcpy(
                &responseHeader,
                responseBuffer,
                sizeof(PacketHeader));

            uint16_t responseSequence =
                ntohs(responseHeader.sequence);

            uint16_t responsePayloadSize =
                ntohs(responseHeader.payloadSize);

            std::cout << "Received response: "
                      << "type="
                      << static_cast<int>(
                             responseHeader.type)
                      << ", sequence="
                      << responseSequence
                      << ", payloadSize="
                      << responsePayloadSize
                      << '\n';

            if (responseHeader.type ==
                static_cast<uint8_t>(PacketType::State))
            {
                if (receivedBytes >=
                    static_cast<int>(
                        sizeof(PacketHeader) +
                        sizeof(StatePayload)))
                {
                    uint16_t playerIdNetwork{};
uint32_t xNetwork{};
uint32_t yNetwork{};
uint32_t zNetwork{};

std::memcpy(
    &playerIdNetwork,
    responseBuffer + sizeof(PacketHeader),
    sizeof(uint16_t));

std::memcpy(
    &xNetwork,
    responseBuffer + sizeof(PacketHeader) + sizeof(uint16_t),
    sizeof(uint32_t));

std::memcpy(
    &yNetwork,
    responseBuffer + sizeof(PacketHeader) +
        sizeof(uint16_t) + sizeof(uint32_t),
    sizeof(uint32_t));

std::memcpy(
    &zNetwork,
    responseBuffer + sizeof(PacketHeader) +
        sizeof(uint16_t) + sizeof(uint32_t) * 2,
    sizeof(uint32_t));

uint16_t playerId = ntohs(playerIdNetwork);

float x = floatFromNetwork(xNetwork);
float y = floatFromNetwork(yNetwork);
float z = floatFromNetwork(zNetwork);

std::cout << "State: "
          << "playerId="
          << playerId
          << ", x="
          << x
          << ", y="
          << y
          << ", z="
          << z
          << '\n';
                }
            }
            else if (responseHeader.type ==
                     static_cast<uint8_t>(PacketType::Ack))
            {
                std::cout << "ACK received\n";
            }
        }

        std::cout << '\n';

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    closesocket(clientSocket);
    WSACleanup();

    return 0;
}