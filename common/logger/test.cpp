#include "./logger.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

using namespace logging;

// ==================================================
// TEST RUNNER
// ==================================================

class TestRunner
{
  public:
    template <typename Func> void run(const std::string &name, Func test)
    {
        std::cout << "[ RUN      ] " << name << '\n';

        try
        {
            test();

            ++m_passed;

            std::cout << "[       OK ] " << name << '\n';
        }
        catch (const std::exception &e)
        {
            ++m_failed;

            std::cerr << "[  FAILED  ] " << name << ": " << e.what() << '\n';
        }
    }

    void assert(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void summary() const
    {
        std::cout << "\n============================\n"
                  << "Tests passed: " << m_passed << '\n'
                  << "Tests failed: " << m_failed << '\n'
                  << "============================\n";
    }

    int result() const
    {
        return m_failed == 0 ? 0 : 1;
    }

  private:
    int m_passed = 0;
    int m_failed = 0;
};

// ==================================================
// TEST SINK
// ==================================================

class TestSink final : public LogSink
{
  public:
    void write(const LogMessage &message) override
    {
        m_messages.push_back(message);
    }

    void flush() override
    {
        ++m_flushCount;
    }

    std::size_t count() const
    {
        return m_messages.size();
    }

    int flush_count() const
    {
        return m_flushCount;
    }

    const std::vector<LogMessage> &messages() const
    {
        return m_messages;
    }

  private:
    // Accessed exclusively by the worker thread.
    std::vector<LogMessage> m_messages;
    int m_flushCount = 0;
};

// ==================================================
// SLOW SINK
// ==================================================

class SlowSink final : public LogSink
{
  public:
    void write(const LogMessage &) override
    {
        std::this_thread::sleep_for(2ms);
        ++m_count;
    }

    void flush() override
    {
    }

    int count() const
    {
        return m_count.load();
    }

  private:
    std::atomic<int> m_count{0};
};

// ==================================================
// THROWING SINK
// ==================================================

class ThrowingSink final : public LogSink
{
  public:
    void write(const LogMessage &) override
    {
        throw std::runtime_error("Simulated sink failure");
    }

    void flush() override
    {
    }
};

// ==================================================
// TESTS
// ==================================================

int main()
{
    TestRunner tests;

    // --------------------------------------------------
    // 1. Independent logger instances
    // --------------------------------------------------

    tests.run("Independent logger instances", [&] {
        Logger logger_a;
        Logger logger_b;

        auto sink_a = std::make_unique<TestSink>();
        auto sink_b = std::make_unique<TestSink>();

        auto *ptr_a = sink_a.get();
        auto *ptr_b = sink_b.get();

        logger_a.add_sink(std::move(sink_a));
        logger_b.add_sink(std::move(sink_b));

        logger_a.info("Message A");
        logger_b.info("Message B");

        logger_a.shutdown();
        logger_b.shutdown();

        tests.assert(ptr_a->count() == 1, "Logger A received an incorrect message count");

        tests.assert(ptr_b->count() == 1, "Logger B received an incorrect message count");

        tests.assert(ptr_a->messages()[0].message == "Message A", "Logger A received an incorrect message");

        tests.assert(ptr_b->messages()[0].message == "Message B", "Logger B received an incorrect message");
    });

    // --------------------------------------------------
    // 2. Concurrent producers
    // --------------------------------------------------

    tests.run("Concurrent producers", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        constexpr int ThreadCount = 10;
        constexpr int MessagesPerThread = 1000;

        std::vector<std::jthread> threads;

        for (int i = 0; i < ThreadCount; ++i)
        {
            threads.emplace_back([i, &logger] {
                for (int j = 0; j < MessagesPerThread; ++j)
                {
                    logger.info("Thread " + std::to_string(i) + " message " + std::to_string(j));
                }
            });
        }

        // Wait for every producer.
        threads.clear();

        logger.shutdown();

        tests.assert(sink_ptr->count() == ThreadCount * MessagesPerThread, "Some messages were lost");

        tests.assert(sink_ptr->flush_count() == 1, "Sink was not flushed exactly once");
    });

    // --------------------------------------------------
    // 3. Shutdown before startup
    // --------------------------------------------------

