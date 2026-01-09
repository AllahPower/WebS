#pragma once

#include <string>
#include <mutex>
#include <memory>
#include "signalrclient/hub_connection.h"

namespace WebS {

class Logger : public signalr::log_writer {
public:
    static Logger& instance();

    // signalr::log_writer interface
    void write(const std::string& entry) override;

    // Logging methods
    void info(const std::string& msg);
    void error(const std::string& msg);
    void success(const std::string& msg);
    void debug(const std::string& msg);
    void luaError(const std::string& eventName, const std::string& err);

    // Get shared pointer for SignalR builder
    static std::shared_ptr<Logger> getShared();

    // Disable copy
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger() = default;

    void logInternal(const std::string& level, const std::string& msg);
    void parseSignalRMessage(const std::string& entry, std::string& outLevel, std::string& outMessage);
    std::string formatLevel(const std::string& level);

    std::mutex mutex_;
    const std::string filename_ = "websocketLogging.txt";
};

} // namespace WebS
