#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <winsock2.h>

#include "protocol.h"
#include "telemetry.h"
#include "csv_logger.h"

struct PingResult
{
    std::uint16_t sequence = 0;
    std::uint64_t sentAtUs = 0;
    double rttMs = 0.0;
    double srttMs = 0.0;
    ResponseStatus status = ResponseStatus::Unknown;
};

struct ExperimentConfig
{
    std::string id;
    int samples = 50;
    int intervalMs = 500;

    int delayMs = 0;
    int jitterMs = 0;
    int lossPercent = 0;
};

PingResult SendPingAndWait(
    SOCKET clientSocket,
    const sockaddr_in& serverAddress,
    Telemetry& telemetry,
    std::uint16_t sequence,
    const std::string& seriesId);

PingResult SendPingAndWait(
    SOCKET clientSocket,
    const sockaddr_in& serverAddress,
    Telemetry& telemetry,
    std::uint16_t sequence,
    const std::string& seriesId)
{
    PingResult result{};
    result.sequence = sequence;

    const auto sendTime =
        std::chrono::steady_clock::now();

    const auto sendTimeUs =
        std::chrono::duration_cast<
            std::chrono::microseconds>(
            sendTime.time_since_epoch())
            .count();

    result.sentAtUs =
        static_cast<std::uint64_t>(sendTimeUs);

    const auto pingPacket =
        SerializePing(
            sequence,
            static_cast<std::uint64_t>(sendTimeUs));

    telemetry.OnPingSent(
        sequence,
        static_cast<std::uint64_t>(sendTimeUs),
        seriesId);

    const int sentBytes = sendto(
        clientSocket,
        reinterpret_cast<const char*>(
            pingPacket.data()),
        static_cast<int>(
            pingPacket.size()),
        0,
        reinterpret_cast<const sockaddr*>(
            &serverAddress),
        sizeof(serverAddress));

    if (sentBytes == SOCKET_ERROR)
    {
        std::cerr
            << "PING sendto failed\n";

        result.status = ResponseStatus::Unknown;
        return result;
    }

    std::cout
        << "PING sent: sequence="
        << sequence
        << ", bytes="
        << sentBytes
        << '\n';

    char responseBuffer[1024]{};

    sockaddr_in responseAddress{};
    int responseAddressSize =
        sizeof(responseAddress);

    const auto deadline =
        sendTime +
        std::chrono::milliseconds(1000);

    while (true)
    {
        const auto now =
            std::chrono::steady_clock::now();

        if (now >= deadline)
        {
            const auto nowUs =
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    now.time_since_epoch())
                    .count();

            telemetry.Expire(
                static_cast<std::uint64_t>(
                    nowUs));

            result.status =
                ResponseStatus::Timeout;

            std::cout
                << "PING timeout: sequence="
                << sequence
                << '\n';

            return result;
        }

        const auto remaining =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                deadline - now)
                .count();

        timeval timeout{};
        timeout.tv_sec =
            static_cast<long>(
                remaining / 1'000'000);

        timeout.tv_usec =
            static_cast<long>(
                remaining % 1'000'000);

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);

        const int selectResult =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout);

        if (selectResult == SOCKET_ERROR)
        {
            std::cerr
                << "select failed\n";

            result.status =
                ResponseStatus::Unknown;

            return result;
        }

        if (selectResult == 0)
        {
            const auto timeoutNow =
                std::chrono::steady_clock::now();

            const auto timeoutNowUs =
                std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    timeoutNow.time_since_epoch())
                    .count();

            telemetry.Expire(
                static_cast<std::uint64_t>(
                    timeoutNowUs));

            result.status =
                ResponseStatus::Timeout;

            std::cout
                << "PING timeout: sequence="
                << sequence
                << '\n';

            return result;
        }

        const int receivedBytes =
            recvfrom(
                clientSocket,
                responseBuffer,
                sizeof(responseBuffer),
                0,
                reinterpret_cast<sockaddr*>(
                    &responseAddress),
                &responseAddressSize);

        if (receivedBytes == SOCKET_ERROR)
        {
            std::cerr
                << "PONG recvfrom failed\n";

            result.status =
                ResponseStatus::Unknown;

            return result;
        }

        const auto pong =
            ParsePong(
                reinterpret_cast<
                    const std::uint8_t*>(
                    responseBuffer),
                static_cast<std::size_t>(
                    receivedBytes));

        if (!pong)
        {
            std::cerr
                << "Invalid PONG packet\n";

            continue;
        }

        const auto receiveTime =
            std::chrono::steady_clock::now();

        const auto receiveTimeUs =
            std::chrono::duration_cast<
                std::chrono::microseconds>(
                receiveTime.time_since_epoch())
                .count();

        const ResponseStatus status =
            telemetry.OnPong(
                pong->sequence,
                static_cast<std::uint64_t>(
                    receiveTimeUs));

        if (pong->sequence != sequence)
        {
            continue;
        }

        result.status = status;
        result.rttMs =
            static_cast<double>(
                receiveTimeUs - sendTimeUs)
            / 1000.0;

        result.srttMs =
            telemetry.SrttMs();

        std::cout
            << "PONG received: sequence="
            << pong->sequence
            << ", status="
            << static_cast<int>(status)
            << ", RTT="
            << result.rttMs
            << " ms, SRTT="
            << result.srttMs
            << " ms\n";

        return result;
    }
}

