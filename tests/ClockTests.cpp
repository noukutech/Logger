/**
 * @file ClockTests.cpp
 * @brief Unit tests for the Clock class using Google Test framework.
 *
 * This file contains a series of test cases to verify the functionality of the Clock class.
 * The tests cover various aspects of the Clock class, including time retrieval, formatting,
 * and timer functionality.
 *
 * The following test cases are included:
 * - testGetLocalTime: Tests the retrieval of local time.
 * - testGetGmtTime: Tests the retrieval of GMT time.
 * - testGetLocalTimeStr: Tests the retrieval of formatted local time strings.
 * - testGetGmtTimeStr: Tests the retrieval of formatted GMT time strings.
 * - testGetDayOfWeek: Tests the retrieval of the current day of the week.
 * - testGetMonth: Tests the retrieval of the current month.
 * - testGetYear: Tests the retrieval of the current year.
 * - testGetTimeOfTheDay: Tests the retrieval of the current time of the day (hours, minutes, seconds).
 * - testGetGmtTimeOfTheDay: Tests the retrieval of the current GMT time of the day (hours, minutes, seconds).
 * - testStart: Tests the start functionality of the timer.
 * - testStop: Tests the stop functionality of the timer.
 * - testGetElapsedTime: Tests the retrieval of elapsed time in different units (seconds, milliseconds).
 * - testTimerThreadSafety: Tests the thread safety of the timer functionality.
 *
 * The Clock class is expected to provide accurate time information and handle timer operations
 * correctly, even in multi-threaded environments.
 *
 * @note The tests use the Google Test framework for assertions and test case management.
 */
#include "Clock.hpp"

#include <iomanip>

#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <algorithm>
#include <ctime>
#include <sstream>
#include <thread>
#include <future>
#include <atomic>

using namespace logger;

#define nullString ""

class ClockTests : public ::testing::Test
{
    protected:
        Clock clock;
};

// Formats a time_t the way Clock does, for comparison against values captured around the call
static std::string putTime(std::time_t timeT, const bool local)
{
    std::ostringstream oss;
    oss << std::put_time(local ? std::localtime(&timeT) : std::gmtime(&timeT), "%c %Z");
    return oss.str();
}

TEST_F(ClockTests, testGetLocalTime)
{
    // Capture before and after so a second boundary during the call cannot fail the test
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto expLocalTime = clock.getLocalTime();
    const auto after = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    EXPECT_TRUE(expLocalTime == putTime(before, true) || expLocalTime == putTime(after, true)) << expLocalTime;
}

TEST_F(ClockTests, testGetGmtTime)
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto expGmtTime = clock.getGmtTime();
    const auto after = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    EXPECT_TRUE(expGmtTime == putTime(before, false) || expGmtTime == putTime(after, false)) << expGmtTime;
}

TEST_F(ClockTests, testGetLocalTimeStr)
{
    auto formattedLocalTime = [](const std::string_view format, const time_t* time)
    {
        std::array<char, 80> buffer;
        std::strftime(buffer.data(), sizeof(buffer), format.data(), std::localtime(time));
        return std::string(buffer.data());
    };     
    
    {
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expLocalTimeStr = clock.getLocalTimeStr();
        auto localTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::string format = "%d/%m/%Y %H:%M:%S";
        EXPECT_TRUE(expLocalTimeStr == formattedLocalTime(format, &before) || expLocalTimeStr == formattedLocalTime(format, &localTime)) << expLocalTimeStr;
    }
    {
        std::string format = "%Y/%m/%d %H:%M:%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expLocalTimeStr = clock.getLocalTimeStr(format);
        auto localTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expLocalTimeStr == formattedLocalTime(format, &before) || expLocalTimeStr == formattedLocalTime(format, &localTime)) << expLocalTimeStr;
    }
    {
        std::string format = "%Y-%m-%d %H:%M:%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expLocalTimeStr = clock.getLocalTimeStr(format);
        auto localTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expLocalTimeStr == formattedLocalTime(format, &before) || expLocalTimeStr == formattedLocalTime(format, &localTime)) << expLocalTimeStr;
    }
    {
        std::string format = "%Y%m%d_%H:%M:%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expLocalTimeStr = clock.getLocalTimeStr(format);
        auto localTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expLocalTimeStr == formattedLocalTime(format, &before) || expLocalTimeStr == formattedLocalTime(format, &localTime)) << expLocalTimeStr;
    }
    {
        std::string format = "%Y%m%d_%H%M%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expLocalTimeStr = clock.getLocalTimeStr(format);
        auto localTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expLocalTimeStr == formattedLocalTime(format, &before) || expLocalTimeStr == formattedLocalTime(format, &localTime)) << expLocalTimeStr;
    }
}

