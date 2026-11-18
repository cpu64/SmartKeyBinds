#include "logger.hpp"
#include "sinks.hpp"

#include <atomic>
#include <chrono>
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

            ++passed_;

            std::cout << "[       OK ] " << name << '\n';
        }
        catch (const std::exception &e)
        {
            ++failed_;

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
                  << "Tests passed: " << passed_ << '\n'
                  << "Tests failed: " << failed_ << '\n'
                  << "============================\n";
    }

    int result() const
    {
        return failed_ == 0 ? 0 : 1;
    }

  private:
    int passed_ = 0;
    int failed_ = 0;
};

// ==================================================
// TEST SINK
// ==================================================

class TestSink final : public LogSink
{
  public:
    void write(const LogMessage &message) override
    {
        messages_.push_back(message);
    }

    void flush() override
    {
        ++flushCount_;
    }

    std::size_t count() const
    {
        return messages_.size();
    }

    int flushCount() const
    {
        return flushCount_;
    }

    const std::vector<LogMessage> &messages() const
    {
        return messages_;
    }

  private:
    // Accessed exclusively by the worker thread.
    std::vector<LogMessage> messages_;
    int flushCount_ = 0;
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
        ++count_;
    }

    void flush() override
    {
    }

    int count() const
    {
        return count_.load();
    }

  private:
    std::atomic<int> count_{0};
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
        Logger loggerA;
        Logger loggerB;

        auto sinkA = std::make_unique<TestSink>();
        auto sinkB = std::make_unique<TestSink>();

        auto *ptrA = sinkA.get();
        auto *ptrB = sinkB.get();

        loggerA.addSink(std::move(sinkA));
        loggerB.addSink(std::move(sinkB));

        loggerA.info("Message A");
        loggerB.info("Message B");

        loggerA.shutdown();
        loggerB.shutdown();

        tests.assert(ptrA->count() == 1, "Logger A received an incorrect message count");

        tests.assert(ptrB->count() == 1, "Logger B received an incorrect message count");

        tests.assert(ptrA->messages()[0].message == "Message A", "Logger A received an incorrect message");

        tests.assert(ptrB->messages()[0].message == "Message B", "Logger B received an incorrect message");
    });

    // --------------------------------------------------
    // 2. Concurrent producers
    // --------------------------------------------------

    tests.run("Concurrent producers", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        constexpr int threadCount = 10;
        constexpr int messagesPerThread = 1000;

        std::vector<std::jthread> threads;

        for (int i = 0; i < threadCount; ++i)
        {
            threads.emplace_back([i, &logger] {
                for (int j = 0; j < messagesPerThread; ++j)
                {
                    logger.info("Thread " + std::to_string(i) + " message " + std::to_string(j));
                }
            });
        }

        // Wait for every producer.
        threads.clear();

        logger.shutdown();

        tests.assert(sinkPtr->count() == threadCount * messagesPerThread, "Some messages were lost");

        tests.assert(sinkPtr->flushCount() == 1, "Sink was not flushed exactly once");
    });

    // --------------------------------------------------
    // 3. Shutdown before startup
    // --------------------------------------------------

    tests.run("Shutdown before startup", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        logger.shutdown();

        logger.info("Ignored message");
        logger.warning("Ignored message");
        logger.error("Ignored message");

        tests.assert(sinkPtr->count() == 0, "Messages were accepted after shutdown");

        tests.assert(sinkPtr->flushCount() == 0, "Sink should not have been flushed without startup");
    });

    // --------------------------------------------------
    // 4. Logging after shutdown
    // --------------------------------------------------

    tests.run("Logging after shutdown", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        logger.info("Accepted message");

        logger.shutdown();

        const auto countAfterShutdown = sinkPtr->count();

        logger.info("Ignored");
        logger.warning("Ignored");
        logger.error("Ignored");

        tests.assert(sinkPtr->count() == countAfterShutdown, "Messages were accepted after shutdown");

        tests.assert(sinkPtr->count() == 1, "Accepted message was not delivered");
    });

    // --------------------------------------------------
    // 5. Repeated shutdown
    // --------------------------------------------------

    tests.run("Repeated shutdown", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        logger.info("Test message");

        logger.shutdown();
        logger.shutdown();
        logger.shutdown();

        tests.assert(sinkPtr->count() == 1, "Unexpected message count");

        tests.assert(sinkPtr->flushCount() == 1, "Sink was flushed more than once");
    });

    // --------------------------------------------------
    // 6. Concurrent shutdown
    // --------------------------------------------------

    tests.run("Concurrent shutdown", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        constexpr int messageCount = 1000;

        for (int i = 0; i < messageCount; ++i)
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

        tests.assert(sinkPtr->count() == messageCount, "Messages were lost during concurrent shutdown");

        tests.assert(sinkPtr->flushCount() == 1, "Sink was flushed more than once");
    });

    // --------------------------------------------------
    // 7. Shutdown racing producers
    // --------------------------------------------------

    tests.run("Shutdown racing producers", [&] {
        Logger logger;

        auto sink = std::make_unique<TestSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        constexpr int threadCount = 8;
        constexpr int messagesPerThread = 10000;

        std::atomic<int> ready{0};
        std::atomic<bool> start{false};

        std::vector<std::jthread> threads;

        for (int i = 0; i < threadCount; ++i)
        {
            threads.emplace_back([i, &logger, &ready, &start] {
                ++ready;

                while (!start.load())
                    std::this_thread::yield();

                for (int j = 0; j < messagesPerThread; ++j)
                {
                    logger.info("Thread " + std::to_string(i) + " message " + std::to_string(j));
                }
            });
        }

        // Wait until all producers are ready.
        while (ready.load() != threadCount)
            std::this_thread::yield();

        start = true;

        // Race shutdown against active producers.
        logger.shutdown();

        // Producers may finish their remaining calls.
        // Those calls must be silently rejected.
        threads.clear();

        const auto acceptedCount = sinkPtr->count();

        logger.info("Must be ignored");

        tests.assert(sinkPtr->count() == acceptedCount, "Messages were accepted after shutdown completed");

        tests.assert(acceptedCount <= threadCount * messagesPerThread, "Invalid message count");

        tests.assert(sinkPtr->flushCount() == (acceptedCount > 0 ? 1 : 0), "Unexpected sink flush count");
    });

    // --------------------------------------------------
    // 8. Slow sink and queue draining
    // --------------------------------------------------

    tests.run("Slow sink drains queue", [&] {
        Logger logger;

        auto sink = std::make_unique<SlowSink>();
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        constexpr int messageCount = 50;

        for (int i = 0; i < messageCount; ++i)
        {
            logger.info("Slow message");
        }

        logger.shutdown();

        tests.assert(sinkPtr->count() == messageCount, "Shutdown did not drain the queue");
    });

    // --------------------------------------------------
    // 9. Throwing sink isolation
    // --------------------------------------------------

    tests.run("Throwing sink isolation", [&] {
        Logger logger;

        auto throwingSink = std::make_unique<ThrowingSink>();
        auto recordingSink = std::make_unique<TestSink>();

        auto *recordingPtr = recordingSink.get();

        logger.addSink(std::move(throwingSink));
        logger.addSink(std::move(recordingSink));

        constexpr int messageCount = 100;

        for (int i = 0; i < messageCount; ++i)
        {
            logger.info("Test message");
        }

        logger.shutdown();

        tests.assert(recordingPtr->count() == messageCount,
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
        auto *sinkPtr = sink.get();

        logger.addSink(std::move(sink));

        const auto expectedLine = std::source_location::current().line() + 2;

        logger.info("Source location test");

        logger.shutdown();

        tests.assert(sinkPtr->count() == 1, "Expected one message");

        const auto &message = sinkPtr->messages()[0];

        tests.assert(message.location.line() == expectedLine, "Incorrect source line");

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
