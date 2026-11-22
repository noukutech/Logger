/*
 * MIT License
 * 
 * Copyright (c) 2024 Nouku Technologies
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * File: LoggingOps.cpp
 * Description: Implementation of the LoggingOps class for asynchronous data operations.
 * See LoggingOps.hpp for class definition and documentation.
 */

#include "LoggingOps.hpp"
#include "Clock.hpp"

#include <sstream>
#include <bitset>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace logger;

static std::mutex m_excpFileMtx;

namespace logger
{
/*friend*/ void operator<<(LoggingOps& obj, const std::ostringstream& oss)
{
    if (oss.good())
        obj.write(oss.str());
}

/*friend*/ void operator<<(LoggingOps& obj, const std::istringstream& iss)
{
    if (iss.good())
        obj.write(iss.str());
}

/*friend*/ void operator<<(LoggingOps& obj, const std::string_view data)
{
    if (!data.empty())
        obj.write(data);
}

/*friend*/ void operator<<(LoggingOps& obj, const std::vector<std::string>& dataVec)
{
    if (!dataVec.empty())
        obj.write(dataVec);
}

/*friend*/ void operator<<(LoggingOps& obj, const std::list<std::string>& dataList)
{
    if (!dataList.empty())
        obj.write(dataList);
}
}   // namespace logger

LoggingOps::LoggingOps()
    : m_DataRecords()
    , m_dataReady(false)
    , m_shutAndExit(false)
    , m_excpPtrVec(0)
{
}

/*virtual*/LoggingOps::~LoggingOps()
{
    // Derived classes stop the watcher in their own destructors (while their
    // overrides are still alive); this call is a no-op in that case.
    stopWatcher();

    collectAndPrintExceptions();
}

void LoggingOps::stopWatcher()
{
    {
        std::scoped_lock<std::mutex> dataLock(m_DataRecordsMtx);
        // Set flag true to indicate shutting down; the watcher drains pending records first
        m_shutAndExit = true;
    }
    m_DataRecordsCv.notify_all();

    if (m_watcher.joinable())
        m_watcher.join();
}

void LoggingOps::push(const std::string_view data)
{
    if (data.empty())
        return;

    // Last byte of every record is reserved for the null terminator
    constexpr size_t chunkSize = bufferSize - 1;
    bool notifyWatcher = false;
    {
        std::array<char, bufferSize> dataRecord{};
        std::scoped_lock<std::mutex> lock(m_DataRecordsMtx);
        // Admission closes as soon as shutdown starts, so steady producers cannot keep the watcher from exiting
        if (m_shutAndExit)
        {
            addRaisedException(std::make_exception_ptr(std::runtime_error("Log record dropped: logging after shutdown")));
            return;
        }
        for (size_t pos = 0; pos < data.size(); pos += chunkSize)
        {
            const auto chunk = data.substr(pos, chunkSize);
            std::copy(chunk.begin(), chunk.end(), dataRecord.begin());
            std::fill(dataRecord.begin() + chunk.size(), dataRecord.end(), '\0');
            m_DataRecords.push(dataRecord);
            ++m_pushedSeq;
        }
        // Once the queue holds at least 256 records, wake the watcher to write them out
        if (m_DataRecords.size() >= 256)
        {
            m_dataReady = true;
            notifyWatcher = true;
        }
    }
    if (notifyWatcher)
        m_DataRecordsCv.notify_all();
}

bool LoggingOps::pop(BufferQ& data)
{
    // Caller holds m_DataRecordsMtx
    m_dataReady = false;
    if (m_DataRecords.empty())
        return false;

    BufferQ().swap(data);
    data.swap(m_DataRecords);
    return true;
}

void LoggingOps::keepWatchAndPull()
{
    BufferQ dataq;
    do
    {
        std::unique_lock<std::mutex> dataLock(m_DataRecordsMtx);
        m_DataRecordsCv.wait(dataLock, [this]{ return m_dataReady || m_shutAndExit.load(); });

        const bool shutdown = m_shutAndExit.load();
        const bool success = pop(dataq);
        const auto batchSize = dataq.size();
        dataLock.unlock();

        if (success)
        {
            // Write on a separate thread so the records queue stays free for producers
            std::exception_ptr excpPtr = nullptr;
            std::thread writerThread([this, &dataq, &excpPtr](){ writeToOutStreamObject(std::move(dataq), excpPtr); });
            writerThread.join();
            if (excpPtr)
                addRaisedException(excpPtr);
        }

        dataLock.lock();
        if (success)
            m_writtenSeq += batchSize;
        // Admission is already closed; exit once every accepted record is written
        const bool done = shutdown && m_DataRecords.empty();
        if (done)
            m_watcherDone = true;
        dataLock.unlock();
        // Wake flush() waiters
        m_DataRecordsCv.notify_all();

        if (done)
            break;
    } while (true);
}