    tests.run("Shutdown before startup", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        logger.shutdown();

        logger.info("Ignored message");
        logger.warning("Ignored message");
        logger.error("Ignored message");

        tests.assert(sink_ptr->count() == 0, "Messages were accepted after shutdown");

        tests.assert(sink_ptr->flush_count() == 0, "Sink should not have been flushed without startup");
    });

    // --------------------------------------------------
    // 4. Logging after shutdown
    // --------------------------------------------------

    tests.run("Logging after shutdown", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        logger.info("Accepted message");

        logger.shutdown();

        const auto CountAfterShutdown = sink_ptr->count();

        logger.info("Ignored");
        logger.warning("Ignored");
        logger.error("Ignored");

        tests.assert(sink_ptr->count() == CountAfterShutdown, "Messages were accepted after shutdown");

        tests.assert(sink_ptr->count() == 1, "Accepted message was not delivered");
    });

    // --------------------------------------------------
    // 5. Repeated shutdown
    // --------------------------------------------------

    tests.run("Repeated shutdown", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        logger.info("Test message");

        logger.shutdown();
        logger.shutdown();
        logger.shutdown();

        tests.assert(sink_ptr->count() == 1, "Unexpected message count");

        tests.assert(sink_ptr->flush_count() == 1, "Sink was flushed more than once");
    });

    // --------------------------------------------------
    // 6. Concurrent shutdown
    // --------------------------------------------------

    tests.run("Concurrent shutdown", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        constexpr int MessageCount = 1000;

        for (int i = 0; i < MessageCount; ++i)
        {
            logger.info("Message " + std::to_string(i));
        }

        std::vector<std::jthread> threads;

        for (int i = 0; i < 10; ++i)
        {
            threads.emplace_back([&logger] { logger.shutdown(); });
        }

        threads.clear();

        logger.shutdown();

        tests.assert(sink_ptr->count() == MessageCount, "Messages were lost during concurrent shutdown");

        tests.assert(sink_ptr->flush_count() == 1, "Sink was flushed more than once");
    });

    // --------------------------------------------------
    // 7. Shutdown racing producers
    // --------------------------------------------------

    tests.run("Shutdown racing producers", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        constexpr int ThreadCount = 8;
        constexpr int MessagesPerThread = 10000;

        std::atomic<int> ready{0};
        std::atomic<bool> start{false};

        std::vector<std::jthread> threads;

        for (int i = 0; i < ThreadCount; ++i)
        {
            threads.emplace_back([i, &logger, &ready, &start] {
                ++ready;

                while (!start.load())
                    std::this_thread::yield();

                for (int j = 0; j < MessagesPerThread; ++j)
                {
                    logger.info("Thread " + std::to_string(i) + " message " + std::to_string(j));
                }
            });
        }

        // Wait until all producers are ready.
        while (ready.load() != ThreadCount)
            std::this_thread::yield();

        start = true;

        // Race shutdown against active producers.
        logger.shutdown();

        // Producers may finish their remaining calls.
        // Those calls must be silently rejected.
        threads.clear();

        const auto AcceptedCount = sink_ptr->count();

        logger.info("Must be ignored");

        tests.assert(sink_ptr->count() == AcceptedCount, "Messages were accepted after shutdown completed");

        tests.assert(AcceptedCount <= ThreadCount * MessagesPerThread, "Invalid message count");

        tests.assert(sink_ptr->flush_count() == (AcceptedCount > 0 ? 1 : 0), "Unexpected sink flush count");
    });

    // --------------------------------------------------
    // 8. Slow sink and queue draining
    // --------------------------------------------------

    tests.run("Slow sink drains queue", [&] {
        Logger logger;

        auto sink = std::make_unique<SlowSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        constexpr int MessageCount = 50;

        for (int i = 0; i < MessageCount; ++i)
        {
            logger.info("Slow message");
        }

        logger.shutdown();

        tests.assert(sink_ptr->count() == MessageCount, "Shutdown did not drain the queue");
    });

    // --------------------------------------------------
    // 9. Throwing sink isolation
    // --------------------------------------------------

    tests.run("Throwing sink isolation", [&] {
        Logger logger;

        auto throwing_sink = std::make_unique<ThrowingSink>();
        auto recording_sink = std::make_unique<TestSink>();

        auto *recording_ptr = recording_sink.get();

        logger.add_sink(std::move(throwing_sink));
        logger.add_sink(std::move(recording_sink));

        constexpr int MessageCount = 100;

        for (int i = 0; i < MessageCount; ++i)
        {
            logger.info("Test message");
        }

        logger.shutdown();

        tests.assert(recording_ptr->count() == MessageCount,
                     "Throwing sink prevented other sinks from receiving messages");
    });

    // --------------------------------------------------
    // 10. Global logger accessor
    // --------------------------------------------------

    tests.run("Global logger accessor", [&] {
        auto &first = GlobalLogger::instance();
        auto &second = GlobalLogger::instance();

        tests.assert(&first == &second, "GlobalLogger returned different instances");
    });

    tests.run("Source location", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sink_ptr = sink.get();

        logger.add_sink(std::move(sink));

        const auto ExpectedLine = std::source_location::current().line() + 2;

        logger.info("Source location test");

        logger.shutdown();

        tests.assert(sink_ptr->count() == 1, "Expected one message");

        const auto &message = sink_ptr->messages()[0];

        tests.assert(message.location.line() == ExpectedLine, "Incorrect source line");

        tests.assert(std::string(message.location.file_name()).find("./common/logger/test.cpp") != std::string::npos,
                     "Incorrect source file");
    });

    // --------------------------------------------------
    // RESULTS
    // --------------------------------------------------
    // std::cout << "hello " << std::source_location::current().file_name() << std::endl;
    tests.summary();

    return tests.result();
}
