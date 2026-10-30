#include "base/clock.hpp"

namespace {

const Clock::TimePoint kStart = Clock::now();

}

double Clock::seconds()
{
    return Clock::msSince(kStart) / 1000.0;
}
