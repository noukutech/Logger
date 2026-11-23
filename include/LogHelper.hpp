/*
 * MIT License
 * 
 * Copyright (c) 2025 Nouku Technologies
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
 */

/**
 * @file LogHelper.hpp
 * @brief Helper utilities and wrapper functions for the Logger library.
 *
 * This header provides a set of template utilities and convenience functions
 * to simplify logging operations using the Logger class. It includes type
 * traits for container detection, functions for setting logger properties,
 * and various logging wrappers for different log levels (info, warning, error,
 * debug, fatal, etc.), as well as support for logging lists and assertion failures.
 */
#ifndef LOGGER_HELPER_HPP
#define LOGGER_HELPER_HPP

#include "Logger.hpp"

#include <cstdio>

namespace logger
{
    template <typename T>
    struct is_vector : std::false_type{};

    template <typename T, typename Alloc>
    struct is_vector<std::vector<T, Alloc>> : std::true_type{};

    template <typename T>
    struct is_list : std::false_type{};

    template <typename T, typename Alloc>
    struct is_list<std::list<T, Alloc>> : std::true_type{};

    /**
     * @brief A global mutex to this file to protect
     * the concurrency of logger property attributes
     */
    // inline (not static): one mutex/logger per program. Per-TU copies broke when the linker
    // picked one TU's inline template instance for callers in another TU.
    inline std::mutex raceMtx;

    /**
     * @brief The logger object for the application.
     * This object is used to construct log messages
     * and write them to the log stream.
     *
     * @note inline because otherwise it will cause linker errors
     * when used in multiple translation units.
     */
    inline Logger loggerObj("%Y%m%d_%H%M%S");

    /**
     * @brief The stream object for logging operations.
     * This object is used to write log messages
     * either to console or to a file, depending on the configuration.
     * It is built using the Logger class's static method.
     *
     * @note inline because otherwise it will cause linker errors
     * when used in multiple translation units.
     */
    inline auto& loggingOps = Logger::buildLoggingOpsObject();

    /**
     * @brief Set the logger properties for the current log message.
     *
     * This function sets the file name, function name, marker, line number,
     * thread ID, and log type for the logger object. It is used to prepare
     * the logger object with necessary context before logging a message.
     *
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] marker A string marker to indicate the type of log (e.g., entry, exit).
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] tid The thread ID of the thread generating the log.
     * @param [in] logType The type of log (e.g., info, error, debug).
     * 
     * @tparam Args Variadic template parameters for the arguments to be formatted into the log message.
     * @param [in] format_str The format string for the log message.
     *                 It can contain placeholders for the arguments, similar to printf-style formatting.
     *                 For example, "Log message: {}" where {} will be replaced by the provided arguments.
     * @param [in] args The arguments to be formatted into the log message.
     *                 These arguments will be formatted according to the format string.
     *                 The number and types of arguments should match the placeholders in the format string.
     *
     * @note This function is typically called before logging a message
     * to ensure that the logger object has all the necessary context
     * for generating a meaningful log entry.
     *
     * @note inline because otherwise it will cause linker errors
     * when used in multiple translation units.
     */
    /**
     * @brief Format a record with the shared logger and return it, without enqueuing it.
     * @pre The caller holds raceMtx: loggerObj and its assertion condition are shared by all threads.
     */
    template<typename ...Args>
    inline std::string formatLocked
    (   const std::string_view fileName,
        const std::string_view funcName,
        const std::string_view marker,
        const size_t lineNo,
        const std::thread::id& tid,
        const LOG_TYPE& logType,
        const std::string_view format_str,
        Args&&... args
    )
    {
        loggerObj.setFileName(std::string(fileName))
                .setFunctionName(std::string(funcName))
                .setLineNo(lineNo)
                .setThreadId(tid)
                .setMarker(std::string(marker))
                .setLogType(logType);
        loggerObj.log(format_str, args...);
        return loggerObj.getLogStream().str();
    }

    /**
     * @brief Format a record with the shared logger and hand it to the logging sink.
     *
     * Same as logMsg() but without taking the lock, so a caller (log_assert) can set the
     * assertion condition and log in one critical section.
     *
     * @pre The caller holds raceMtx: loggerObj and its assertion condition are shared by all threads.
     */
    template<typename ...Args>
    inline void logMsgLocked
    (   const std::string_view fileName,
        const std::string_view funcName,
        const std::string_view marker,
        const size_t lineNo,
        const std::thread::id& tid,
        const LOG_TYPE& logType,
        const std::string_view format_str,
        Args&&... args
    )
    {
        loggingOps << formatLocked(fileName, funcName, marker, lineNo, tid, logType, format_str, args...);
    }

