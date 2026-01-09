#pragma once

#include <string>
#include <mutex>
#include <memory>
#include <atomic>
#include "signalrclient/hub_connection.h"

namespace WebS {

enum class LogLevel {
    None = 0,
    Critical = 1,
    Error = 2,
    Warning = 3,
    Info = 4,
    Debug = 5,
    Verbose = 6
};

inline const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::None: return "none";
        case LogLevel::Critical: return "critical";
        case LogLevel::Error: return "error";
        case LogLevel::Warning: return "warning";
        case LogLevel::Info: return "info";
        case LogLevel::Debug: return "debug";
        case LogLevel::Verbose: return "verbose";
        default: return "unknown";
    }
}

inline LogLevel StringToLogLevel(const std::string& str) {
    if (str == "none") return LogLevel::None;
    if (str == "critical") return LogLevel::Critical;
    if (str == "error") return LogLevel::Error;
    if (str == "warning") return LogLevel::Warning;
    if (str == "info") return LogLevel::Info;
    if (str == "debug") return LogLevel::Debug;
    if (str == "verbose") return LogLevel::Verbose;
    return LogLevel::Info;
}

class Logger : public signalr::log_writer {
public:
    static Logger& instance();
    static std::shared_ptr<Logger> getShared();

    void write(const std::string& entry) override;

    void setMinLevel(LogLevel level);
    LogLevel minLevel() const;

    void critical(const std::string& msg);
    void error(const std::string& msg);
    void warning(const std::string& msg);
    void info(const std::string& msg);
    void debug(const std::string& msg);
    void verbose(const std::string& msg);

    void success(const std::string& msg);
    void luaError(const std::string& eventName, const std::string& err);

    void clear();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger() = default;

    void log(LogLevel level, const std::string& msg);
    void logInternal(const std::string& level, const std::string& msg);
    void parseSignalRMessage(const std::string& entry, std::string& outLevel, std::string& outMessage);
    LogLevel parseSignalRLevel(const std::string& levelStr);
    std::string formatLevel(const std::string& level);

    std::mutex mutex_;
    std::atomic<LogLevel> minLevel_{ LogLevel::Info };
    const std::string filename_ = "websocketLogging.txt";
};

} // namespace WebS
