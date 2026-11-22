//leaks --atExit --list -- ./bin/TestLogger_d --gtest_shuffle --gtest_repeat=3 --gtest_filter="LoggerTest.*"
//leaks --atExit --list -- ./bin/TestLogger_d --gtest_shuffle --gtest_repeat=3 --gtest_filter=LoggerTest.testForStdFunctionObject
//valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind.log ./bin/TestLogger_d

#include "CommonFunc.hpp"
#include "Logger.hpp"
#include "LOGGER_MACROS.hpp"
#include "FileOps.hpp"
#include "ConsoleOps.hpp"

#include <type_traits>
#include <future>
#include <functional>
#include <any>

using namespace logger;

class LoggerTest : public CommonTestDataGenerator
{
    public:
        std::vector<std::string_view> logTypeStringVec = 
            { "ERR", "INF", "WARN", "DBG", "FATAL", "ASRT", "DEFAULT" };

        std::vector<logger::LOG_TYPE> logTypeVec = 
        {   LOG_TYPE::LOG_ERR,
            LOG_TYPE::LOG_INFO,
            LOG_TYPE::LOG_WARN,
            LOG_TYPE::LOG_DBG,
            LOG_TYPE::LOG_FATAL,
            LOG_TYPE::LOG_ASSERT,
            LOG_TYPE::LOG_DEFAULT
        };
        static void assertionDeathTest()
        {
            LOG_ASSERT(2 > 3);
        }
        static void assertionMsgDeathTest()
        {
            LOG_ASSERT(0.62f < -0.51f, "Death at the end");
        }
        static void fatalDeathTest()
        {
            LOG_FATAL("FATAL error at line {}", __LINE__);
        }
        static void testLoggedData(
            const LOG_TYPE& expLogType,
            const std::string_view prettyFuncName,
            const std::string_view marker,
            const std::string_view logMsg = "") noexcept
        {
            auto& logStream = loggerObj.getLogStream();
            std::ostringstream oss;
            oss << std::this_thread::get_id();
            EXPECT_TRUE(logStream.str().find(oss.str()) != std::string::npos)
                << "oss.str() = " << oss.str() << ", " << "logStream.str() = " << logStream.str();
            std::string fileName = __FILE__;
            //Remove the parent directory name from the file name (if any)
            auto dirPos = fileName.rfind(FORWARD_SLASH);
            if (std::string::npos != dirPos)
            {
                fileName = fileName.substr(dirPos + 1);
            }
            std::ostringstream().swap(oss);
            oss << fileName;
            EXPECT_TRUE(logStream.str().find(oss.str()) != std::string::npos)
                << "oss.str() = " << oss.str() << ", " << "logStream.str() = " << logStream.str();

            EXPECT_TRUE(logStream.str().find(Logger::covertLogTypeEnumToString(expLogType)) != std::string::npos);
            ASSERT_TRUE(std::string::npos != prettyFuncName.find(":"));
            auto className = prettyFuncName.substr(0, prettyFuncName.find_first_of(":"));
            className = className.substr(className.rfind(" ") + 1);
            auto funcNameWithoutClassName = prettyFuncName.substr(prettyFuncName.find_last_of(":") + 1);
            funcNameWithoutClassName = funcNameWithoutClassName.substr(0, funcNameWithoutClassName.find_first_of("("));

            EXPECT_TRUE(logStream.str().find(className) != std::string::npos) << "className = " << className << std::endl;
            EXPECT_TRUE(logStream.str().find(funcNameWithoutClassName) != std::string::npos) << "funcNameWithoutClassName = " << funcNameWithoutClassName;
            EXPECT_TRUE(logStream.str().find(marker) != std::string::npos);

            if (!logMsg.empty())
            {
                EXPECT_TRUE(logStream.str().find(logMsg) != std::string::npos);
            }
        }
        static int* funcReturningPointer(const int val1, const int val2) noexcept
        {
            LOG_ENTRY();
            static std::unique_ptr<int> pRetVal = std::make_unique<int>(0);
            *pRetVal = val1 * val2;    // static init runs once; assign every call so results don't leak across tests
            LOG_EXIT();
            return pRetVal.get();
        }
        static std::unique_ptr<std::string> funcReturnUniquePtr() noexcept
        {
            LOG_ENTRY();
            LOG_EXIT();
            return std::make_unique<std::string>("LoggerTesting");
        }
        static std::unique_ptr<int> funcReturnIntUniquePtr(const int val1, const int val2) noexcept
        {
            LOG_ENTRY();
            LOG_EXIT();
            return std::make_unique<int>(val1 * val2);
        }
        template <typename F, typename ...Args>
        static std::any submit(F&& f, Args&&... args)
        {
            LOG_ENTRY_DBG();
            using Result = std::invoke_result_t<F, Args...>;
            auto boundFunc = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            // Wrap the bound function in a packaged_task that returns std::any
            // The lambda inside the packaged_task handles both void and non-void return types
            // by checking if Result is void at compile time. Use constructor as assignment or
            // copy initialization does not work with std::packaged_task.
            std::packaged_task<std::any()> packagedTask(
            [boundFunc]() -> std::any
            {
                if constexpr (std::is_void_v<Result>)
                {
                    boundFunc();
                    return std::any{};
                }
                else
                {
                    return boundFunc();
                }
            });
            std::any result;
            if (packagedTask.valid())
            {
                packagedTask();
                auto future = packagedTask.get_future();
                if (future.valid())
                    result = future.get();
                else
                    result = std::any{};
            }
            LOG_EXIT_DBG();
            return result;
        }
};

