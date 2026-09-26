#include "telemetry.h"

#include <algorithm>
#include <cmath>

void Telemetry::OnPingSent(
    std::uint16_t sequence,
    std::uint64_t nowUs,
    const std::string& seriesId)
{
    if (inFlight_.size() >= kMaxInFlight)
    {
        const std::uint16_t oldestSequence =
            flightOrder_.front();

        flightOrder_.pop_front();
        inFlight_.erase(oldestSequence);
    }

    Flight flight{};
    flight.sequence = sequence;
    flight.sentUs = nowUs;
    flight.seriesId = seriesId;
    flight.status = ResponseStatus::Unknown;
    flight.attempts = 1;

    inFlight_[sequence] = flight;
    flightOrder_.push_back(sequence);

    ++sentCount_;
}

ResponseStatus Telemetry::OnPong(
    std::uint16_t sequence,
    std::uint64_t nowUs)
{
    auto it = inFlight_.find(sequence);

    if (it == inFlight_.end())
    {
        return ResponseStatus::Unknown;
    }

    Flight& flight = it->second;

    if (flight.status == ResponseStatus::Timeout)
    {
        flight.status = ResponseStatus::Late;
        ++lateCount_;

        return ResponseStatus::Late;
    }

    if (flight.status == ResponseStatus::Received ||
        flight.status == ResponseStatus::Late)
    {
        return ResponseStatus::Duplicate;
    }

    const std::uint64_t elapsedUs =
        nowUs - flight.sentUs;

    const double rttMs =
        static_cast<double>(elapsedUs) / 1000.0;

    if (elapsedUs > kTimeoutUs)
    {
        flight.status = ResponseStatus::Late;
        ++lateCount_;

        return ResponseStatus::Late;
    }

    if (!hasPreviousRtt_)
    {
        srttMs_ = rttMs;
        hasPreviousRtt_ = true;
    }
    else
    {
        srttMs_ =
            0.875 * srttMs_ +
            0.125 * rttMs;

        jitterSumMs_ +=
            std::abs(rttMs - previousRttMs_);
    }

    previousRttMs_ = rttMs;

    rttSamplesMs_.push_back(rttMs);

    ++samples_;
    ++receivedCount_;

    flight.status = ResponseStatus::Received;

    return ResponseStatus::Received;
}

std::size_t Telemetry::Expire(
    std::uint64_t nowUs)
{
    std::size_t expiredCount = 0;

    for (auto& [sequence, flight] : inFlight_)
    {
        (void)sequence;

        if (flight.status == ResponseStatus::Unknown &&
            nowUs - flight.sentUs > kTimeoutUs)
        {
            flight.status = ResponseStatus::Timeout;

            ++timeoutCount_;
            ++expiredCount;
        }
    }

    return expiredCount;
}

double Telemetry::SrttMs() const
{
    return srttMs_;
}

double Telemetry::MeanJitterMs() const
{
    if (samples_ <= 1)
    {
        return 0.0;
    }

    return jitterSumMs_ /
           static_cast<double>(samples_ - 1);
}

std::size_t Telemetry::SentCount() const
{
    return sentCount_;
}

std::size_t Telemetry::ReceivedCount() const
{
    return receivedCount_;
}

std::size_t Telemetry::TimeoutCount() const
{
    return timeoutCount_;
}

std::size_t Telemetry::LateCount() const
{
    return lateCount_;
}
double Telemetry::MinRttMs() const
{
    if (rttSamplesMs_.empty())
    {
        return 0.0;
    }

    return *std::min_element(
        rttSamplesMs_.begin(),
        rttSamplesMs_.end());
}

double Telemetry::MaxRttMs() const
{
    if (rttSamplesMs_.empty())
    {
        return 0.0;
    }

    return *std::max_element(
        rttSamplesMs_.begin(),
        rttSamplesMs_.end());
}

double Telemetry::MeanRttMs() const
{
    if (rttSamplesMs_.empty())
    {
        return 0.0;
    }

    double sum = 0.0;

    for (const double rtt : rttSamplesMs_)
    {
        sum += rtt;
    }

    return sum /
           static_cast<double>(
               rttSamplesMs_.size());
}

double Telemetry::MedianRttMs() const
{
    if (rttSamplesMs_.empty())
    {
        return 0.0;
    }

    std::vector<double> sorted =
        rttSamplesMs_;

    std::sort(
        sorted.begin(),
        sorted.end());

    const std::size_t middle =
        sorted.size() / 2;

    if (sorted.size() % 2 == 0)
    {
        return (
            sorted[middle - 1] +
            sorted[middle]
        ) / 2.0;
    }

    return sorted[middle];
}

double Telemetry::LossPercent() const
{
    if (sentCount_ == 0)
    {
        return 0.0;
    }

    return
        static_cast<double>(timeoutCount_) /
        static_cast<double>(sentCount_) *
        100.0;
}