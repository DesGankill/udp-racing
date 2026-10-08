#pragma once

#include <string>

#include "network_impairment.hpp"

struct ExperimentProfile
{
    std::string id = "baseline";
    int delayMs = 0;
    int jitterMs = 0;
    int lossPercent = 0;
    bool impairReliable = false;
    int samples = 50;
    int maxAttempts = 5;
    unsigned seed = 12345;
};

inline ExperimentProfile PingProfile(const std::string& experimentId)
{
    ExperimentProfile profile{};
    profile.id = experimentId;
    profile.impairReliable = false;

    if (experimentId == "delay_50")
    {
        profile.delayMs = 50;
    }
    else if (experimentId == "delay_100")
    {
        profile.delayMs = 100;
    }
    else if (experimentId == "jitter")
    {
        profile.id = "jitter";
        profile.delayMs = 50;
        profile.jitterMs = 20;
    }
    else if (experimentId == "loss_5")
    {
        profile.lossPercent = 5;
    }
    else if (experimentId == "combined")
    {
        profile.delayMs = 50;
        profile.jitterMs = 20;
        profile.lossPercent = 5;
    }
    else
    {
        profile.id = "baseline";
    }

    return profile;
}

inline ExperimentProfile ReliableProfile(const std::string& experimentId)
{
    ExperimentProfile profile{};
    profile.impairReliable = true;
    profile.samples = 50;
    profile.maxAttempts = 5;
    profile.seed = 12345;

    if (experimentId == "loss_5")
    {
        profile.id = "loss_5";
        profile.lossPercent = 5;
    }
    else if (experimentId == "loss_10")
    {
        profile.id = "loss_10";
        profile.lossPercent = 10;
    }
    else if (experimentId == "loss_20")
    {
        profile.id = "loss_20";
        profile.lossPercent = 20;
    }
    else if (experimentId == "delay_100_loss_5")
    {
        profile.id = "delay_100_loss_5";
        profile.delayMs = 100;
        profile.lossPercent = 5;
    }
    else if (experimentId == "jitter_loss_10")
    {
        profile.id = "jitter_loss_10";
        profile.delayMs = 50;
        profile.jitterMs = 20;
        profile.lossPercent = 10;
    }
    else
    {
        profile.id = "baseline";
    }

    return profile;
}

inline NetworkConditions ConditionsFromProfile(
    const ExperimentProfile& profile)
{
    NetworkConditions conditions{};
    conditions.delayMs = profile.delayMs;
    conditions.jitterMs = profile.jitterMs;
    conditions.lossPercent = profile.lossPercent;
    conditions.impairReliable = profile.impairReliable;
    conditions.seed = profile.seed;
    return conditions;
}
