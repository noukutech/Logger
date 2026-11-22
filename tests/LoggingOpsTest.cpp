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

#include "LogHelper.hpp"
#include <cstring>

namespace
{
    // Fails any batch that contains a record starting with "old"; records everything else.
    class ContentFailSink : public LoggingOps
    {
        public:
            explicit ContentFailSink(bool failAll = false) : m_failAll(failAll) { m_watcher = std::thread([this]() { keepWatchAndPull(); }); }
            ~ContentFailSink() override
            {
                stopWatcher();
                std::scoped_lock<std::mutex> lock(m_excpMtx);
                m_excpPtrVec.clear();   // expected failures; keep them out of the exception log file
            }
            const std::string getClassId() const override { return "ContentFailSink"; }
            size_t failureCount() { std::scoped_lock<std::mutex> lock(m_excpMtx); return m_excpPtrVec.size(); }
            std::atomic<size_t> terminalWrites{0};

        protected:
            void writeDataTo(const std::string_view data) override { push(data); }
            void writeToOutStreamObject(BufferQ&& dataQueue, std::exception_ptr& excpPtr) override
            {
                bool fail = m_failAll;
                bool sawTerminal = false;
                while (!dataQueue.empty())
                {
                    const char* rec = dataQueue.front().data();
                    if (std::strncmp(rec, "old", 3) == 0)
                        fail = true;
                    if (std::strncmp(rec, "terminal", 8) == 0)
                        sawTerminal = true;
                    dataQueue.pop();
                }
                if (fail)
                    excpPtr = std::make_exception_ptr(std::runtime_error("batch write failed"));
                else if (sawTerminal)
                    ++terminalWrites;
            }

        private:
            bool m_failAll;
    };
}

// Regression: success of the terminal (fatal/assert) record was inferred from a sink-wide error
// count, so a failure of an *older* queued batch during the pre-fatal drain sent a successfully
// written fatal record to stderr. The terminal record's own write result must decide.
TEST(TerminalRecordTest, olderBatchFailureDoesNotMarkTerminalRecordFailed)
{
    ContentFailSink sink;
    for (int i = 0; i < 10; ++i)
        sink.write(std::string_view("old-record"));   // queued, will fail when drained
    EXPECT_TRUE(persistTerminalRecord(sink, "terminal-record"));
    EXPECT_EQ(1u, sink.terminalWrites.load());
    EXPECT_GT(sink.failureCount(), 0u);   // the older batch did fail
}

TEST(TerminalRecordTest, failedTerminalWriteIsReported)
{
    ContentFailSink sink(/*failAll=*/true);
    EXPECT_FALSE(persistTerminalRecord(sink, "terminal-record"));
    EXPECT_EQ(0u, sink.terminalWrites.load());
    EXPECT_GT(sink.failureCount(), 0u);
}

#include <cstdlib>
#include <new>

namespace
{
    // Global operator new hook: once armed, the Nth matching allocation of a BufferQ node throws.
    std::atomic<int> g_failAfterChunkAllocs{-1};

    bool shouldFailChunkAlloc(std::size_t size)
    {
        // A deque node of std::array<char, bufferSize> is at least one record in size
        if (size < bufferSize)
            return false;
        int remaining = g_failAfterChunkAllocs.load();
        while (remaining >= 0)
        {
            if (g_failAfterChunkAllocs.compare_exchange_weak(remaining, remaining - 1))
                return remaining == 0;
        }
        return false;
    }

    class RecordingSink : public LoggingOps
    {
        public:
            RecordingSink() { m_watcher = std::thread([this]() { keepWatchAndPull(); }); }
            ~RecordingSink() override { stopWatcher(); }
            const std::string getClassId() const override { return "RecordingSink"; }
            std::vector<std::string> written() { std::scoped_lock<std::mutex> l(m_mtx); return m_written; }
            // Records accepted but not yet written must equal what is actually queued
            bool accountingConsistent()
            {
                std::scoped_lock<std::mutex> l(m_DataRecordsMtx);
                return m_pushedSeq - m_writtenSeq == m_DataRecords.size();
            }
        protected:
            void writeDataTo(const std::string_view data) override { push(data); }
            void writeToOutStreamObject(BufferQ&& dataQueue, std::exception_ptr&) override
            {
                std::scoped_lock<std::mutex> l(m_mtx);
                while (!dataQueue.empty()) { m_written.emplace_back(dataQueue.front().data()); dataQueue.pop(); }
            }
        private:
            std::mutex m_mtx;
            std::vector<std::string> m_written;
    };
}

void* operator new(std::size_t size)
{
    if (shouldFailChunkAlloc(size))
        throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

// Regression: push() added all chunks to m_pushedSeq only after every chunk was stored. If a later
// chunk's allocation threw, the stored chunks were written but never counted as accepted, so
// m_writtenSeq ran ahead and a later flush() could return before its own record was attempted.
TEST(LoggingOpsFaultTest, partialPushStillCountsStoredChunks)
{
    RecordingSink sink;
    // 17 chunks: crosses a deque block boundary with both libc++ (16 records/block) and
    // libstdc++ (1 record/node), so the second large allocation fails mid-record on both.
    const std::string big(17 * (bufferSize - 1), 'x');
    g_failAfterChunkAllocs = 1;
    EXPECT_THROW(sink.write(std::string_view(big)), std::bad_alloc);
    g_failAfterChunkAllocs = -1;
    EXPECT_TRUE(sink.accountingConsistent()) << "stored chunks were not counted as accepted";

    sink.write(std::string_view("after-partial-push"));
    sink.flush();
    const auto got = sink.written();
    ASSERT_FALSE(got.empty());
    EXPECT_EQ("after-partial-push", got.back()) << "flush() returned before its own record was written";
}