TEST_F(ClockTests, testGetGmtTimeStr)
{
    auto formattedGmtTime = [](const std::string_view format, const time_t* time)
    {
        std::array<char, 80> buffer;
        std::strftime(buffer.data(), sizeof(buffer), format.data(), std::gmtime(time));
        return std::string(buffer.data());
    };     
    
    {
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expGmtTimeStr = clock.getGmtTimeStr();
        auto gmtTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::string format = "%d/%m/%Y %H:%M:%S";
        EXPECT_TRUE(expGmtTimeStr == formattedGmtTime(format, &before) || expGmtTimeStr == formattedGmtTime(format, &gmtTime)) << expGmtTimeStr;
    }
    {
        std::string format = "%Y/%m/%d %H:%M:%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expGmtTimeStr = clock.getGmtTimeStr(format);
        auto gmtTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expGmtTimeStr == formattedGmtTime(format, &before) || expGmtTimeStr == formattedGmtTime(format, &gmtTime)) << expGmtTimeStr;
    }
    {
        std::string format = "%Y-%m-%d %H:%M:%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expGmtTimeStr = clock.getGmtTimeStr(format);
        auto gmtTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expGmtTimeStr == formattedGmtTime(format, &before) || expGmtTimeStr == formattedGmtTime(format, &gmtTime)) << expGmtTimeStr;
    }
    {
        std::string format = "%Y%m%d_%H:%M:%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expGmtTimeStr = clock.getGmtTimeStr(format);
        auto gmtTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expGmtTimeStr == formattedGmtTime(format, &before) || expGmtTimeStr == formattedGmtTime(format, &gmtTime)) << expGmtTimeStr;
    }
    {
        std::string format = "%Y%m%d_%H%M%S";
        const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());   // bracket the call: a second boundary must not fail the test
        auto expGmtTimeStr = clock.getGmtTimeStr(format);
        auto gmtTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        EXPECT_TRUE(expGmtTimeStr == formattedGmtTime(format, &before) || expGmtTimeStr == formattedGmtTime(format, &gmtTime)) << expGmtTimeStr;
    }
}

TEST_F(ClockTests, testGetDayOfWeek)
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto expDayOfWeek = clock.getDayOfWeek();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto localTime = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(localTime, "%A");
    std::ostringstream ossBefore;
    ossBefore << std::put_time(std::localtime(&before), "%A");
    EXPECT_TRUE(expDayOfWeek == oss.str() || expDayOfWeek == ossBefore.str()) << expDayOfWeek;
}

TEST_F(ClockTests, testGetMonth)
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto expMonth = clock.getMonth();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto localTime = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(localTime, "%B");
    std::ostringstream ossBefore;
    ossBefore << std::put_time(std::localtime(&before), "%B");
    EXPECT_TRUE(expMonth == oss.str() || expMonth == ossBefore.str()) << expMonth;
}

TEST_F(ClockTests, testGetYear)
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto expYear = clock.getYear();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto localTime = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(localTime, "%Y");
    std::ostringstream ossBefore;
    ossBefore << std::put_time(std::localtime(&before), "%Y");
    EXPECT_TRUE(expYear == oss.str() || expYear == ossBefore.str()) << expYear;
}

TEST_F(ClockTests, testGetTimeOfTheDay)
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto [hours, minutes, seconds] = clock.getTimeOfTheDay();
    const auto after = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto matches = [&](std::time_t t) { const std::tm tmVal = *std::localtime(&t); return hours == tmVal.tm_hour && minutes == tmVal.tm_min && seconds == tmVal.tm_sec; };
    EXPECT_TRUE(matches(before) || matches(after)) << hours << ":" << minutes << ":" << seconds;
}

TEST_F(ClockTests, testGetGmtTimeOfTheDay)
{
    const auto before = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto [hours, minutes, seconds] = clock.getGmtTimeOfTheDay();
    const auto after = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto matches = [&](std::time_t t) { const std::tm tmVal = *std::gmtime(&t); return hours == tmVal.tm_hour && minutes == tmVal.tm_min && seconds == tmVal.tm_sec; };
    EXPECT_TRUE(matches(before) || matches(after)) << hours << ":" << minutes << ":" << seconds;
}

TEST_F(ClockTests, testStart)
{
    clock.start();
    EXPECT_TRUE(clock.isRunning());
}