void LoggingOps::flush()
{
    std::unique_lock<std::mutex> dataLock(m_DataRecordsMtx);
    // Wait only for records queued before this call, so steady producers cannot starve us
    const auto target = m_pushedSeq;
    // Shutdown does not release us early: the watcher drains every accepted record before it exits
    while (m_writtenSeq < target && !m_watcherDone)
    {
        // Re-arm on every wake-up: the watcher clears m_dataReady when it pops a batch
        if (!m_DataRecords.empty())
        {
            m_dataReady = true;
            m_DataRecordsCv.notify_all();
        }
        m_DataRecordsCv.wait(dataLock);
    }
}

void LoggingOps::write(const std::string_view data)
{
    if (data.empty())
        return;

    writeDataTo(data);
}

void LoggingOps::write(const std::vector<std::string_view>& dataVec) noexcept
{
    if (!dataVec.empty())
    {
        for (const auto& data : dataVec)
            write(data);
    }
}

void LoggingOps::write(const std::list<std::string_view>& dataList) noexcept
{
    if (!dataList.empty())
    {
        for (const auto& data : dataList)
            write(data);
    }
}

void LoggingOps::write(const std::vector<std::string>& dataVec) noexcept
{
    if (!dataVec.empty())
    {
        for (const auto& data : dataVec)
            write(data);
    }
}

void LoggingOps::write(const std::list<std::string>& dataList) noexcept
{
    if (!dataList.empty())
    {
        for (const auto& data : dataList)
            write(data);
    }
}

void LoggingOps::write(const uint8_t data)
{
    write(std::bitset<8>(data).to_string());
}

void LoggingOps::write(const uint16_t data)
{
    write(std::bitset<16>(data).to_string());
}

void LoggingOps::write(const uint32_t data)
{
    write(std::bitset<32>(data).to_string());
}

void LoggingOps::write(const uint64_t data)
{
    write(std::bitset<64>(data).to_string());
}

void LoggingOps::append(const std::string_view data)
{
    write(data);
}

void LoggingOps::append(const uint8_t data)
{
    write(std::bitset<8>(data).to_string());
}

void LoggingOps::append(const uint16_t data)
{
    write(std::bitset<16>(data).to_string());
}

void LoggingOps::append(const uint32_t data)
{
    write(std::bitset<32>(data).to_string());
}

void LoggingOps::append(const uint64_t data)
{
    write(std::bitset<64>(data).to_string());
}

void LoggingOps::append(const std::vector<uint8_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::append(const std::vector<uint16_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::append(const std::vector<uint32_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::append(const std::vector<uint64_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::write(const std::vector<uint8_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::write(const std::vector<uint16_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::write(const std::vector<uint32_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::write(const std::vector<uint64_t>& binaryStream)
{
    if (!binaryStream.empty())
    {
        for (const auto& bindata : binaryStream)
            write(bindata);
    }
}

void LoggingOps::collectAndPrintExceptions()
{
    // Check for if there is any data write exceptions
    // before quitting. If so, log them in a common file
    if (!m_excpPtrVec.empty())
    {
        auto constructMsg = [](const std::exception& excp)
        {
            std::stringstream ss;
            Clock clock("%Y%m%d_%H%M%S");
            ss << m_FieldSep << clock.getLocalTimeStr() << m_FieldSep;
            ss << std::this_thread::get_id() << m_FieldSep;
            ss << ">> " << excp.what() << "\n";
            return ss.str();
        };

        auto currFilePath = std::filesystem::current_path().string();
        currFilePath += "/";
        //Finally create the file path object
        auto filePathObj = std::filesystem::path(currFilePath + m_ExcpLogFileName.data());

        std::unique_lock<std::mutex> fileLock(m_excpFileMtx);
        std::ofstream excpFile(filePathObj, std::ios::app | std::ios::binary);
        if (excpFile.is_open())
        {
            for (const auto& excp : m_excpPtrVec)
            {
                try
                {
                    if (excp)
                        std::rethrow_exception(excp);
                }
                catch(const std::exception& e)
                {
                    excpFile << constructMsg(e);
                }
            }
            excpFile.close();
        }
        fileLock.unlock();
    }
}