    template<typename ...Args>
    inline void logMsg
    (   const std::string_view fileName,
        const std::string_view funcName,
        const std::string_view marker,
        const size_t lineNo,
        const std::thread::id& tid,
        const LOG_TYPE& logType,
        const std::string_view format_str,
        Args&&... args
    )
    {
        // Lock it as in case of a threadpool ops
        // it might be very possible that multiple
        // threads are just lurcking around to get
        // a hold of this beautiful lady at the same
        // point of time.
        std::lock_guard<std::mutex> raceLock(raceMtx);
        logMsgLocked(fileName, funcName, marker, lineNo, tid, logType, format_str, args...);
    }

    /**
     * @brief Log a list or vector of messages.
     *
     * This function logs a list or vector of messages using the specified format string
     * and arguments. It checks if the provided List is either a std::vector or std::list
     * of strings, and if so, it logs each message in the list.
     *
     * @tparam List The type of the list or vector to be logged.
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] msgList The list or vector of messages to be logged.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename List, typename ...Args>
    void log_list
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const List& msgList,
        const std::string_view format_str,
        Args&&... args
    )
    {
        // If and only if, it is either a vector or std::list of strings
        if constexpr (is_list<List>::value || is_vector<List>::value)
        {
            logMsg( fileName,
                    funcName,
                    FORWARD_ANGLES,
                    lineNo,
                    std::this_thread::get_id(),
                    LOG_TYPE::LOG_INFO,
                    format_str,
                    args...);

            std::lock_guard<std::mutex> raceLock(raceMtx);
            loggingOps << msgList;
        }
    }

    /**
     * @brief Log an entry point message.
     *
     * This function logs a message indicating the entry point of a function or code block.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] debugMode The boolean value to indicate whether the logging will happen in DEBUG mode only
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_entry
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const bool debugMode,
        const std::string_view format_str,
        Args&&... args
    )
    {
        auto log = false;
        if (debugMode)
        {
#if defined (DEBUG) || (__DEBUG__)
            log = true;
#endif
        }
        else
        {
            log = true;
        }
        if (log)
        {
            logMsg( fileName,
                    funcName,
                    FORWARD_ANGLES,
                    lineNo,
                    std::this_thread::get_id(),
                    LOG_TYPE::LOG_INFO,
                    format_str,
                    args...);
        }
    }

    /**
     * @brief Log an exit point message.
     *
     * This function logs a message indicating the exit point of a function or code block.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] debugMode The boolean value to indicate whether the logging will happen in DEBUG mode only
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_exit
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const bool debugMode,
        const std::string_view format_str,
        Args&&... args
    )
    {
        auto log = false;
        if (debugMode)
        {
#if defined (DEBUG) || (__DEBUG__)
            log = true;
#endif
        }
        else
        {
            log = true;
        }
        if (log)
        {
            logMsg( fileName,
                    funcName,
                    BACKWARD_ANGLES,
                    lineNo,
                    std::this_thread::get_id(),
                    LOG_TYPE::LOG_INFO,
                    format_str,
                    args...);
        }
    }

    /**
     * @brief Log an error message.
     *
     * This function logs an error message with the specified format and arguments.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_err
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const std::string_view format_str,
        Args&&... args
    )
    {
        logMsg( fileName,
                funcName,
                FORWARD_ANGLE,
                lineNo,
                std::this_thread::get_id(),
                LOG_TYPE::LOG_ERR,
                format_str,
                args...);
    }

    /**
     * @brief Log a warning message.
     *
     * This function logs a warning message with the specified format and arguments.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_warn
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const std::string_view format_str,
        Args&&... args
    )
    {
        logMsg( fileName,
                funcName,
                FORWARD_ANGLE,
                lineNo,
                std::this_thread::get_id(),
                LOG_TYPE::LOG_WARN,
                format_str,
                args...);
    }

    /**
     * @brief Log an informational message.
     *
     * This function logs an informational message with the specified format and arguments.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_info
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const std::string_view format_str,
        Args&&... args
    )
    {
        logMsg( fileName,
                funcName,
                FORWARD_ANGLE,
                lineNo,
                std::this_thread::get_id(),
                LOG_TYPE::LOG_INFO,
                format_str,
                args...);
    }

    /**
     * @brief Log an important detail message.
     *
     * This function logs a message indicating an importance of the logged message.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_imp
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const std::string_view format_str,
        Args&&... args
    )
    {
        logMsg( fileName,
                funcName,
                FORWARD_ANGLE,
                lineNo,
                std::this_thread::get_id(),
                LOG_TYPE::LOG_IMP,
                format_str,
                args...);
    }

    /**
     * @brief Log a debug message.
     *
     * This function logs a debug message with the specified format and arguments.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     *
     * @note This function is only compiled and executed in debug mode.
     * It is used to log debug information during development and testing.
     * In release mode, this function will not be included in the binary.
     */
    template<typename ...Args>
    void log_dbg
    (
        [[maybe_unused]] const std::string_view fileName,
        [[maybe_unused]] const std::string_view funcName,
        [[maybe_unused]] const size_t lineNo,
        [[maybe_unused]] const std::string_view format_str,
        [[maybe_unused]] Args&&... args
    )
    {
    #if defined (DEBUG) || (__DEBUG__)
        logMsg( fileName,
                funcName,
                FORWARD_ANGLE,
                lineNo,
                std::this_thread::get_id(),
                LOG_TYPE::LOG_DBG,
                format_str,
                args...);
    #endif
    }

