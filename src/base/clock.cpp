#include "base/clock.hpp"

namespace {

const Clock::TimePoint kStart = Clock::now();

}

Clock::Seconds Clock::sinceStart()
{
    return Clock::since(kStart);
}
