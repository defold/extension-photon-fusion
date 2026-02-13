// Copyright Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_LOGUTILS_C
#define SHAREDCLIENT_LOGUTILS_C

#include <cstdint>
// ReSharper disable once CppUnusedIncludeDirective
#include <cstddef>

#include "StringType.h"

namespace SharedMode::Logging {
    enum LogLevel : uint8_t {
        Trace = 1 << 0,
        Debug = 1 << 1,
        Info = 1 << 2,
        Warning = 1 << 3,
        Error = 1 << 4
    };

    bool TryGetLogLevelFromString(const CharType *logLevelString, LogLevel &outLogLevel);

    void SetLogLevelsFromBitmask(uint8_t logLevelMask);

    void LogEnable(LogLevel logLevel);

    void LogDisable(LogLevel logLevel);

    bool IsLogEnabled(LogLevel logLevel);

    void LogLogLevels();

    // Add a log output implementation.
    void AddLogOutput(class LogOutput *logOutput);

    // Removes a log output implementation.
    // Returns true/false depending if LogOutput exists and could be removed.
    bool RemoveLogOutput(LogOutput *logOutput);

    size_t LogOutputCount();

    void Log(LogLevel logLevel, const CharType* Log);
}

#endif // SHAREDCLIENT_LOGUTILS_C
