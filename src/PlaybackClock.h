#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

namespace notebot {

class PlaybackClock {
public:
    using Clock = std::chrono::steady_clock;

    void reset() {
        mNextTick.reset();
        mNextWorldTick.reset();
    }

    static double intervalTicks(float speed) {
        const double valid = std::isfinite(speed) ? std::clamp(static_cast<double>(speed), 0.02, 2.0) : 1.0;
        // Match the UI's two decimal places, including at the 0.02 boundary.
        return 1.0 / (std::round(valid * 100.0) / 100.0);
    }

    int advance(Clock::time_point now, float speed) {
        const auto interval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(0.05 * intervalTicks(speed)));
        if (interval != mInterval) {
            reset();
            mInterval = interval;
        }
        if (mNextTick && now < *mNextTick) return 0;

        // Preserve timing across small jitter, but discard a stalled client's backlog.
        if (!mNextTick || now - *mNextTick >= std::chrono::milliseconds(200))
            mNextTick = now;
        int ticks = 0;
        while (now >= *mNextTick && ticks < 4) {
            *mNextTick += interval;
            ++ticks;
        }
        if (now >= *mNextTick) mNextTick = now + interval;
        return ticks;
    }

    int advanceWorld(uint64_t tick, float speed) {
        const double interval = intervalTicks(speed);
        const double now = static_cast<double>(tick);
        if (!mNextWorldTick || interval != mWorldInterval ||
            now - *mNextWorldTick >= std::max(4.0, interval)) {
            mNextWorldTick = now;
            mWorldInterval = interval;
        }
        int ticks = 0;
        while (now + 1e-7 >= *mNextWorldTick && ticks < 4) {
            *mNextWorldTick += interval;
            ++ticks;
        }
        return ticks;
    }

private:
    std::optional<Clock::time_point> mNextTick;
    Clock::duration mInterval{std::chrono::milliseconds(50)};
    std::optional<double> mNextWorldTick;
    double mWorldInterval = 1.0;
};

} // namespace notebot