class LoggerTestObj final : public Logger
{
    LoggerTestObj() = delete;
    LoggerTestObj(const std::string_view timeFormat)
        : Logger(timeFormat)
    {}

    ~LoggerTestObj() = default;
    LoggerTestObj(const LoggerTestObj& rhs) = delete;
    LoggerTestObj(LoggerTestObj&& rhs) = delete;
    LoggerTestObj& operator=(const LoggerTestObj& rhs) = delete;
    LoggerTestObj& operator=(LoggerTestObj&& rhs) = delete;
};

TEST_F(LoggerTest, testLogTypeStringToEnum)
{
    for (size_t idx = 0; idx < logTypeVec.size(); ++idx)
        EXPECT_EQ(logTypeVec[idx], Logger::convertStringToLogTypeEnum(logTypeStringVec[idx]));
}

TEST_F(LoggerTest, testLogTypeEnumToString)
{
    for (size_t idx = 0; idx < logTypeVec.size(); ++idx)
        EXPECT_EQ(Logger::covertLogTypeEnumToString(logTypeVec[idx]), logTypeStringVec[idx]);
}

TEST_F(LoggerTest, testLogList)
{
    std::uintmax_t maxTextSize = 10;
    std::vector<std::string> dataVec;
    std::list<std::string> dataList;
    for (auto cnt = 0; cnt < 5; ++cnt)
    {
        dataVec.push_back(generateRandomText(maxTextSize));
        dataList.push_back(generateRandomText(maxTextSize));
    }

    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    LOG_LIST(dataVec, "Testing log msgs for list of size {:d}", dataVec.size());
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, FORWARD_ANGLES);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    LOG_LIST(dataList, "Testing log msgs for list of size {:d}", dataVec.size());
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
}

/**
 * @note The following three tests should be marked disabled
 * while running all the test cases for this test suite.
 * Reasone being, assertion failure testing in a multithreaded
 * enviornment is not guranteed to work as expected always without
 * causing a deadlock. Eveven with the death test style
 * set to threadsafe. So if you want to test the assertion failure,
 * you can enable these tests and run them separately in isolation for each test.
 * @see https://github.com/google/googletest/blob/main/docs/advanced.md#death-tests-and-threads
 * for more details.
 */
TEST_F(LoggerTest, testAssertion)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_DEATH(assertionDeathTest(), "");
}

TEST_F(LoggerTest, testAssertionWithMsg)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_DEATH(assertionMsgDeathTest(), "");
}

TEST_F(LoggerTest, testFatalError)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    ASSERT_DEATH(fatalDeathTest(), "");
}

TEST_F(LoggerTest, testLogEntryExit)
{
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    LOG_ENTRY();
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, FORWARD_ANGLES);
    LOG_EXIT();
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, BACKWARD_ANGLES);
}

TEST_F(LoggerTest, testLogEntryExitDebug)
{
#if defined (DEBUG) || defined (__DEBUG__)
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
#endif
    LOG_ENTRY_DBG();
#if defined (DEBUG) || defined (__DEBUG__)
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, FORWARD_ANGLES);
#endif
    LOG_EXIT_DBG();
