#pragma once

#include <cstdint>

// Jacobson's adaptive retransmission timeout.
//
// Time unit of the public sample and RtoMs() is milliseconds.
// RtoUs() returns the same value in microseconds for the delivery channel.
//
// First valid sample:
//   SRTT = RTT_sample
//   RTTVAR = RTT_sample / 2
//
// Later valid samples, in this order (RTTVAR uses the previous SRTT):
//   RTTVAR = (1 - beta) * RTTVAR + beta * abs(SRTT - RTT_sample)
//   SRTT = (1 - alpha) * SRTT + alpha * RTT_sample
//
//   RTO = clamp(SRTT + 4 * RTTVAR, 100 ms, 3000 ms)
//
// alpha = 0.125, beta = 0.25.
// Before the first sample, RTO is the minimum, 100 ms.
// Non-finite and negative samples are ignored and do not change the estimate.
class AdaptiveTimeout
{
public:
    static constexpr double kAlpha = 0.125;
    static constexpr double kBeta = 0.25;
    static constexpr double kMinRtoMs = 100.0;
    static constexpr double kMaxRtoMs = 3000.0;

    bool OnSample(double rttMs);

    double RtoMs() const;
    std::uint64_t RtoUs() const;

    double SrttMs() const;
    double RttVarMs() const;
    bool IsInitialized() const;

private:
    double srttMs_ = 0.0;
    double rttVarMs_ = 0.0;
    bool initialized_ = false;
};
