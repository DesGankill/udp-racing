#include <iostream>
#include <winsock2.h>

#include "protocol.h"
#include "packet_handler.h"

int main()
{
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

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddress.sin_port = htons(54000);

    if (bind(
        serverSocket,
        reinterpret_cast<sockaddr*>(&serverAddress),
        sizeof(serverAddress)) == SOCKET_ERROR)
    {
        std::cerr << "Bind failed\n";
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "UDP server started on port 54000\n";
    std::cout << "Waiting for packets...\n";

    while (true)
    {
        char buffer[1024];

        sockaddr_in clientAddress{};
        int clientAddressSize = sizeof(clientAddress);

        int receivedBytes = recvfrom(
            serverSocket,
            buffer,
            sizeof(buffer),
            0,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressSize);

        if (receivedBytes == SOCKET_ERROR)
        {
            std::cerr << "recvfrom failed\n";
            break;
        }

        handlePacket(
            serverSocket,
            buffer,
            receivedBytes,
            clientAddress,
            clientAddressSize);
    }

    closesocket(serverSocket);
    WSACleanup();

    return 0;
}