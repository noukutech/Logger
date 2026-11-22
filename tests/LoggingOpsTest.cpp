/**
 * @file LoggingOpsTest.cpp
 * @brief Shutdown and failure-path tests for the LoggingOps watcher/queue machinery.
 */
#include "LoggingOps.hpp"
#include "FileOps.hpp"
#include "CommonFunc.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

using namespace logger;
using namespace std::chrono_literals;

namespace
{
    // Minimal sink: counts written records, writes nowhere.
    class CountingSink : public LoggingOps
    {
        public:
            CountingSink() { m_watcher = std::thread([this]() { keepWatchAndPull(); }); }
            ~CountingSink() override
            {
                stopWatcher();
                // Rejected-after-shutdown records are expected here; keep them out of the exception log file
                std::scoped_lock<std::mutex> lock(m_excpMtx);
                m_excpPtrVec.clear();
            }
            const std::string getClassId() const override { return "CountingSink"; }
            void shutdown() { stopWatcher(); }
            std::atomic<size_t> written{0};

        protected:
            void writeDataTo(const std::string_view data) override { push(data); }
            void writeToOutStreamObject(BufferQ&& dataQueue, std::exception_ptr&) override
            {
                written += dataQueue.size();
                std::this_thread::sleep_for(1ms);   // slow sink so the queue keeps refilling
            }
    };
}

namespace
{
    // Sink whose writer blocks until released, to observe flush() while a batch is in flight
    class GatedSink : public LoggingOps
    {
        public:
            GatedSink() { m_watcher = std::thread([this]() { keepWatchAndPull(); }); }
            ~GatedSink() override { release(); stopWatcher(); }
            void shutdown() { stopWatcher(); }
            bool shuttingDown() { std::scoped_lock<std::mutex> lock(m_DataRecordsMtx); return m_shutAndExit.load(); }
            const std::string getClassId() const override { return "GatedSink"; }
            void release() { std::call_once(m_released, [this]() { m_gate.set_value(); }); }
            std::atomic<size_t> written{0};
            std::atomic<bool> writerEntered{false};

        protected:
            void writeDataTo(const std::string_view data) override { push(data); }
            void writeToOutStreamObject(BufferQ&& dataQueue, std::exception_ptr&) override
            {
                writerEntered = true;
                m_gateFuture.wait();
                written += dataQueue.size();
            }

        private:
            std::promise<void> m_gate;
            std::shared_future<void> m_gateFuture = m_gate.get_future().share();
            std::once_flag m_released;
    };
}

// Regression: shutdown accepted records until the queue happened to be empty, so steady
// producers could keep stopWatcher() from ever joining.
TEST(LoggingOpsShutdownTest, shutdownCompletesUnderLoadAndAccountsForEveryRecord)
{
    CountingSink sink;
    std::atomic<bool> stop{false};
    std::vector<size_t> attempts(4, 0);
    std::vector<std::thread> producers;
    for (size_t t = 0; t < attempts.size(); ++t)
    {
        producers.emplace_back([&, t]()
        {
            while (!stop)
            {
                sink.write(std::string_view("record"));
                ++attempts[t];
            }
        });
    }
    std::thread flusher([&]() { while (!stop) sink.flush(); });

    std::this_thread::sleep_for(50ms);
    std::promise<void> shutDown;
    auto shutDownFuture = shutDown.get_future();
    std::thread stopper([&]() { sink.shutdown(); shutDown.set_value(); });
    // Producers keep running while we wait: this is exactly the load that used to starve shutdown
    const bool returnedUnderLoad = shutDownFuture.wait_for(10s) == std::future_status::ready;

    // Clean up before asserting, so a failure reports instead of hanging (a real deadlock is bounded by the test runner)
    stop = true;
    for (auto& p : producers)
        p.join();
    flusher.join();
    stopper.join();

    EXPECT_TRUE(returnedUnderLoad) << "stopWatcher() did not return while producers were running";
    size_t total = 0;
    for (const auto a : attempts)
        total += a;
    // Every write was either written by the watcher or reported as rejected; none silently lost
    EXPECT_EQ(total, sink.written.load() + sink.getAllExceptions().size());
    EXPECT_GT(sink.written.load(), 0u);
}

// Waits up to `limit` for `cond`; returns whether it became true
template<typename Cond>
static bool waitFor(Cond cond, std::chrono::milliseconds limit)
{
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!cond() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(1ms);
    return cond();
}

