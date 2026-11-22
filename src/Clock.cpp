/**
 * File: Clock.cpp
 * Brief: Implementation of the Clock class for time measurement and formatting.
 * 
 * This file contains the implementation of the Clock class, which provides
 * functionalities for measuring elapsed time, getting the current time in
 * various formats, and retrieving specific components of the current time.
 * 
 * Copyright (c) 2025 Swarnendu RC
 * 
 * @license MIT License
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "Clock.hpp"

#include <iomanip>
#include <array>
#include <ctime>

using namespace logger;

namespace
{
    // std::localtime/std::gmtime share one static buffer and race across threads; use the reentrant forms
    std::tm toLocalTm(const std::time_t timeT) noexcept
    {
        std::tm tmVal{};
#if defined(_WIN32)
        localtime_s(&tmVal, &timeT);
#else
        localtime_r(&timeT, &tmVal);
#endif
        return tmVal;
    }

    std::tm toGmtTm(const std::time_t timeT) noexcept
    {
        std::tm tmVal{};
#if defined(_WIN32)
        gmtime_s(&tmVal, &timeT);
#else
        gmtime_r(&timeT, &tmVal);
#endif
        return tmVal;
    }

    // strftime returns 0 (buffer contents unspecified) when the result does not fit
    std::string formatTm(const std::string_view format, const std::tm& tmVal)
    {
        std::array<char, 80> buffer{};
        const std::string fmtStr(format);
        const auto len = std::strftime(buffer.data(), buffer.size(), fmtStr.c_str(), &tmVal);
        return std::string(buffer.data(), len);
    }
}

void Clock::start()
{
    // Check and transition under one lock: two starters, or start vs stop, must not interleave
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isRunning)
        return;
    m_startTime = std::chrono::steady_clock::now();
    m_threadId = std::this_thread::get_id();
    m_isRunning = true;   // publish last, after the thread id it guards
}

void Clock::stop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isRunning || (m_threadId != std::this_thread::get_id()))
        return;
    m_endTime = std::chrono::steady_clock::now();
    m_isRunning = false;
    m_condition.notify_one();
}

double Clock::getElapsedTime(const TimeUnits& units)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_isRunning || (m_threadId != std::this_thread::get_id()))
        return -1.0;

    // Fractional result: duration_cast truncated e.g. 0.999 s to 0
    switch (units)
    {
        case TimeUnits::SECONDS:
        return std::chrono::duration<double, std::ratio<1>>(m_endTime - m_startTime).count();
        case TimeUnits::MILLISECONDS:
            return std::chrono::duration<double, std::milli>(m_endTime - m_startTime).count();
        case TimeUnits::MICROSECONDS:
            return std::chrono::duration<double, std::micro>(m_endTime - m_startTime).count();
        case TimeUnits::NANOSECONDS:
            return std::chrono::duration<double, std::nano>(m_endTime - m_startTime).count();
        default:
            return -1.0;
    }
}

std::string Clock::getGmtTime() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto gmtTimeTm = toGmtTm(now);
    const auto* gmtTime = &gmtTimeTm;
    std::ostringstream oss;
    oss << std::put_time(gmtTime, "%c %Z");
    return oss.str();
}

std::string Clock::getLocalTime() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto localTimeTm = toLocalTm(now);
    const auto* localTime = &localTimeTm;
    std::ostringstream oss;
    oss << std::put_time(localTime, "%c %Z");
    return oss.str();
}

std::string Clock::getGmtTimeStr(const std::string_view format) const
{
    auto now = std::chrono::system_clock::now();
    auto nowTimeT = std::chrono::system_clock::to_time_t(now);
    return formatTm(format.empty() ? std::string_view(m_strFormat) : format, toGmtTm(nowTimeT));
}

std::string Clock::getLocalTimeStr(const std::string_view format) const
{
    auto nowTimeT = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto localTimeTTm = toLocalTm(nowTimeT);
    const auto* localTimeT = &localTimeTTm;
    return formatTm(format.empty() ? std::string_view(m_strFormat) : format, *localTimeT);
}

std::string Clock::getDayOfWeek() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto localTimeTm = toLocalTm(now);
    const auto* localTime = &localTimeTm;
    std::ostringstream oss;
    oss << std::put_time(localTime, "%A");
    return oss.str();
}

std::string Clock::getMonth() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto localTimeTm = toLocalTm(now);
    const auto* localTime = &localTimeTm;
    std::ostringstream oss;
    oss << std::put_time(localTime, "%B");
    return oss.str();
}

std::string Clock::getYear() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto localTimeTm = toLocalTm(now);
    const auto* localTime = &localTimeTm;
    std::ostringstream oss;
    oss << std::put_time(localTime, "%Y");
    return oss.str();
}

std::tuple<int, int, int> Clock::getTimeOfTheDay() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto localTimeTm = toLocalTm(now);
    const auto* localTime = &localTimeTm;
    return std::make_tuple(localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
}

std::tuple<int, int, int> Clock::getGmtTimeOfTheDay() const
{
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const auto gmtTimeTm = toGmtTm(now);
    const auto* gmtTime = &gmtTimeTm;
    return std::make_tuple(gmtTime->tm_hour, gmtTime->tm_min, gmtTime->tm_sec);
}

