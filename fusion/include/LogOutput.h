// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "StringType.h"

// Interface for implementing LogOutputs for the logger.
// e.g logs could be output to a log file, or debug messages to the screen.
// The log system supports having multiple log outputs active at once.

namespace PhotonCommon {
	class LogOutput {
	public:
		virtual ~LogOutput() = default;
		virtual void LogTrace(const CharType* message) = 0;
		virtual void LogDebug(const CharType* message) = 0;
		virtual void LogInfo(const CharType* message) = 0;
		virtual void LogWarning(const CharType* message) = 0;
		virtual void LogError(const CharType* message) = 0;
	};
}
