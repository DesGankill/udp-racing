#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "telemetry.h"

struct CsvRecord
{
    std::string experimentId;
    std::size_t sample = 0;
    std::uint16_t sequence = 0;
    std::uint64_t sentAtMs = 0;
    double rttMs = 0.0;
    double srttMs = 0.0;
    ResponseStatus status = ResponseStatus::Unknown;
};

class CsvLogger
{
public:
    explicit CsvLogger(
        const std::string& fileName);

    ~CsvLogger();

    void Write(
        const CsvRecord& record);

private:
    std::ofstream file_;
};