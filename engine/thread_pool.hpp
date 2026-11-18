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

    template <typename Function, typename... Args> void enqueue(Function &&function, Args &&...args)
    {
        auto task = std::bind(std::forward<Function>(function), std::forward<Args>(args)...);

        {
            std::lock_guard<std::mutex> lock(mutex);

            // Reserve an available worker if one exists.
            if (idle_workers > 0)
            {
                tasks.emplace(std::move(task));
                --idle_workers;

                condition.notify_one();
                return;
            }
        }

        // All pool workers are occupied.
        // Execute this task on a detached thread instead.
        std::thread(std::move(task)).detach();
    }

  private:
    ThreadPool() : idle_workers(MIN_THREADS)
    {
        workers.reserve(MIN_THREADS);

        for (std::size_t i = 0; i < MIN_THREADS; ++i)
        {
            workers.emplace_back(&ThreadPool::worker, this);
        }
    }

    ~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }

        condition.notify_all();

        for (auto &worker : workers)
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
                std::unique_lock<std::mutex> lock(mutex);

                condition.wait(lock, [this] { return stopping || !tasks.empty(); });

                if (stopping && tasks.empty())
                {
                    return;
                }

                task = std::move(tasks.front());
                tasks.pop();
            }

            task();

            {
                std::lock_guard<std::mutex> lock(mutex);
                ++idle_workers;
            }
        }
    }

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex mutex;
    std::condition_variable condition;

    std::size_t idle_workers = 0;
    bool stopping = false;
};
