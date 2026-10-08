#include "reliability/adaptive_timeout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

bool AdaptiveTimeout::OnSample(double rttMs)
{
    if (!std::isfinite(rttMs) || rttMs < 0.0)
    {
        return false;
    }

    if (!initialized_)
    {
        srttMs_ = rttMs;
        rttVarMs_ = rttMs / 2.0;
        initialized_ = true;
        return true;
    }

    rttVarMs_ =
        (1.0 - kBeta) * rttVarMs_ +
        kBeta * std::abs(srttMs_ - rttMs);

    srttMs_ =
        (1.0 - kAlpha) * srttMs_ +
        kAlpha * rttMs;

    return true;
}

double AdaptiveTimeout::RtoMs() const
{
    const double raw = srttMs_ + 4.0 * rttVarMs_;
    return std::clamp(raw, kMinRtoMs, kMaxRtoMs);
}

std::uint64_t AdaptiveTimeout::RtoUs() const
{
    return static_cast<std::uint64_t>(
        std::llround(RtoMs() * 1000.0));
}

double AdaptiveTimeout::SrttMs() const
{
    return srttMs_;
}

double AdaptiveTimeout::RttVarMs() const
{
    return rttVarMs_;
}

bool AdaptiveTimeout::IsInitialized() const
{
    return initialized_;
}
