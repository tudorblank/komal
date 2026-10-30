#pragma once
// Background work on a few threads: jobs run in the order they were posted, at most `threads` at
// a time (a hundred files dropped at once decode a couple at a time, not all at once). Threads
// start as work comes; idle ones sleep. Destroying it drops what hasn't started and waits for
// what has.
//
//   WorkerPool pool(2);
//   pool.post([] { ... });   // from any thread

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class WorkerPool {
public:
    explicit WorkerPool(unsigned threads) : m_maxThreads(threads > 0 ? threads : 1) {}
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    void post(std::function<void()> job);

private:
    void work();

    const unsigned m_maxThreads;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::deque<std::function<void()>> m_jobs;   // guarded by m_mutex
    std::vector<std::thread> m_threads;
    unsigned m_idle = 0;                        // threads waiting for a job
    bool m_stopping = false;
};
