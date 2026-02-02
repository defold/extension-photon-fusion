// Copyright Exit Games GmbH. All Rights Reserved.

#ifndef SHAREDCLIENT_LOGOUTPUT_H
#define SHAREDCLIENT_LOGOUTPUT_H

// Interface for implementing LogOutputs for the logger.
// e.g logs could be output to a log file, or debug messages to the screen.
// The log system supports having multiple log outputs active at once.

namespace SharedMode::Logging {
	class LogOutput {
	public:
		virtual ~LogOutput() = default;
		virtual void LogTrace(const wchar_t* message) = 0;
		virtual void LogDebug(const wchar_t* message) = 0;
		virtual void LogInfo(const wchar_t* message) = 0;
		virtual void LogWarning(const wchar_t* message) = 0;
		virtual void LogError(const wchar_t* message) = 0;
	};
}


#endif // SHAREDCLIENT_LOGOUTPUT_H
