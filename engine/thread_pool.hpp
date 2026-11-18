#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

class ThreadPool
{
  public:
    static constexpr std::size_t MIN_THREADS = 3;

    static ThreadPool &instance()
    {
        static ThreadPool pool;
        return pool;
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    template <typename Function, typename... ArgumentTypes> void enqueue(Function &&function, ArgumentTypes &&...args)
    {
        auto task = std::bind(std::forward<Function>(function), std::forward<ArgumentTypes>(args)...);

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Reserve an available worker if one exists.
            if (m_idle_workers > 0)
            {
                m_tasks.emplace(std::move(task));
                --m_idle_workers;

                m_condition.notify_one();
                return;
            }
        }

        // All pool workers are occupied.
        // Execute this task on a detached thread instead.
        std::thread(std::move(task)).detach();
    }

  private:
    ThreadPool() : m_idle_workers(MIN_THREADS)
    {
        m_workers.reserve(MIN_THREADS);

        for (std::size_t i = 0; i < MIN_THREADS; ++i)
        {
            m_workers.emplace_back(&ThreadPool::worker, this);
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }

        m_condition.notify_all();

        for (auto &worker : m_workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

    void worker()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(m_mutex);

                m_condition.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });

                if (m_stopping && m_tasks.empty())
                {
                    return;
                }

                task = std::move(m_tasks.front());
                m_tasks.pop();
            }

            task();

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                ++m_idle_workers;
            }
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;

    std::mutex m_mutex;
    std::condition_variable m_condition;

    std::size_t m_idle_workers = 0;
    bool m_stopping = false;
};