    /**
     * @brief Write a fatal/assert record to stderr with control characters escaped as `\\xNN`.
     * Allocation-free, so it is safe on a failing path right before abort()/exit().
     */
    inline void writeEscapedToStderr(const std::string_view record) noexcept
    {
        for (const char ch : record)
        {
            const auto c = static_cast<unsigned char>(ch);
            if (c < 0x20 || c == 0x7f)
                std::fprintf(stderr, "\\x%02x", c);
            else
                std::fputc(c, stderr);
        }
        std::fputs(" [logger: sink failed to write this record]\n", stderr);
        std::fflush(stderr);   // stderr may be fully buffered by the host; abort() would discard it
    }

    /**
     * @brief Persist the record that precedes process termination.
     *
     * Drains everything queued before it, then writes the record synchronously, so the write
     * result belongs to this record alone. abort()/exit() skip destructors, so this is the last
     * chance: if the sink reports a failure for this record, it goes to stderr instead.
     *
     * @return true if the sink wrote the record; false if it was sent to stderr instead.
     */
    inline bool persistTerminalRecord(LoggingOps& sink, const std::string_view record)
    {
        try
        {
            sink.flush();
            if (sink.writeRecordNow(record))
                return true;
        }
        catch (...)
        {
            // The record is already formatted; fall through to the stderr copy before terminating
        }
        writeEscapedToStderr(record);
        return false;
    }

    /**
     * @brief Log an assertion failure message.
     *
     * This function logs an assertion failure message with the specified condition and format.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message. If `exitGracefuly` is true, it will exit the program gracefully.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] cond The condition that failed (assertion).
     * @param [in] exitGracefuly Whether to exit gracefully or abort on assertion failure.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_assert
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const std::string_view cond,
        const bool exitGracefuly,
        const std::string_view format_str,
        Args&&... args
    )
    {
        if (cond.empty())
            return;

        // Termination must happen even if logging itself fails
        try
        {
            std::string record;
            {
                // Condition and record under one lock, so a concurrent log call cannot clear it or take it
                std::lock_guard<std::mutex> raceLock(raceMtx);
                loggerObj.setAssertCondition(std::string(cond));
                record = formatLocked(fileName,
                                      funcName,
                                      FORWARD_ANGLE,
                                      lineNo,
                                      std::this_thread::get_id(),
                                      LOG_TYPE::LOG_ASSERT,
                                      format_str,
                                      args...);
            }
            // Lock released first: atexit handlers may log, and abort/exit skip destructors so persist now
            persistTerminalRecord(loggingOps, record);
        }
        catch (...) {}
        if (exitGracefuly)
        {
            std::exit(EXIT_FAILURE);
        }
        else
        {
            std::abort();
        }
    }

    /**
     * @brief Log a fatal error message and abort the program.
     *
     * This function logs a fatal error message with the specified format and arguments.
     * It sets the logger properties such as file name, function name, line number, thread ID,
     * and log type before logging the message. After logging, it aborts the program.
     *
     * @tparam Args Variadic template parameters for additional arguments to be formatted into the log message.
     * @param [in] fileName The name of the file where the log is being generated.
     * @param [in] funcName The name of the function where the log is being generated.
     * @param [in] lineNo The line number in the source code where the log is being generated.
     * @param [in] format_str The format string for the log message.
     * @param [in] args Additional arguments to be formatted into the log message.
     */
    template<typename ...Args>
    void log_fatal
    (
        const std::string_view fileName,
        const std::string_view funcName,
        const size_t lineNo,
        const std::string_view format_str,
        Args&&... args
    )
    {
        // Termination must happen even if logging itself fails (e.g. bad_alloc)
        try
        {
            std::string record;
            {
                std::lock_guard<std::mutex> raceLock(raceMtx);
                record = formatLocked(fileName,
                                      funcName,
                                      FORWARD_ANGLE,
                                      lineNo,
                                      std::this_thread::get_id(),
                                      LOG_TYPE::LOG_FATAL,
                                      format_str,
                                      args...);
            }
            // abort skips destructors; without this a file sink loses the fatal record
            persistTerminalRecord(loggingOps, record);
        }
        catch (...) {}
        std::abort();
    }
};  // namespace logger

#endif // LOGGER_HELPER_HPP