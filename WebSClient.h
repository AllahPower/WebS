#pragma once

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include "Types.h"
#include "ThreadSafeQueue.h"
#include "EventManager.h"
#include "signalrclient/hub_connection.h"

extern "C" {
#include "lua.h"
}

namespace WebS {

struct ServerMessage {
    std::string method;
    std::vector<signalr::value> args;
};

class WebSClient {
public:
    static WebSClient& instance();

    bool connect(const std::string& url, const std::string& token = "");
    void disconnect();
    ConnectionStatus status() const;
    std::string connectionId() const;

    void setReconnectConfig(const ReconnectConfig& config);
    ReconnectConfig reconnectConfig() const;
    int reconnectAttempts() const;

    bool send(const std::string& method, const std::vector<signalr::value>& args);
    bool sendAsync(const std::string& method, const std::vector<signalr::value>& args, int callbackRef);
    std::string getMessage();
    size_t queueSize() const;

    void registerServerMethod(const std::string& methodName);
    void unregisterServerMethod(const std::string& methodName);

    EventManager& events();
    int processEvents(lua_State* L);

    void setLuaState(lua_State* L);
    lua_State* luaState() const;
    void shutdown();

    WebSClient(const WebSClient&) = delete;
    WebSClient& operator=(const WebSClient&) = delete;

private:
    WebSClient() = default;
    ~WebSClient();

    void connectionThreadFunc(std::string url, std::string token);
    void handleDisconnected(std::exception_ptr ex);
    void attemptReconnect();
    int calculateBackoffDelay(int attempt);
    void setStatus(ConnectionStatus status);
    void registerAllServerMethods(signalr::hub_connection& conn);

    std::atomic<ConnectionStatus> status_{ConnectionStatus::DISCONNECTED};
    std::shared_ptr<signalr::hub_connection> connection_;
    lua_State* luaState_ = nullptr;
    std::string currentUrl_;
    std::string currentToken_;

    ReconnectConfig reconnectConfig_;
    std::atomic<int> reconnectAttempts_{0};
    std::atomic<bool> reconnecting_{false};
    mutable std::mutex reconnectMutex_;

    std::unique_ptr<std::thread> connectionThread_;
    std::atomic<bool> stopThread_{false};
    std::atomic<bool> destroyed_{false};
    mutable std::mutex connectionMutex_;

    ThreadSafeQueue<std::string> messageQueue_;
    ThreadSafeQueue<AsyncResult> asyncResultsQueue_;
    ThreadSafeQueue<ServerMessage> serverMessageQueue_;

    std::set<std::string> registeredServerMethods_;
    mutable std::mutex serverMethodsMutex_;

    EventManager eventManager_;
};

void pushSignalRValueToLua(lua_State* L, const signalr::value& val);

} // namespace WebS
