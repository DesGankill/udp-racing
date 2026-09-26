#include "csv_logger.h"
#include <filesystem>
#include <iostream>

const char* StatusToString(
    ResponseStatus status)
{
    switch (status)
    {
        case ResponseStatus::Received:
            return "received";

        case ResponseStatus::Timeout:
            return "timeout";

        case ResponseStatus::Late:
            return "late";

        case ResponseStatus::Duplicate:
            return "duplicate";

        case ResponseStatus::Unknown:
            return "unknown";
    }

    return "unknown";
}

CsvLogger::CsvLogger(
    const std::string& fileName)
    : file_(fileName, std::ios::app)
{
    if (!file_.is_open())
    {
        std::cerr << "Failed to open CSV file: "
                  << fileName << '\n';
        return;
    }

    std::cout << "CSV file opened: "
              << fileName << '\n';

    if (std::filesystem::file_size(fileName) == 0)
{
    file_
        << "experiment_id;"
        << "sample;"
        << "sequence;"
        << "sent_at_ms;"
        << "rtt_ms;"
        << "srtt_ms;"
        << "status\n";
}
}

CsvLogger::~CsvLogger()
{
    file_.close();
}

void CsvLogger::Write(
    const CsvRecord& record)
{
    file_
        << record.experimentId << ';'
        << record.sample << ';'
        << record.sequence << ';'
        << record.sentAtMs << ';'
        << record.rttMs << ';'
        << record.srttMs << ';'
        << StatusToString(record.status)
        << '\n';
}