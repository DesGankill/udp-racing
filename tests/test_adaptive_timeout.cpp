#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "../src/reliability/adaptive_timeout.hpp"

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

#define EXPECT(condition) Expect((condition), #condition, __LINE__)

bool Near(double actual, double expected)
{
    return std::abs(actual - expected) < 1e-9;
}

} // namespace

int main()
{
    {
        AdaptiveTimeout timeout;
        EXPECT(!timeout.IsInitialized());
        EXPECT(timeout.RtoMs() == AdaptiveTimeout::kMinRtoMs);
        EXPECT(timeout.RtoUs() == 100'000);
    }

    {
        AdaptiveTimeout timeout;
        EXPECT(timeout.OnSample(200.0));
        EXPECT(timeout.IsInitialized());
        EXPECT(Near(timeout.SrttMs(), 200.0));
        EXPECT(Near(timeout.RttVarMs(), 100.0));
        EXPECT(Near(timeout.RtoMs(), 600.0));

        EXPECT(timeout.OnSample(100.0));

        const double expectedVar =
            (1.0 - AdaptiveTimeout::kBeta) * 100.0 +
            AdaptiveTimeout::kBeta * std::abs(200.0 - 100.0);
        const double expectedSrtt =
            (1.0 - AdaptiveTimeout::kAlpha) * 200.0 +
            AdaptiveTimeout::kAlpha * 100.0;

        EXPECT(Near(timeout.RttVarMs(), expectedVar));
        EXPECT(Near(timeout.SrttMs(), expectedSrtt));
        EXPECT(Near(
            timeout.RtoMs(),
            expectedSrtt + 4.0 * expectedVar));
    }

    {
        AdaptiveTimeout timeout;
        EXPECT(timeout.OnSample(1.0));
        EXPECT(timeout.RtoMs() == AdaptiveTimeout::kMinRtoMs);

        AdaptiveTimeout saturated;
        EXPECT(saturated.OnSample(10'000.0));
        EXPECT(saturated.RtoMs() == AdaptiveTimeout::kMaxRtoMs);
        EXPECT(saturated.RtoUs() == 3'000'000);
    }

    {
        AdaptiveTimeout timeout;
        EXPECT(timeout.OnSample(80.0));
        const double srtt = timeout.SrttMs();
        const double variation = timeout.RttVarMs();
        const double rto = timeout.RtoMs();

        EXPECT(!timeout.OnSample(-1.0));
        EXPECT(!timeout.OnSample(
            std::numeric_limits<double>::quiet_NaN()));
        EXPECT(!timeout.OnSample(
            std::numeric_limits<double>::infinity()));
        EXPECT(Near(timeout.SrttMs(), srtt));
        EXPECT(Near(timeout.RttVarMs(), variation));
        EXPECT(Near(timeout.RtoMs(), rto));
        EXPECT(timeout.IsInitialized());
    }

    {
        AdaptiveTimeout timeout;
        EXPECT(!timeout.OnSample(-5.0));
        EXPECT(!timeout.IsInitialized());
        EXPECT(timeout.RtoMs() == AdaptiveTimeout::kMinRtoMs);
        EXPECT(timeout.OnSample(0.0));
        EXPECT(Near(timeout.SrttMs(), 0.0));
        EXPECT(timeout.RtoMs() == AdaptiveTimeout::kMinRtoMs);
    }

    if (gFailures != 0)
    {
        std::cerr << gFailures << " adaptive-timeout checks failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Adaptive timeout tests passed\n";
    return EXIT_SUCCESS;
}
