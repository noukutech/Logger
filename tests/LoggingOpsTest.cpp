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

// Regression: shutdown accepted records until the queue happened to be empty, so steady
// producers could keep stopWatcher() from ever joining; flush() waiters must also be released.
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
    auto done = std::async(std::launch::async, [&]() { sink.shutdown(); });
    ASSERT_EQ(std::future_status::ready, done.wait_for(10s)) << "stopWatcher() did not return under load";

    stop = true;
    for (auto& p : producers)
        p.join();
    flusher.join();

    size_t total = 0;
    for (const auto a : attempts)
        total += a;
    // Every write was either written by the watcher or reported as rejected; none silently lost
    EXPECT_EQ(total, sink.written.load() + sink.getAllExceptions().size());
    EXPECT_GT(sink.written.load(), 0u);
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
