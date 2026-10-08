#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <random>
#include <thread>

struct NetworkConditions
{
    int delayMs = 0;
    int jitterMs = 0;
    int lossPercent = 0;
    bool impairReliable = false;
    unsigned seed = 12345;
};

// Applies loss, delay and jitter to real datagram handling.
// Incoming and outgoing loss draws are independent.
// Forced drops are deterministic and are used by tests; they do not
// consume the probabilistic generator.
class NetworkImpairment
{
public:
    explicit NetworkImpairment(const NetworkConditions& conditions)
        : conditions_(conditions),
          lossRng_(conditions.seed),
          jitterRng_(conditions.seed)
    {
    }

    const NetworkConditions& Conditions() const
    {
        return conditions_;
    }

    void ForceIncomingDrops(int count)
    {
        forcedIncoming_ = count;
    }

    void ForceOutgoingDrops(int count)
    {
        forcedOutgoing_ = count;
    }

    bool DropIncoming(
        std::uint16_t sequence,
        const char* label)
    {
        if (forcedIncoming_ > 0)
        {
            --forcedIncoming_;
            std::cout
                << label
                << " dropped: sequence="
                << sequence
                << '\n';
            return true;
        }

        if (conditions_.lossPercent <= 0)
        {
            return false;
        }

        std::uniform_int_distribution<int> distribution(1, 100);
        if (distribution(lossRng_) <= conditions_.lossPercent)
        {
            std::cout
                << label
                << " dropped: sequence="
                << sequence
                << '\n';
            return true;
        }

        return false;
    }

    bool DropOutgoing(
        std::uint16_t sequence,
        const char* label)
    {
        if (forcedOutgoing_ > 0)
        {
            --forcedOutgoing_;
            std::cout
                << label
                << " dropped: sequence="
                << sequence
                << '\n';
            return true;
        }

        if (conditions_.lossPercent <= 0)
        {
            return false;
        }

        std::uniform_int_distribution<int> distribution(1, 100);
        if (distribution(lossRng_) <= conditions_.lossPercent)
        {
            std::cout
                << label
                << " dropped: sequence="
                << sequence
                << '\n';
            return true;
        }

        return false;
    }

    void ApplyDelay()
    {
        int totalDelayMs = conditions_.delayMs;

        if (conditions_.jitterMs > 0)
        {
            std::uniform_int_distribution<int> distribution(
                -conditions_.jitterMs,
                conditions_.jitterMs);

            totalDelayMs += distribution(jitterRng_);
            if (totalDelayMs < 0)
            {
                totalDelayMs = 0;
            }
        }

        std::cout
            << "Applying network delay: "
            << totalDelayMs
            << " ms\n";

        if (totalDelayMs <= 0)
        {
            return;
        }

        const auto delayStart = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(totalDelayMs));
        const auto delayEnd = std::chrono::steady_clock::now();

        const auto actualDelayUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                delayEnd - delayStart)
                .count();

        std::cout
            << "Requested delay: "
            << totalDelayMs
            << " ms, actual delay: "
            << actualDelayUs / 1000.0
            << " ms\n";
    }

private:
    NetworkConditions conditions_;
    std::mt19937 lossRng_;
    std::mt19937 jitterRng_;
    int forcedIncoming_ = 0;
    int forcedOutgoing_ = 0;
};
