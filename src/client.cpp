#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <winsock2.h>

#include "experiment_profiles.hpp"
#include "protocol.h"
#include "reliable_client.hpp"
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
    const std::string& seriesId,
    int timeoutMs = 1000);

PingResult SendPingAndWait(
    SOCKET clientSocket,
    const sockaddr_in& serverAddress,
    Telemetry& telemetry,
    std::uint16_t sequence,
    const std::string& seriesId,
    int timeoutMs)
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
        std::chrono::milliseconds(timeoutMs);

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

int RunReliableExperiment(
    SOCKET clientSocket,
    const sockaddr_in& serverAddress,
    const std::string& experimentId,
    int samples)
{
    ExperimentProfile profile = ReliableProfile(experimentId);
    if (samples > 0)
    {
        profile.samples = samples;
    }

    std::cout
        << "\n=== Reliable experiment: "
        << profile.id
        << " ===\n";
    std::cout
        << "samples="
        << profile.samples
        << ", lossPercent="
        << profile.lossPercent
        << ", delayMs="
        << profile.delayMs
        << ", jitterMs="
        << profile.jitterMs
        << ", maxAttempts="
        << profile.maxAttempts
        << ", seed="
        << profile.seed
        << '\n';

    AdaptiveTimeout adaptiveTimeout;
    Telemetry telemetry;
    std::uint16_t pingSequence = 1000;

    for (int i = 0; i < 5; ++i)
    {
        const PingResult result = SendPingAndWait(
            clientSocket,
            serverAddress,
            telemetry,
            pingSequence,
            profile.id,
            500);

        if (result.status == ResponseStatus::Received)
        {
            adaptiveTimeout.OnSample(result.rttMs);
        }

        ++pingSequence;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }

    std::cout
        << "Warmup RTO: "
        << adaptiveTimeout.RtoMs()
        << " ms\n";

    ReliableClient reliableClient(
        clientSocket,
        serverAddress,
        adaptiveTimeout);

    const std::string csvPath = "docs/reliability_samples.csv";
    std::filesystem::create_directories("docs");

    const bool writeHeader =
        !std::filesystem::exists(csvPath) ||
        std::filesystem::file_size(csvPath) == 0;

    std::ofstream csv(csvPath, std::ios::app);
    if (!csv.is_open())
    {
        std::cerr << "Failed to open " << csvPath << '\n';
        return 1;
    }

    if (writeHeader)
    {
        csv
            << "series_id;sample;sequence;attempts;retransmissions;"
            << "delivered_first_try;failed;time_to_ack_ms;rto_ms;"
            << "loss_percent;delay_ms;jitter_ms;max_attempts;seed;status\n";
    }

    std::size_t deliveredFirstTry = 0;
    std::size_t failed = 0;
    std::size_t retransmissionTotal = 0;
    double attemptSum = 0.0;
    double ackTimeSum = 0.0;
    std::size_t ackTimeCount = 0;

    std::uint16_t sequence = 1;

    for (int sample = 1; sample <= profile.samples; ++sample)
    {
        const ShootDelivery delivery =
            reliableClient.SendShoot(sequence, 1);

        if (delivery.deliveredFirstTry)
        {
            ++deliveredFirstTry;
        }

        if (delivery.failed)
        {
            ++failed;
        }

        retransmissionTotal += delivery.retransmissions;
        attemptSum += static_cast<double>(delivery.attempts);

        if (delivery.delivered)
        {
            ackTimeSum += delivery.timeToAckMs;
            ++ackTimeCount;
        }

        csv
            << profile.id << ';'
            << sample << ';'
            << delivery.sequence << ';'
            << delivery.attempts << ';'
            << delivery.retransmissions << ';'
            << (delivery.deliveredFirstTry ? 1 : 0) << ';'
            << (delivery.failed ? 1 : 0) << ';';

        if (delivery.delivered)
        {
            csv << std::fixed << std::setprecision(3)
                << delivery.timeToAckMs;
        }

        csv
            << ';'
            << std::fixed << std::setprecision(3)
            << delivery.rtoAfterMs << ';'
            << profile.lossPercent << ';'
            << profile.delayMs << ';'
            << profile.jitterMs << ';'
            << profile.maxAttempts << ';'
            << profile.seed << ';'
            << (delivery.failed ? "failed" : "acked")
            << '\n';

        ++sequence;

        std::this_thread::sleep_for(
            std::chrono::milliseconds(20));
    }

    const double avgAttempts =
        attemptSum / static_cast<double>(profile.samples);
    const double avgAckMs =
        ackTimeCount == 0
            ? 0.0
            : ackTimeSum / static_cast<double>(ackTimeCount);
    const double failedShare =
        100.0 * static_cast<double>(failed) /
        static_cast<double>(profile.samples);

    std::cout
        << "SUMMARY series="
        << profile.id
        << " sent="
        << profile.samples
        << " first="
        << deliveredFirstTry
        << " retrans="
        << retransmissionTotal
        << " failed="
        << failed
        << " failed_pct="
        << std::fixed << std::setprecision(2)
        << failedShare
        << " avg_attempts="
        << std::setprecision(3)
        << avgAttempts
        << " avg_ack_ms="
        << avgAckMs
        << " final_rto_ms="
        << adaptiveTimeout.RtoMs()
        << '\n';

    return 0;
}

