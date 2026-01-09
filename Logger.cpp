#include "pch.h"
#include "Logger.h"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <algorithm>

namespace WebS {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

std::shared_ptr<Logger> Logger::getShared() {
    return std::shared_ptr<Logger>(&instance(), [](Logger*) {});
}

void Logger::setMinLevel(LogLevel level) {
    minLevel_.store(level);
}

LogLevel Logger::minLevel() const {
    return minLevel_.load();
}

void Logger::critical(const std::string& msg) {
    log(LogLevel::Critical, msg);
}

void Logger::error(const std::string& msg) {
    log(LogLevel::Error, msg);
}

void Logger::warning(const std::string& msg) {
    log(LogLevel::Warning, msg);
}

void Logger::info(const std::string& msg) {
    log(LogLevel::Info, msg);
}

void Logger::debug(const std::string& msg) {
    log(LogLevel::Debug, msg);
}

void Logger::verbose(const std::string& msg) {
    log(LogLevel::Verbose, msg);
}

void Logger::success(const std::string& msg) {
    log(LogLevel::Info, "[SUCCESS] " + msg);
}

void Logger::luaError(const std::string& eventName, const std::string& err) {
    log(LogLevel::Error, "[LUA] Event '" + eventName + "': " + err);
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream logfile(filename_, std::ios::trunc);
    if (logfile.is_open()) {
        logfile.close();
    }
}

void Logger::log(LogLevel level, const std::string& msg) {
    if (level > minLevel_.load() || minLevel_.load() == LogLevel::None) {
        return;
    }
    logInternal(LogLevelToString(level), msg);
}

void Logger::write(const std::string& entry) {
    std::string levelStr;
    std::string cleanMsg;
    parseSignalRMessage(entry, levelStr, cleanMsg);

    LogLevel level = parseSignalRLevel(levelStr);
    if (level > minLevel_.load() || minLevel_.load() == LogLevel::None) {
        return;
    }

    logInternal("signalr", cleanMsg);
}

LogLevel Logger::parseSignalRLevel(const std::string& levelStr) {
    std::string lower = levelStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "critical" || lower == "crit") return LogLevel::Critical;
    if (lower == "error" || lower == "err") return LogLevel::Error;
    if (lower == "warning" || lower == "warn") return LogLevel::Warning;
    if (lower == "information" || lower == "info") return LogLevel::Info;
    if (lower == "debug" || lower == "dbg") return LogLevel::Debug;
    if (lower == "trace" || lower == "verbose") return LogLevel::Verbose;

    return LogLevel::Verbose;
}

void Logger::parseSignalRMessage(const std::string& entry, std::string& outLevel, std::string& outMessage) {
    outLevel = "info";
    outMessage = entry;

    if (entry.empty()) return;

    size_t levelStart = entry.find('[');
    size_t levelEnd = entry.find(']', levelStart);

    if (levelStart != std::string::npos && levelEnd != std::string::npos && levelEnd > levelStart) {
        std::string levelWithBrackets = entry.substr(levelStart + 1, levelEnd - levelStart - 1);
        size_t lastNonSpace = levelWithBrackets.find_last_not_of(" ");
        if (lastNonSpace != std::string::npos) {
            outLevel = levelWithBrackets.substr(0, lastNonSpace + 1);
        } else if (!levelWithBrackets.empty()) {
            outLevel = levelWithBrackets;
        }

        size_t msgStart = levelEnd + 1;
        while (msgStart < entry.length() && entry[msgStart] == ' ') {
            msgStart++;
        }

        if (msgStart < entry.length()) {
            outMessage = entry.substr(msgStart);
        }
    }
}

std::string Logger::formatLevel(const std::string& level) {
    std::string formatted = level;
    while (formatted.length() < 9) {
        formatted += " ";
    }
    return formatted;
}

void Logger::logInternal(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream logfile(filename_, std::ios::app);
    if (logfile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        struct tm timeinfo;
        localtime_s(&timeinfo, &time);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

        std::string cleanMsg = msg;
        while (!cleanMsg.empty() && (cleanMsg.back() == '\n' || cleanMsg.back() == '\r')) {
            cleanMsg.pop_back();
        }

        logfile << "[" << buffer << "." << std::setfill('0') << std::setw(3) << ms.count()
                << "] [" << formatLevel(level) << "] " << cleanMsg << std::endl;
    }
}

} // namespace WebS
