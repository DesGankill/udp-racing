#include <cstdlib>
#include <iostream>

#include "../src/reliability/dedup_window.hpp"
#include "../src/server_session.hpp"

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

} // namespace

int main()
{
    {
        DedupWindow window(8);
        EXPECT(!window.Observe(1));
        EXPECT(window.Observe(1));
        EXPECT(!window.Observe(2));
        EXPECT(window.Size() == 2);
    }

    {
        DedupWindow window(4);
        EXPECT(!window.Observe(1));
        EXPECT(!window.Observe(2));
        EXPECT(!window.Observe(3));
        EXPECT(!window.Observe(4));
        EXPECT(!window.Observe(5));
        EXPECT(window.Observe(1));
        EXPECT(window.Observe(2));
        EXPECT(!window.Observe(6));
    }

    {
        DedupWindow window(16);
        EXPECT(!window.Observe(65535));
        EXPECT(!window.Observe(0));
        EXPECT(!window.Observe(1));
        EXPECT(window.Observe(65535));
        EXPECT(window.Observe(0));
        EXPECT(!window.Observe(65530));
        EXPECT(window.Observe(65530));
    }

    {
        ServerSession session(4);
        EXPECT(session.OnShoot(10, 3, true).effectApplied);
        EXPECT(session.ShotsApplied() == 1);
        EXPECT(session.LastWeaponId() == 3);

        const ShootDecision duplicate = session.OnShoot(10, 9, true);
        EXPECT(duplicate.duplicate);
        EXPECT(!duplicate.effectApplied);
        EXPECT(duplicate.sendAck);
        EXPECT(duplicate.acknowledgedSequence == 10);
        EXPECT(session.ShotsApplied() == 1);
        EXPECT(session.LastWeaponId() == 3);

        EXPECT(session.OnShoot(11, 1, true).effectApplied);
        EXPECT(session.OnShoot(12, 1, true).effectApplied);
        EXPECT(session.OnShoot(13, 1, true).effectApplied);
        EXPECT(session.OnShoot(14, 1, true).effectApplied);
        EXPECT(session.OnShoot(10, 1, true).duplicate);
        EXPECT(session.ShotsApplied() == 5);
    }

    if (gFailures != 0)
    {
        std::cerr << gFailures << " dedup checks failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "Dedup tests passed\n";
    return EXIT_SUCCESS;
}