int main(int argc, char* argv[])
{
    std::string mode = "ping";
    std::string experimentId = "baseline";
    int reliableSamples = 50;

    if (argc >= 2)
    {
        const std::string argument = argv[1];
        if (argument == "demo" ||
            argument == "reliable" ||
            argument == "ping")
        {
            mode = argument;
            if (argc >= 3)
            {
                experimentId = argv[2];
            }
        }
        else
        {
            experimentId = argument;
        }
    }

    if (argc >= 4 && mode == "reliable")
    {
        reliableSamples = std::stoi(argv[3]);
    }

    const ExperimentConfig& experiment =
        GetExperimentConfig(experimentId);

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

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr =
        inet_addr("127.0.0.1");
    serverAddress.sin_port = htons(54000);

    if (mode == "reliable")
    {
        const int code = RunReliableExperiment(
            clientSocket,
            serverAddress,
            experimentId,
            reliableSamples);

        closesocket(clientSocket);
        WSACleanup();
        return code;
    }

    if (mode != "demo")
    {
        std::cout
            << "Experiment: "
            << experiment.id
            << '\n';

        Telemetry telemetry;
        CsvLogger csvLogger("results.csv");
        std::uint16_t pingSequence = 100;

        RunExperiment(
            experiment,
            clientSocket,
            serverAddress,
            csvLogger,
            pingSequence);
    }

    AdaptiveTimeout demoTimeout;
    ReliableClient demoClient(
        clientSocket,
        serverAddress,
        demoTimeout);

    for (uint16_t sequence = 1;
         sequence <= 10;
         ++sequence)
    {
        const bool sendMovement =
            (sequence % 2 == 1);

        if (!sendMovement)
        {
            demoClient.SendShoot(sequence, 1);

            std::cout << '\n';
            std::this_thread::sleep_for(
                std::chrono::seconds(1));
            continue;
        }

        const std::vector<std::uint8_t> packet =
            SerializeMovement(
                sequence,
                10.0f + static_cast<float>(sequence),
                20.0f,
                0.0f,
                kAckNotRequired);

        std::cout
            << "Sending MOVEMENT, sequence="
            << sequence
            << '\n';

        int sentBytes = sendto(
            clientSocket,
            reinterpret_cast<const char*>(packet.data()),
            static_cast<int>(packet.size()),
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

        const auto* responseBytes =
            reinterpret_cast<const std::uint8_t*>(
                responseBuffer);

        const auto responseHeader = ParseHeader(
            responseBytes,
            static_cast<std::size_t>(receivedBytes));

        if (!responseHeader)
        {
            std::cerr << "Invalid response packet\n";
        }
        else
        {
            std::cout
                << "Received response: "
                << "type="
                << static_cast<int>(responseHeader->type)
                << ", sequence="
                << responseHeader->sequence
                << ", payloadSize="
                << responseHeader->payloadSize
                << ", requiresAck="
                << static_cast<int>(
                       responseHeader->requiresAck)
                << '\n';

            if (responseHeader->type ==
                static_cast<uint8_t>(PacketType::State))
            {
                const auto state = ParseState(
                    responseBytes,
                    static_cast<std::size_t>(receivedBytes));

                if (!state)
                {
                    std::cerr << "Invalid STATE packet\n";
                }
                else
                {
                    std::cout
                        << "State: "
                        << "playerId="
                        << state->playerId
                        << ", x="
                        << state->x
                        << ", y="
                        << state->y
                        << ", z="
                        << state->z
                        << '\n';
                }
            }
            else if (responseHeader->type ==
                     static_cast<uint8_t>(PacketType::Ack))
            {
                const auto ack = ParseAck(
                    responseBytes,
                    static_cast<std::size_t>(receivedBytes));

                if (!ack)
                {
                    std::cerr << "Invalid ACK packet\n";
                }
                else
                {
                    std::cout
                        << "ACK received, acknowledgedSequence="
                        << ack->acknowledgedSequence
                        << '\n';
                }
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
