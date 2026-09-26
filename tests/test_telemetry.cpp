#include <cassert>
#include <cstdint>
#include <iostream>

#include "../src/telemetry.h"

int main()
{
    {
        Telemetry telemetry;

        telemetry.OnPingSent(
            1,
            1'000'000ULL);

        const auto status =
            telemetry.OnPong(
                1,
                1'050'000ULL);

        assert(status == ResponseStatus::Received);
        assert(telemetry.SentCount() == 1);
        assert(telemetry.ReceivedCount() == 1);
        assert(telemetry.TimeoutCount() == 0);
        assert(telemetry.LateCount() == 0);

        assert(telemetry.SrttMs() == 50.0);
        assert(telemetry.MeanRttMs() == 50.0);
    }

    {
        Telemetry telemetry;

        telemetry.OnPingSent(
            1,
            1'000'000ULL);

        telemetry.OnPong(
            1,
            1'100'000ULL);

        telemetry.OnPingSent(
            2,
            2'000'000ULL);

        telemetry.OnPong(
            2,
            2'200'000ULL);

        const double expectedSrtt =
            0.875 * 100.0 +
            0.125 * 200.0;

        assert(
            telemetry.SrttMs() == expectedSrtt);

        assert(telemetry.ReceivedCount() == 2);
    }

    {
        Telemetry telemetry;

        telemetry.OnPingSent(
            1,
            1'000'000ULL);

        const auto expired =
            telemetry.Expire(
                2'100'000ULL);

        assert(expired == 1);
        assert(telemetry.TimeoutCount() == 1);
        assert(telemetry.ReceivedCount() == 0);
    }

    {
        Telemetry telemetry;

        telemetry.OnPingSent(
            1,
            1'000'000ULL);

        telemetry.Expire(
            2'100'000ULL);

        const auto status =
            telemetry.OnPong(
                1,
                2'200'000ULL);

        assert(status == ResponseStatus::Late);
        assert(telemetry.TimeoutCount() == 1);
        assert(telemetry.LateCount() == 1);
        assert(telemetry.ReceivedCount() == 0);
    }

    std::cout << "Telemetry tests passed\n";

    return 0;
}