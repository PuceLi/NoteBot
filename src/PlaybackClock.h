#pragma once

#include <algorithm>
#include <chrono>
#include <optional>

namespace notebot {

class PlaybackClock {
public:
    using Clock = std::chrono::steady_clock;

    void reset() { mNextTick.reset(); }

    bool advance(Clock::time_point now, int tickDelay) {
        const auto interval = std::chrono::milliseconds(50 * (std::clamp(tickDelay, 0, 20) + 1));
        if (interval != mInterval) {
            reset();
            mInterval = interval;
        }
        if (mNextTick && now < *mNextTick) return false;

        // Preserve timing across small jitter, but discard a stalled client's backlog.
        mNextTick = mNextTick && now - *mNextTick < interval
            ? *mNextTick + interval : now + interval;
        return true;
    }

private:
    std::optional<Clock::time_point> mNextTick;
    std::chrono::milliseconds mInterval{50};
};

} // namespace notebot