void RunExperiment(
    const ExperimentConfig& config,
    SOCKET clientSocket,
    const sockaddr_in& serverAddress,
    CsvLogger& csvLogger,
    std::uint16_t& pingSequence)
{
    Telemetry telemetry;

    std::cout
        << "\n=== Experiment: "
        << config.id
        << " ===\n";

    for (int i = 0; i < config.samples; ++i)
    {
        const PingResult result =
            SendPingAndWait(
                clientSocket,
                serverAddress,
                telemetry,
                pingSequence,
                config.id);

        CsvRecord record{};
        record.experimentId = config.id;
        record.sample =
            static_cast<std::size_t>(i + 1);
        record.sequence = result.sequence;
        record.sentAtMs =
            result.sentAtUs / 1000;
        record.rttMs = result.rttMs;
        record.srttMs = result.srttMs;
        record.status = result.status;

        csvLogger.Write(record);

        ++pingSequence;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                config.intervalMs));
    }
        std::cout
        << "\n--- Statistics: "
        << config.id
        << " ---\n";

    std::cout
        << "Sent: "
        << telemetry.SentCount()
        << '\n';

    std::cout
        << "Received: "
        << telemetry.ReceivedCount()
        << '\n';

    std::cout
        << "Timeout: "
        << telemetry.TimeoutCount()
        << '\n';

    std::cout
        << "Late: "
        << telemetry.LateCount()
        << '\n';

    std::cout
        << "Min RTT: "
        << telemetry.MinRttMs()
        << " ms\n";

    std::cout
        << "Max RTT: "
        << telemetry.MaxRttMs()
        << " ms\n";

    std::cout
        << "Mean RTT: "
        << telemetry.MeanRttMs()
        << " ms\n";

    std::cout
        << "Median RTT: "
        << telemetry.MedianRttMs()
        << " ms\n";

    std::cout
        << "SRTT: "
        << telemetry.SrttMs()
        << " ms\n";

    std::cout
        << "Jitter: "
        << telemetry.MeanJitterMs()
        << " ms\n";

    std::cout
        << "Loss: "
        << telemetry.LossPercent()
        << " %\n";
}

const ExperimentConfig& GetExperimentConfig(
    const std::string& experimentId)
{
    static const ExperimentConfig baseline{
        "baseline",
        50,
        500,
        0,
        0,
        0
    };

    static const ExperimentConfig delay50{
        "delay_50",
        50,
        500,
        50,
        0,
        0
    };

    static const ExperimentConfig delay100{
        "delay_100",
        50,
        500,
        100,
        0,
        0
    };

    static const ExperimentConfig jitter{
        "jitter",
        50,
        500,
        50,
        20,
        0
    };

    static const ExperimentConfig loss5{
        "loss_5",
        50,
        500,
        0,
        0,
        5
    };

    static const ExperimentConfig combined{
        "combined",
        50,
        500,
        50,
        20,
        5
    };

    if (experimentId == "delay_50")
        return delay50;

    if (experimentId == "delay_100")
        return delay100;

    if (experimentId == "jitter")
        return jitter;

    if (experimentId == "loss_5")
        return loss5;

    if (experimentId == "combined")
        return combined;

    return baseline;
}

