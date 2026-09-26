#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

enum class ResponseStatus
{
    Received,
    Timeout,
    Late,
    Duplicate,
    Unknown
};

struct Flight
{
    std::uint16_t sequence = 0;
    std::uint64_t sentUs = 0;
    std::string seriesId;
    ResponseStatus status = ResponseStatus::Unknown;
    std::uint32_t attempts = 1;
};

class Telemetry
{
public:
    void OnPingSent(
        std::uint16_t sequence,
        std::uint64_t nowUs,
        const std::string& seriesId = "default");

    ResponseStatus OnPong(
        std::uint16_t sequence,
        std::uint64_t nowUs);

    std::size_t Expire(
        std::uint64_t nowUs);

    double SrttMs() const;

    double MeanJitterMs() const;

    double MinRttMs() const;
    double MaxRttMs() const;
    double MeanRttMs() const;
    double MedianRttMs() const;
    double LossPercent() const;

    std::size_t SentCount() const;

    std::size_t ReceivedCount() const;

    std::size_t TimeoutCount() const;

    std::size_t LateCount() const;

private:
    static constexpr std::uint64_t kTimeoutUs =
        1'000'000;

    static constexpr std::size_t kMaxInFlight =
        256;

    std::unordered_map<
        std::uint16_t,
        Flight> inFlight_;

    std::deque<std::uint16_t> flightOrder_;

    double srttMs_ = 0.0;
    double previousRttMs_ = 0.0;
    double jitterSumMs_ = 0.0;

    std::size_t sentCount_ = 0;
    std::size_t receivedCount_ = 0;
    std::size_t timeoutCount_ = 0;
    std::size_t lateCount_ = 0;
    std::size_t samples_ = 0;

    bool hasPreviousRtt_ = false;
    std::vector<double> rttSamplesMs_;
};