// flush() must stay blocked until the batch holding its records is actually written.
TEST(LoggingOpsShutdownTest, flushWaitsForInFlightBatch)
{
    GatedSink sink;
    for (int i = 0; i < 10; ++i)
        sink.write(std::string_view("pending"));
    std::atomic<bool> flushed{false};
    std::thread flusher([&]() { sink.flush(); flushed = true; });

    const bool entered = waitFor([&]() { return sink.writerEntered.load(); }, 2000ms);
    std::this_thread::sleep_for(100ms);
    const bool flushedWhileGated = flushed;

    // Release and join before asserting, so a failed check cannot leave a joinable thread behind
    sink.release();
    flusher.join();

    EXPECT_TRUE(entered) << "watcher never picked up the batch";
    EXPECT_FALSE(flushedWhileGated) << "flush() returned before its batch was written";
    EXPECT_EQ(10u, sink.written.load());
}

// Regression: starting shutdown used to release a pending flush() before its batch was written.
TEST(LoggingOpsShutdownTest, shutdownDoesNotReleasePendingFlushEarly)
{
    GatedSink sink;
    for (int i = 0; i < 10; ++i)
        sink.write(std::string_view("pending"));
    std::atomic<bool> flushed{false};
    std::atomic<bool> stopped{false};
    std::thread flusher([&]() { sink.flush(); flushed = true; });
    const bool entered = waitFor([&]() { return sink.writerEntered.load(); }, 2000ms);

    std::thread stopper([&]() { sink.shutdown(); stopped = true; });
    // Shutdown must actually have begun while the batch is still gated, or this test proves nothing
    const bool shutdownBegan = waitFor([&]() { return sink.shuttingDown(); }, 2000ms);
    std::this_thread::sleep_for(50ms);
    const bool flushedDuringShutdown = flushed;
    const bool stoppedWhileGated = stopped;

    sink.release();
    flusher.join();
    stopper.join();

    EXPECT_TRUE(entered) << "watcher never picked up the batch";
    EXPECT_TRUE(shutdownBegan) << "shutdown flag was never observed while the writer was gated";
    EXPECT_FALSE(flushedDuringShutdown) << "shutdown released flush() before its batch was written";
    EXPECT_FALSE(stoppedWhileGated) << "stopWatcher() returned before the in-flight batch was written";
    EXPECT_EQ(10u, sink.written.load());
}

#ifndef _WIN32  // RLIMIT_FSIZE is POSIX; it lets the open succeed and makes later writes fail (EFBIG)
namespace
{
    // FileOps that drops its recorded exceptions on destruction, so the test leaves no exception log behind
    class QuietFileOps : public FileOps
    {
        public:
            using FileOps::FileOps;
            ~QuietFileOps() override
            {
                stopWatcher();
                std::scoped_lock<std::mutex> lock(m_excpMtx);
                m_excpPtrVec.clear();
            }
    };
}

// Regression: a write/close failure after a successful open was never checked, so records were dropped silently.
TEST(LoggingOpsFailureTest, writeFailureAfterOpenIsReportedAndFlushReturns)
{
    const auto dir = std::filesystem::temp_directory_path() / ("logger_efbig_" + CommonTestDataGenerator::generateRandomText(8));
    ASSERT_TRUE(std::filesystem::create_directory(dir));

    rlimit original{};
    ASSERT_EQ(0, getrlimit(RLIMIT_FSIZE, &original));
    auto previousHandler = std::signal(SIGXFSZ, SIG_IGN);   // get EFBIG instead of being killed
    rlimit limited = original;
    limited.rlim_cur = 4096;
    ASSERT_EQ(0, setrlimit(RLIMIT_FSIZE, &limited));

    bool sawError = false;
    {
        QuietFileOps file(64 * 1024 * 1024, "efbig.log", dir.string());
        for (int i = 0; i < 200; ++i)
            file.write(std::string(100, 'x'));   // ~20 KB, well past the 4 KB limit
        file.flush();   // must return even though the batch failed
        sawError = !file.getAllExceptions().empty();
        setrlimit(RLIMIT_FSIZE, &original);   // restore before anything else touches the filesystem
    }
    std::signal(SIGXFSZ, previousHandler);

    EXPECT_TRUE(sawError) << "write failure after open was not reported";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
#endif
