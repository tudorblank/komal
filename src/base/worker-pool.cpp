#include "base/worker-pool.hpp"

#include <utility>

WorkerPool::~WorkerPool()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopping = true;
        m_jobs.clear();
    }
    m_wake.notify_all();
    for (std::thread& t : m_threads) t.join();
}

void WorkerPool::post(std::function<void()> job)
{
    {
        std::lock_guard lock(m_mutex);
        m_jobs.push_back(std::move(job));
        if (m_idle == 0 && m_threads.size() < m_maxThreads) {   // nobody free to take it: one more thread
            m_threads.emplace_back([this] { work(); });
            return;
        }
    }
    m_wake.notify_one();
}

void WorkerPool::work()
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        ++m_idle;
        m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
        --m_idle;
        if (m_stopping) return;
        std::function<void()> job = std::move(m_jobs.front());
        m_jobs.pop_front();
        lock.unlock();
        job();
        lock.lock();
    }
}
