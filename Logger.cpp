#include "pch.h"
#include "Logger.h"
#include <fstream>
#include <chrono>
#include <iomanip>

namespace WebS {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

std::shared_ptr<Logger> Logger::getShared() {
    // Return a shared_ptr that doesn't delete the singleton
    return std::shared_ptr<Logger>(&instance(), [](Logger*) {});
}

void Logger::write(const std::string& entry) {
    std::string level;
    std::string cleanMsg;
    parseSignalRMessage(entry, level, cleanMsg);
    logInternal(level, cleanMsg);
}

void Logger::info(const std::string& msg) {
    logInternal("info", msg);
}

void Logger::error(const std::string& msg) {
    logInternal("error", msg);
}

void Logger::success(const std::string& msg) {
    logInternal("success", msg);
}

void Logger::debug(const std::string& msg) {
    logInternal("debug", msg);
}

void Logger::luaError(const std::string& eventName, const std::string& err) {
    logInternal("lua_error", "Event '" + eventName + "': " + err);
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
        struct tm timeinfo;
        localtime_s(&timeinfo, &time);

        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

        std::string cleanMsg = msg;
        while (!cleanMsg.empty() && (cleanMsg.back() == '\n' || cleanMsg.back() == '\r')) {
            cleanMsg.pop_back();
        }

        logfile << "[" << buffer << "] [" << formatLevel(level) << "] " << cleanMsg << std::endl;
    }
}

} // namespace WebS