#if defined (DEBUG) || defined (__DEBUG__)
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, BACKWARD_ANGLES);
#endif
}

TEST_F(LoggerTest, testLogEntryExitWithMsg)
{
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_INFO);
    LOG_ENTRY("Entering now {} with LOG_TYPE as {:#08x} and log marker as {}", 
        std::string("testLogEntryExitWithMsg"), logType, FORWARD_ANGLES);
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, FORWARD_ANGLES);

    LOG_EXIT("Exiting {} with LOG_TYPE as {:#08x} and log marker as {}", 
        std::string("testLogEntryExitWithMsg"), logType, BACKWARD_ANGLES);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, BACKWARD_ANGLES);
}

TEST_F(LoggerTest, testLogEntryExitWithMsgDebugMode)
{
#if defined (DEBUG) || defined (__DEBUG__)
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
#endif
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_INFO);
    LOG_ENTRY_DBG("Entering now {} with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogEntryExitWithMsg"), logType, FORWARD_ANGLES);
#if defined (DEBUG) || defined (__DEBUG__)
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, FORWARD_ANGLES);
#endif

    LOG_EXIT_DBG("Exiting {} with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogEntryExitWithMsg"), logType, BACKWARD_ANGLES);
#if defined (DEBUG) || defined (__DEBUG__)
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, BACKWARD_ANGLES);
#endif
}

TEST_F(LoggerTest, testLogWarning)
{
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_WARN);
    LOG_WARN("Going to exit {} with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogWarning"), logType, FORWARD_ANGLE);
    testLoggedData(LOG_TYPE::LOG_WARN, __PRETTY_FUNCTION__, FORWARD_ANGLE);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
}

TEST_F(LoggerTest, testLogInfo)
{
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_INFO);
    LOG_INFO("In {} with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogInfo"), logType, FORWARD_ANGLE);
    testLoggedData(LOG_TYPE::LOG_INFO, __PRETTY_FUNCTION__, FORWARD_ANGLE);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
}

TEST_F(LoggerTest, testLogError)
{
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_ERR);
    LOG_ERR("No error to exit {} with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogInfo"), logType, FORWARD_ANGLE);
    testLoggedData(LOG_TYPE::LOG_ERR, __PRETTY_FUNCTION__, FORWARD_ANGLE);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
}

TEST_F(LoggerTest, testLogImp)
{
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_IMP);
    LOG_IMP("It should be working for {} as well with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogImp"), logType, FORWARD_ANGLE);
    testLoggedData(LOG_TYPE::LOG_IMP, __PRETTY_FUNCTION__, FORWARD_ANGLE);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
}

TEST_F(LoggerTest, testLogDbg)
{
#if defined (DEBUG) || defined (__DEBUG__)
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
    auto logType = static_cast<std::underlying_type_t<LOG_TYPE>>(LOG_TYPE::LOG_DBG);
    LOG_DBG("It should be working for {} in debug mode as well with LOG_TYPE as {:#08x} and log marker as {}",
        std::string("testLogDbg"), logType, FORWARD_ANGLE);
    testLoggedData(LOG_TYPE::LOG_DBG, __PRETTY_FUNCTION__, FORWARD_ANGLE);
    std::cout << std::endl; // Put a line break so that the printed log msg can be seen clearly
#endif
}

/**
 * @brief The objective of these test features
 * is to test and polish the class name and
 * function name in the actual log message.
 * We need to look at the console or file
 * (console option is quick and better) to
 * check how the class and function names
 * are been populated in the log message
 * and trim/polish them accordingly.
 */
TEST_F(LoggerTest, testDiffFuncSignatures)
{
    EXPECT_NE(0, *funcReturningPointer(10, 10));
    EXPECT_NE(0, *funcReturnIntUniquePtr(10, 10));
    EXPECT_FALSE(funcReturnUniquePtr()->empty());
    {
        auto lamdaFunc = [](const std::string& val1, const std::string& val2)
        {
            LOG_ENTRY();
            if (val1.empty() && val2.empty())
            {
                LOG_INFO("Both the incoming string are empty");
            }
            else
            {
                LOG_INFO("Combined string results of the incoming strings {} and {} is {}", 
                    val1, val2, std::string(val1 + val2));
            }
            LOG_EXIT();
            return 0;
        };
        EXPECT_EQ(0, lamdaFunc("Testing ", "function signature"));
    }
}

