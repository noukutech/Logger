# Logger (C++)

A lightweight, efficient, and easy-to-use logging utility library for C++ projects, developed on C++-20. This logger aims to provide essential logging features with minimal dependencies and overhead, suitable for both small and large scale applications.

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Installation](#installation)
- [Configuration](#configuration)
- [Usage](#usages)
- [Tests](#tests)
- [Documentation](#documentation)
- [Extension](#extension)
- [Bonus](#bonus)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

## Overview

This Logger library offers simple logging capabilities, including multiple log levels, formatted output, and timestamping. It is designed to be lightweight, performant, and straightforward to integrate into any C++ project (min version C++-20) without complicated setup or heavy dependencies. Supported platforms (64-bit only): macOS (arm64, x86_64), Linux (x86_64, arm64) and Windows (x86_64, via MinGW-w64 under MSYS2 UCRT64; MSVC is not supported because the build is a GNU makefile).

## Features

- Multiple log severity levels: ENTRY, EXIT, DEBUG, INFO, WARN, ERROR, ASSERT, FATAL.
- Timestamped logs with configurable time formats.
- Support for console output and file output (either one at a time).
- Customizable log message format.
- Thread-safe logging to prevent message interleaving in multithreaded applications.
- Minimal dependencies — uses only the C++ standard library (`std::format` for formatting).
- Simple APIs for quick logging.
- Configurable log message format.
- Easy to integrate with existing projects.
- Supports both static and shared library builds.
- In addition you also get a Clock class to get the current time in various formats along with a Timer to measure elapsed time. (optional)
- A FileOps class to handle file operations like reading, writing and many more. (optional)
- A ConsoleOps class to handle logging msgs to console directly. (optional)
- Unit tests using Google Test framework to ensure reliability.

```log
|20250825_155051| 0x16bd8f000| ProducerConsumer.cpp|   31|INF<< [Producer : produce] 
|20250825_155051| 0x16be1b000| ProducerConsumer.cpp|   28|INF>  [Producer : produce] Producer[13] produces data[5] while running in thread 0x16be1b000
|20250825_155051| 0x16be1b000| ProducerConsumer.cpp|   31|INF<< [Producer : produce] 
|20250825_155051| 0x16bea7000| ProducerConsumer.cpp|   28|INF>  [Producer : produce] Producer[14] produces data[64] while running in thread 0x16bea7000
|20250825_155051| 0x16bea7000| ProducerConsumer.cpp|   31|INF<< [Producer : produce] 
|20250825_155051| 0x16bf33000| ProducerConsumer.cpp|   50|INF>  [Consumer : consume] Consumer[1] consumes data[86] while running in thread 0x16bf33000
|20250825_155051| 0x16bfbf000| ProducerConsumer.cpp|   50|INF>  [Consumer : consume] Consumer[2] consumes data[66] while running in thread 0x16bfbf000
|20250825_155051| 0x16c04b000| ProducerConsumer.cpp|   50|INF>  [Consumer : consume] Consumer[3] consumes data[7] while running in thread 0x16c04b000
|20250825_155051| 0x16c0d7000| ProducerConsumer.cpp|   50|INF>  [Consumer : consume] Consumer[4] consumes data[46] while running in thread 0x16c0d7000
|20250825_155051| 0x16c163000| ProducerConsumer.cpp|   50|INF>  [Consumer : consume] Consumer[5] consumes data[59] while running in thread 0x16c163000
|20250825_155051| 0x16b817000| ProducerConsumer.cpp|   28|INF>  [Producer : produce] Producer[2] produces data[27] while running in thread 0x16b817000
```

**Column details:**

| Field            | Description                                               |
|------------------|-----------------------------------------------------------|
| Date Time        | Timestamp when the log entry was generated (e.g., `20250713_180700`) |
| Thread ID        | ID of the thread that produced the log (e.g., `140332865914560`) |
| File Name        | Source file where the log was generated (e.g., `src/ProducerConsumer.cpp`) |
| Line Number      | Line number in the source file that issued the log (e.g., `28`) |
| Log Level        | Severity or level of the log (e.g., `INF/ERR/WARN/FATAL`)               |
| Log Marker       | Additional marker or tag related to log context (e.g., `>>` indicates function/code block entry, `<<` indicates exit, `>` indicates msg) |
| Class & Method   | Class name and method/function name generating the log (e.g., `Producer : produce`) |
| Message          | User-defined log message content                            |

## Installation

### Prerequisits

- A C++20 compiler and standard library with `<format>` support: GCC 13+ (libstdc++ 13+), or Clang with a `<format>`-capable library (libc++ 17+, or libstdc++ 13+). Tested: Apple clang 21 (libc++), GCC 14 and Clang 18 on Ubuntu 24.04 (libstdc++ 14), and MSYS2 UCRT64 GCC/Clang on Windows
- make for building the library (make based; `clang++` by default, override with e.g. `make CXX=g++-14`)
- On macOS, Homebrew include/lib paths are picked up automatically (`brew --prefix`), so Homebrew's `googletest` works on Apple Silicon without extra flags
- On Windows, build inside an MSYS2 UCRT64 shell with `make`, `mingw-w64-ucrt-x86_64-gcc` (or `-clang`) and `mingw-w64-ucrt-x86_64-gtest` installed
- Google Test framework for running tests (optional, but recommended)

Clone the repository:

```bash
git clone https://github.com/noukutech/Logger.git
cd Logger
```

### Build the library and install (manual)

Run the following commands to build the library:

```bash
make clean
make
```

> **Note**: The library will be built as a static library by default. You can change the `LIB_TYPE` variable in the `makefile` to `shared` if you want to build it as a shared library.
> Release build will be created by default. You can change the `BUILD_TYPE` variable in the `makefile` to `debug` if you want to build it as a debug library. `all` target will build both release and debug libraries.
> By default Tests are not built. You can change the `BUILD_TEST` variable in the `makefile` to `yes` to build the tests.
> If you want to do this all in one line rather than changing the `makefile`, you can run the following command:

```bash
make clean; make LIB_TYPE=shared BUILD_TYPE=debug BUILD_TEST=yes
```

#### Install the library

To install the library, run the following commands from the repository root after building (this is what `./scripts/install_lib.sh` does; adjust the install locations if needed):

```bash
LIB_DIR="lib"                                  # built libraries
HEADER_DIR="include"                           # public headers
INSTALL_LIB_DIR="/usr/local/lib"
INSTALL_INCLUDE_DIR="/usr/local/include/logger"

# Create target directories if not exist
sudo mkdir -p "$INSTALL_LIB_DIR"
sudo mkdir -p "$INSTALL_INCLUDE_DIR"

# Copy library file recursively
sudo cp -r "$LIB_DIR"/* "$INSTALL_LIB_DIR"

# Copy header files recursively
sudo cp -r "$HEADER_DIR"/* "$INSTALL_INCLUDE_DIR"
```

### Build the library and install (automatic)

You can also run the provided script to install the library automatically:

```bash
./buildNinstall.sh -BUILD_TYPE=release -LIB_TYPE=shared -FILE_LOGGING=yes -LOG_FILE_NAME=IntegrationTesting.log
```

Where `buildNinstall.sh` is a script that will build the library and install it to the default locations. You can also pass the following options to the script:

- `-BUILD_TYPE`: Set the build type (release or debug). Default is release.
- `-LIB_TYPE`: Set the library type (static or shared). Default is static.
- `-FILE_LOGGING`: Enable file logging. Default is no.
- `-LOG_FILE_NAME`: Set the log file name. There is no default: without it the logger uses console output, even with `-FILE_LOGGING=yes`.
- `-BUILD_TEST`: Enable building tests. Default is no.

> **Note**: The default installation locations are:
>
> - Library files: `/usr/local/lib`
> - Header files: `/usr/local/include/logger`

For more details on the script, you can run

```bash
./buildNinstall.sh -h
```

## Configuration

You can configure the logger by modifying the `Logger.hpp` file or by passing configuration options during build and installation. The logger supports various configuration options such as cconsole logging/file logging, log file name (if file logging is enabled), max log file size, msg format, and more.

Please refer to buildNinstall.sh script for more details on the available configuration options or alternatively run

```bash
./buildNinstall.sh -h
```

## Usages

To use the Logger in your C++ project, follow these steps:

1. Include the header file in your source code:

```cpp
#include <logger/LOGGER_MACROS.hpp>
```

1. Use the logger macros to log messages at different levels in your code. For example:

```cpp
#include <logger/LOGGER_MACROS.hpp>

using namespace logger;   // the macros call logger:: helpers unqualified

void Consumer::consume()
{
    LOG_ENTRY();
    std::unique_lock<std::mutex> lock(m_mtx);
    m_cv.wait(lock, []{ return !m_dataQueue.empty() || m_isDataReady; });
    m_consumedDataQ.emplace(m_dataQueue.front());
    m_dataQueue.pop();
    std::ostringstream oss;
    oss << std::endl << "Consumer[" << getID() << "] ";
    oss << "consumes data[" << m_consumedDataQ.front() << "] ";
    oss << "while running in thread " << std::this_thread::get_id() << "\n" << std::endl;
    LOG_INFO(oss.str());
    m_consumedDataQ.pop();
    lock.unlock();
    m_cv.notify_all();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    LOG_EXIT();
}
```

or

```cpp
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
```

## Tests

The library is having numerous unit test cases which uses `Google Unit test framework`. If you have built the test app too while building then you can run the test cases
by running following command:

```bash
./bin/TestLogger_d --gtest_shuffle --gtest_repeat=3
```

- `--gtest_shuffle` Shuffles the test cases in each iteration (optional)
- `--gtest_repeat=3` Repeats the test cases at least 3 times (optional)

Sanitizer builds: pass the flags through `CXX` and use separate output directories so objects are never reused across variants:

```bash
make OBJ_DIR=build/tsan/obj LIB_DIR=build/tsan/lib BIN_DIR=build/tsan/bin TEST_OBJ_DIR=build/tsan/obj/test \
     CXX="clang++ -fsanitize=thread" BUILD_TYPE=debug BUILD_TEST=yes
./build/tsan/bin/TestLogger_d --gtest_shuffle --gtest_repeat=5
```

`tests/fatal_flush_check.sh [compiler]` (run from the repo root) builds small file-logging programs and checks that `LOG_FATAL`/`LOG_ASSERT` records reach the log file before the process aborts or exits. The gtest death tests cannot observe this because they log to the console.

CI (`.github/workflows/ci.yml`) runs the suite on Linux (gcc/clang, ASan+LSan, TSan, shared), macOS (arm64 and Intel, ASan, TSan) and Windows (MinGW-w64 gcc/clang).

![Test cases run](docs/TestCases.png)

## Documentation

For detailed documentation on the Logger library, including API references, configuration options, and examples, please generate the documentation using Doxygen. You can find the Doxygen configuration file in the root directory of the project.
To generate the documentation, run the following command:

```bash
doxygen Doxyfile
```

The generated `.html` files will be under `docs` folder which also contains a rudimentary class diagram of this library for a quick look.

## Extension

If you want different level/style of logging you can easily extend the code by deriving your own classes from FileOps and/or ConsoleOps.
All the important methods are virtual in nature so that can be easily extended.

## Bonus

Apprat from easy to use logging macros this library also provides three handy classes namely

1. FileOps - For common/standard file related operations like creating a new file, writing it, deleting a file, ammending and many more.
To know how to use that class you can have a look into `FileOpsTest.cpp` under the `test` folder. For example:

```cpp
TEST_F(FileOpsTests, testReadWrite)
{
    std::uintmax_t maxFileSize = 1024 * 1000;
    std::uintmax_t maxTextSize = 255;
    auto fileName = generateRandomFileName();
    FileOps file(maxFileSize, fileName);
    auto text = generateRandomText(maxTextSize);
    file.write(text);
    ASSERT_TRUE(file.fileExists());
    EXPECT_FALSE(file.isEmpty());
    file.readFile();
    auto fileContents = file.getFileContent();
    ASSERT_FALSE(fileContents.empty());
    auto data = fileContents.front();
    EXPECT_EQ(text, data);
    ASSERT_TRUE(file.deleteFile());
}
```

2. ConsoleOps - For writing data into standard console. For example

```cpp
TEST_F(ConsoleOpsTest, testWriteToConsole)
{
    std::uintmax_t maxTextSize = 255;
    auto text = generateRandomText(maxTextSize);
    ConsoleOpsTestClass testObj;
    testObj.setTestingModeOn();
    testObj.write(text);
    auto& result = testObj.getTestStringStreamFromConsole();
    text += "\n";   // A new line character as the outstream object will have one as well
    EXPECT_EQ(text, result.str());
    testObj.setTestingModeOff();
}
```

3. Clock - A utility to perform various clock related ops like getting cuurent local time, GMT time, day of the week etc. For examples:

```cpp
{
    std::string format = "%Y-%m-%d %H:%M:%S";
    auto expLocalTimeStr = clock.getLocalTimeStr(format);
    auto localTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    EXPECT_STREQ(expLocalTimeStr.c_str(), formattedLocalTime(format, &localTime).c_str());
}

TEST_F(ClockTests, testGetDayOfWeek)
{
    auto expDayOfWeek = clock.getDayOfWeek();
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto localTime = std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(localTime, "%A");
    EXPECT_STREQ(expDayOfWeek.c_str(), oss.str().c_str());
}
```

## Contributing

Contributions, issues, and feature requests are welcome! Please follow these steps:

1. Fork this repository.
2. Create a feature branch (`git checkout -b feature-name`).
3. Commit your changes (`git commit -m "Add feature"`).
4. Push to the branch (`git push origin feature-name`).
5. Open a pull request.

Please ensure your code follows consistent style and includes tests where applicable.

> **NOTE:** If you are adding new code then back it by respective test case/s and make sure it doesn't break the existing ones.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contact

Maintainer: Nouku Technologies
GitHub: [noukutech](https://github.com/noukutech)

---

*Efficient C++ logging made simple. Feel free to open issues for bugs or feature requests.*