TEST_F(ClockTests, testStop)
{
    clock.start();
    clock.stop();
    EXPECT_FALSE(clock.isRunning());
}

TEST_F(ClockTests, testGetElapsedTime)
{
    // Lower bounds allow 5%: on Windows sleep_for and steady_clock use different timer sources, so a
    // 1 s sleep can measure 0.9999 s. Upper bounds only catch wrong units; shared CI runners oversleep a lot.
    {
        clock.start();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        clock.stop();
        auto elapsedTime = clock.getElapsedTime(TimeUnits::SECONDS);
        EXPECT_GE(elapsedTime, 0.95);
        EXPECT_LT(elapsedTime, 5.0);
    }
    {
        clock.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        clock.stop();
        auto elapsedTime = clock.getElapsedTime(TimeUnits::MILLISECONDS);
        EXPECT_GE(elapsedTime, 9.5);
        EXPECT_LT(elapsedTime, 1000.0);   // a seconds/microseconds mix-up would be off by 1000x
    }
}

TEST_F(ClockTests, testTimerThreadSafety)
{
    // Explicit handshakes instead of sleeps: each thread checks the timer state only after the other side has acted
    {
        std::promise<void> started, checkedRunning;
        std::thread t1([&]()
        {
            clock.start();
            EXPECT_TRUE(clock.isRunning());
            started.set_value();
            checkedRunning.get_future().wait();
            clock.stop();
        });
        std::thread t2([&]()
        {
            started.get_future().wait();
            EXPECT_TRUE(clock.isRunning());
            checkedRunning.set_value();
        });
        t1.join();
        t2.join();
        EXPECT_FALSE(clock.isRunning());
    }
    {
        std::promise<void> started;
        std::thread t1([&]()
        {
            clock.start();
            started.set_value();
        });
        std::thread t2([&]()
        {
            started.get_future().wait();
            EXPECT_TRUE(clock.isRunning());
            EXPECT_EQ(clock.getElapsedTime(TimeUnits::MILLISECONDS), -1.0);   // elapsed is undefined while running
        });
        t1.join();
        t2.join();
        clock.stop();
    }
}


// Regression: the format was kept as a string_view and dangled when built from a temporary std::string.
TEST(ClockRegressionTests, formatFromTemporaryStringOutlivesIt)
{
    std::unique_ptr<Clock> clock;
    {
        std::string fmt = "%Y";
        clock = std::make_unique<Clock>(fmt);
        fmt.assign(fmt.size(), 'X');   // overwrite the source buffer
    }
    // Year is 4 digits; a dangling view would yield "XX" or garbage
    const auto year = clock->getLocalTimeStr();
    ASSERT_EQ(4u, year.size()) << year;
    EXPECT_TRUE(std::all_of(year.begin(), year.end(), [](char c) { return c >= '0' && c <= '9'; })) << year;
}

// Regression: an oversized strftime result left the buffer unspecified; it must return "" safely.
TEST(ClockRegressionTests, oversizedFormatReturnsEmpty)
{
    Clock clock;
    EXPECT_EQ(std::string(), clock.getLocalTimeStr(std::string(100, 'A')));
}

// Concurrency: many threads formatting time at once must agree on the year (shared tm buffer race).
TEST(ClockRegressionTests, concurrentFormattingIsConsistent)
{
    Clock clock;
    // Bracket the run so crossing local New Year mid-test cannot fail it
    const auto yearBefore = clock.getLocalTimeStr("%Y");
    std::vector<std::string> results(8 * 500);
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t)
        workers.emplace_back([&, t]() { for (int i = 0; i < 500; ++i) results[t * 500 + i] = clock.getLocalTimeStr("%Y"); });
    for (auto& w : workers)
        w.join();
    const auto yearAfter = clock.getLocalTimeStr("%Y");
    for (const auto& year : results)
        EXPECT_TRUE(year == yearBefore || year == yearAfter) << year;
}

// Regression: start()/stop() checked state outside the mutex; competing callers raced on the thread id (TSan).
TEST(ClockRegressionTests, competingStartStopCallersAreSafe)
{
    Clock clock;
    std::atomic<bool> go{false};
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t)
    {
        workers.emplace_back([&]()
        {
            while (!go) {}
            for (int i = 0; i < 2000; ++i)
            {
                clock.start();
                clock.stop();
                (void)clock.getElapsedTime(TimeUnits::NANOSECONDS);
            }
        });
    }
    go = true;
    for (auto& w : workers)
        w.join();
    // Whoever started last may still own it; stop from all threads is a no-op for non-owners
    SUCCEED();
}