int main(int argc, char* argv[])
{
    std::string experimentId = "baseline";

    if (argc >= 2)
    {
        experimentId = argv[1];
    }

    const ExperimentConfig& experiment =
        GetExperimentConfig(experimentId);

    std::cout
        << "Experiment: "
        << experiment.id
        << '\n';

    WSADATA wsaData{};

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET clientSocket =
        socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    Telemetry telemetry;
    CsvLogger csvLogger("results.csv");

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr =
        inet_addr("127.0.0.1");
    serverAddress.sin_port = htons(54000);

    // -------------------------
    // PING / PONG test
    // -------------------------

    std::uint16_t pingSequence = 100;

RunExperiment(
    experiment,
    clientSocket,
    serverAddress,
    csvLogger,
    pingSequence);

    // -------------------------
    // Original PР1 test
    // -------------------------

    for (uint16_t sequence = 1;
         sequence <= 10;
         ++sequence)
    {
        bool sendMovement =
            (sequence % 2 == 1);

        PacketHeader header{};

        header.type =
            static_cast<uint8_t>(
                sendMovement
                    ? PacketType::Movement
                    : PacketType::Shoot);

        header.sequence =
            htons(sequence);

        if (sendMovement)
        {
            header.payloadSize =
                htons(sizeof(MovementPayload));
        }
        else
        {
            header.payloadSize =
                htons(sizeof(ShootPayload));
        }

        header.version =
            htons(kProtocolVersion);

        char buffer[1024];
        int packetSize = 0;

        if (sendMovement)
        {
            MovementPayload movement{};

            movement.x =
                10.0f + sequence;

            movement.y = 20.0f;
            movement.z = 0.0f;

            std::memcpy(
                buffer,
                &header,
                sizeof(PacketHeader));

            uint32_t xNetwork =
                floatToNetwork(movement.x);

            uint32_t yNetwork =
                floatToNetwork(movement.y);

            uint32_t zNetwork =
                floatToNetwork(movement.z);

            std::memcpy(
                buffer + sizeof(PacketHeader),
                &xNetwork,
                sizeof(uint32_t));

            std::memcpy(
                buffer +
                    sizeof(PacketHeader) +
                    sizeof(uint32_t),
                &yNetwork,
                sizeof(uint32_t));

            std::memcpy(
                buffer +
                    sizeof(PacketHeader) +
                    sizeof(uint32_t) * 2,
                &zNetwork,
                sizeof(uint32_t));

            packetSize =
                sizeof(PacketHeader) +
                sizeof(MovementPayload);

            std::cout
                << "Sending MOVEMENT, sequence="
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

            std::cout
                << "Sending SHOOT, sequence="
                << sequence
                << '\n';
        }

        int sentBytes = sendto(
            clientSocket,
            buffer,
            packetSize,
            0,
            reinterpret_cast<sockaddr*>(
                &serverAddress),
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
            reinterpret_cast<sockaddr*>(
                &responseAddress),
            &responseAddressSize);

        if (receivedBytes == SOCKET_ERROR)
        {
            std::cerr << "recvfrom failed\n";
            break;
        }

        if (receivedBytes >=
            static_cast<int>(
                sizeof(PacketHeader)))
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

            std::cout
                << "Received response: "
                << "type="
                << static_cast<int>(
                    responseHeader.type)
                << ", sequence="
                << responseSequence
                << ", payloadSize="
                << responsePayloadSize
                << '\n';

            if (responseHeader.type ==
                static_cast<uint8_t>(
                    PacketType::State))
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
                        responseBuffer +
                            sizeof(PacketHeader),
                        sizeof(uint16_t));

                    std::memcpy(
                        &xNetwork,
                        responseBuffer +
                            sizeof(PacketHeader) +
                            sizeof(uint16_t),
                        sizeof(uint32_t));

                    std::memcpy(
                        &yNetwork,
                        responseBuffer +
                            sizeof(PacketHeader) +
                            sizeof(uint16_t) +
                            sizeof(uint32_t),
                        sizeof(uint32_t));

                    std::memcpy(
                        &zNetwork,
                        responseBuffer +
                            sizeof(PacketHeader) +
                            sizeof(uint16_t) +
                            sizeof(uint32_t) * 2,
                        sizeof(uint32_t));

                    uint16_t playerId =
                        ntohs(playerIdNetwork);

                    float x =
                        floatFromNetwork(xNetwork);

                    float y =
                        floatFromNetwork(yNetwork);

                    float z =
                        floatFromNetwork(zNetwork);

                    std::cout
                        << "State: "
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
                     static_cast<uint8_t>(
                         PacketType::Ack))
            {
                std::cout
                    << "ACK received\n";
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
