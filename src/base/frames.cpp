#include "base/frames.hpp"

#include <atomic>

namespace {

std::atomic<MainLoopWaker> g_waker{ nullptr };

}

void installMainLoopWaker(MainLoopWaker waker) { g_waker.store(waker); }

void wakeMainLoop()
{
    if (const MainLoopWaker waker = g_waker.load()) waker();
}
