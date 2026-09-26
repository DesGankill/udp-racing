#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../src/protocol.h"

int main()
{
    {
        const std::uint16_t sequence = 42;
        const std::uint64_t sendTime = 123456789ULL;

        const auto buffer =
            SerializePing(sequence, sendTime);

        const auto parsed =
            ParsePing(buffer.data(), buffer.size());

        assert(parsed.has_value());
        assert(parsed->sequence == sequence);
        assert(parsed->clientSendTimeUs == sendTime);
    }

    {
        PongData pong{};
        pong.sequence = 77;
        pong.clientSendTimeUs = 1000ULL;
        pong.serverReceiveTimeUs = 2000ULL;
        pong.serverSendTimeUs = 3000ULL;

        const auto buffer =
            SerializePong(pong);

        const auto parsed =
            ParsePong(buffer.data(), buffer.size());

        assert(parsed.has_value());
        assert(parsed->sequence == 77);
        assert(parsed->clientSendTimeUs == 1000ULL);
        assert(parsed->serverReceiveTimeUs == 2000ULL);
        assert(parsed->serverSendTimeUs == 3000ULL);
    }

    {
        const auto buffer =
            SerializePing(1, 123456789ULL);

        auto invalidBuffer = buffer;

        invalidBuffer[3] = 0;
        invalidBuffer[4] = 7;

        const auto parsed =
            ParsePing(
                invalidBuffer.data(),
                invalidBuffer.size());

        assert(!parsed.has_value());
    }

    {
        const auto buffer =
            SerializePing(1, 123456789ULL);

        auto invalidBuffer = buffer;

        invalidBuffer[5] = 0;
        invalidBuffer[6] = 2;

        const auto parsed =
            ParsePing(
                invalidBuffer.data(),
                invalidBuffer.size());

        assert(!parsed.has_value());
    }

    std::cout << "Protocol tests passed\n";

    return 0;
}