TEST_F(LoggerTest, testTemplatedSubmitFunc)
{
    auto val1 = 10;
    auto val2 = 1000;
    auto result = submit(funcReturningPointer, val1, val2);
    EXPECT_EQ(val1 * val2, *(std::any_cast<int*>(result)));
}

TEST_F(LoggerTest, testFunctorSubmit)
{
    int* (*functor)(const int val1, const int val2);
    functor = &funcReturningPointer;
    auto val1 = 10;
    auto val2 = 1000;
    auto result = submit(functor, val1, val2);
    EXPECT_EQ(val1 * val2, *(std::any_cast<int*>(result)));
}

TEST_F(LoggerTest, testForStdFunctionObject)
{
    std::function<int* (const int val1, const int val2)> functor = funcReturningPointer;
    auto val1 = 10;
    auto val2 = 1000;
    auto result = submit(functor, val1, val2);
    EXPECT_EQ(val1 * val2, *(std::any_cast<int*>(result)));
}

// Regression: unknown/empty inputs dereferenced cend() (UB). Documented fallbacks are LOG_DEFAULT / "".
TEST(LoggerConversionTest, unknownLogTypeFallsBackSafely)
{
    EXPECT_EQ(LOG_TYPE::LOG_DEFAULT, Logger::convertStringToLogTypeEnum(""));
    EXPECT_EQ(LOG_TYPE::LOG_DEFAULT, Logger::convertStringToLogTypeEnum("NOT_A_TYPE"));
    EXPECT_EQ(LOG_TYPE::LOG_FATAL, Logger::convertStringToLogTypeEnum("FATAL"));
    EXPECT_EQ(std::string(), Logger::covertLogTypeEnumToString(static_cast<LOG_TYPE>(-1)));
    EXPECT_EQ(std::string("FATAL"), Logger::covertLogTypeEnumToString(LOG_TYPE::LOG_FATAL));
}

// Regression: a string_view slice was looked up as a C string, so bytes past the view leaked into the key.
TEST(LoggerConversionTest, lookupHonorsStringViewLength)
{
    const std::string backing = "FATALXYZ";
    EXPECT_EQ(LOG_TYPE::LOG_FATAL, Logger::convertStringToLogTypeEnum(std::string_view(backing).substr(0, 5)));
    const char raw[5] = {'F', 'A', 'T', 'A', 'L'};   // deliberately not null-terminated
    EXPECT_EQ(LOG_TYPE::LOG_FATAL, Logger::convertStringToLogTypeEnum(std::string_view(raw, sizeof(raw))));
}

// Regression: log() forwarded rvalues into std::make_format_args, which libstdc++ rejects at compile time.
TEST(LoggerConversionTest, logAcceptsRvalueArguments)
{
    Logger logger("%Y");
    logger.log("{} {}", 42, std::string("rvalue"));
    EXPECT_NE(std::string::npos, logger.getLogStream().str().find("42 rvalue"));
}

const void* logHelperLoggerAddressFromSecondTU();
const void* logHelperMutexAddressFromSecondTU();
const void* logHelperOpsAddressFromSecondTU();

// Regression: raceMtx/loggerObj were per-TU statics while inline templates using them could be
// merged across TUs, so a caller could lock one TU's mutex and format with another TU's logger.
TEST(LoggerSharedStateTest, loggingStateIsSharedAcrossTranslationUnits)
{
    EXPECT_EQ(static_cast<const void*>(&logger::loggerObj), logHelperLoggerAddressFromSecondTU());
    EXPECT_EQ(static_cast<const void*>(&logger::raceMtx), logHelperMutexAddressFromSecondTU());
    EXPECT_EQ(static_cast<const void*>(&logger::loggingOps), logHelperOpsAddressFromSecondTU());
}

// Regression: a malformed runtime format string threw std::format_error out of the logging API.
TEST(LoggerConversionTest, malformedFormatDoesNotThrow)
{
    Logger logger("%Y");
    EXPECT_NO_THROW(logger.log("broken {", 1));
    EXPECT_NE(std::string::npos, logger.getLogStream().str().find("format error"));
}
