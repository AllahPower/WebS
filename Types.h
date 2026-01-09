#pragma once

#include <string>
#include <vector>
#include "signalrclient/signalr_value.h"

namespace WebS {

enum class ConnectionStatus {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    DISCONNECTING = 3,
    RECONNECTING = 4
};

inline const char* ConnectionStatusToString(ConnectionStatus status) {
    switch (status) {
        case ConnectionStatus::DISCONNECTED: return "disconnected";
        case ConnectionStatus::CONNECTING: return "connecting";
        case ConnectionStatus::CONNECTED: return "connected";
        case ConnectionStatus::DISCONNECTING: return "disconnecting";
        case ConnectionStatus::RECONNECTING: return "reconnecting";
        default: return "disconnected";
    }
}

struct AsyncResult {
    int callbackRef = -1;
    signalr::value result;
    std::string error;
    bool success = false;
};

struct LuaEvent {
    std::string name;
    std::vector<std::string> args;
};

struct ReconnectConfig {
    bool enabled = false;
    int maxAttempts = 5;           // 0 = infinite
    int initialDelayMs = 1000;     // Initial delay in ms
    int maxDelayMs = 30000;        // Maximum delay in ms
    float multiplier = 2.0f;       // Exponential backoff multiplier
};

} // namespace WebS
