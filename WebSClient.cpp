#include "pch.h"
#include "WebSClient.h"
#include "Logger.h"
#include "signalrclient/hub_connection_builder.h"
#include "signalrclient/signalr_client_config.h"
#include <algorithm>
#include <cmath>

extern "C" {
#include "lauxlib.h"
}

namespace WebS {

WebSClient& WebSClient::instance() {
    static WebSClient instance;
    return instance;
}

WebSClient::~WebSClient() {
    shutdown();
}

void WebSClient::setStatus(ConnectionStatus status) {
    status_.store(status);
}

ConnectionStatus WebSClient::status() const {
    return status_.load();
}

void WebSClient::setLuaState(lua_State* L) {
    luaState_ = L;
}

lua_State* WebSClient::luaState() const {
    return luaState_;
}

EventManager& WebSClient::events() {
    return eventManager_;
}

void WebSClient::setReconnectConfig(const ReconnectConfig& config) {
    std::lock_guard<std::mutex> lock(reconnectMutex_);
    reconnectConfig_ = config;
}

ReconnectConfig WebSClient::reconnectConfig() const {
    std::lock_guard<std::mutex> lock(reconnectMutex_);
    return reconnectConfig_;
}

int WebSClient::reconnectAttempts() const {
    return reconnectAttempts_.load();
}

int WebSClient::calculateBackoffDelay(int attempt) {
    std::lock_guard<std::mutex> lock(reconnectMutex_);
    if (attempt < 0) attempt = 0;
    if (attempt > 20) attempt = 20;
    double delay = reconnectConfig_.initialDelayMs * std::pow(reconnectConfig_.multiplier, attempt);
    if (delay > static_cast<double>(reconnectConfig_.maxDelayMs) || delay < 0) {
        return reconnectConfig_.maxDelayMs;
    }
    return static_cast<int>(delay);
}

bool WebSClient::connect(const std::string& url, const std::string& token) {
    std::string tempUrl = url;

    size_t schemePos = tempUrl.find("://");
    if (schemePos == std::string::npos) {
        tempUrl = "https://" + tempUrl;
    } else {
        std::string scheme = tempUrl.substr(0, schemePos);
        std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
        if (scheme != "http" && scheme != "https") {
            Logger::instance().error("Invalid URL scheme: '" + scheme + "'");
            return false;
        }
    }

    ConnectionStatus currentStatus = status_.load();

    if (currentStatus == ConnectionStatus::CONNECTED) {
        Logger::instance().error("Already connected");
        return false;
    }

    if (currentStatus != ConnectionStatus::DISCONNECTED) {
        Logger::instance().info("Stopping current connection attempt...");
        stopThread_ = true;
    }

    if (connectionThread_) {
        if (connectionThread_->joinable()) {
            connectionThread_->join();
        }
        connectionThread_.reset();
    }

    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connection_ = nullptr;
        currentUrl_ = tempUrl;
        currentToken_ = token;
    }

    setStatus(ConnectionStatus::DISCONNECTED);
    stopThread_ = false;
    destroyed_ = false;
    reconnectAttempts_ = 0;
    reconnecting_ = false;

    try {
        connectionThread_ = std::make_unique<std::thread>(&WebSClient::connectionThreadFunc, this, tempUrl, token);
    } catch (const std::exception& e) {
        Logger::instance().error("Failed to start connection thread: " + std::string(e.what()));
        return false;
    }

    return true;
}

void WebSClient::registerServerMethod(const std::string& methodName) {
    std::lock_guard<std::mutex> lock(serverMethodsMutex_);
    registeredServerMethods_.insert(methodName);
}

void WebSClient::unregisterServerMethod(const std::string& methodName) {
    std::lock_guard<std::mutex> lock(serverMethodsMutex_);
    registeredServerMethods_.erase(methodName);
}

void WebSClient::registerAllServerMethods(signalr::hub_connection& conn) {
    std::lock_guard<std::mutex> lock(serverMethodsMutex_);
    for (const auto& methodName : registeredServerMethods_) {
        conn.on(methodName, [this, methodName](const std::vector<signalr::value>& args) {
            if (destroyed_.load()) return;
            serverMessageQueue_.push({ methodName, args });
        });
    }
}

void WebSClient::connectionThreadFunc(std::string urlStr, std::string tokenStr) {
    std::shared_ptr<signalr::hub_connection> threadConnection = nullptr;

    try {
        setStatus(ConnectionStatus::CONNECTING);
        Logger::instance().info("Connecting to: " + urlStr);

        auto newConnection = signalr::hub_connection_builder::create(urlStr)
            .with_logging(Logger::getShared(), signalr::trace_level::verbose)
            .build();

        if (!tokenStr.empty()) {
            signalr::signalr_client_config config;
            config.get_http_headers().emplace("Authorization", tokenStr);
            config.set_proxy({ web::web_proxy::use_auto_discovery });
            newConnection.set_client_config(config);
        }

        newConnection.set_disconnected([this](std::exception_ptr ex) {
            handleDisconnected(ex);
        });

        registerAllServerMethods(newConnection);

        std::atomic<bool> connection_started{ false };
        std::atomic<bool> connection_failed{ false };

        newConnection.start([&connection_started, &connection_failed](std::exception_ptr exception) {
            if (exception) {
                connection_failed = true;
            } else {
                connection_started = true;
            }
        });

        auto start_time = std::chrono::steady_clock::now();
        while (!connection_started.load() && !connection_failed.load()) {
            if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(15)) {
                throw std::runtime_error("Connection timeout");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (connection_failed.load()) {
            throw std::runtime_error("Failed to start connection");
        }

        {
            std::lock_guard<std::mutex> lock(connectionMutex_);
            threadConnection = std::make_shared<signalr::hub_connection>(std::move(newConnection));
            connection_ = threadConnection;
            destroyed_ = false;
        }

        setStatus(ConnectionStatus::CONNECTED);
        reconnectAttempts_ = 0;
        reconnecting_ = false;
        Logger::instance().success("Connected successfully to hub.");
        eventManager_.emit("OnConnect");

        while (!stopThread_.load() && status_.load() == ConnectionStatus::CONNECTED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    catch (const std::exception& e) {
        setStatus(ConnectionStatus::DISCONNECTED);
        eventManager_.emit("OnError", { "Exception: " + std::string(e.what()) });
        Logger::instance().error("ConnectionThreadFunc exception: " + std::string(e.what()));

        if (!stopThread_.load()) {
            attemptReconnect();
            return;
        }
    }
    catch (...) {
        setStatus(ConnectionStatus::DISCONNECTED);
        eventManager_.emit("OnError", { "Unknown exception" });
        Logger::instance().error("ConnectionThreadFunc unknown exception");
    }

    if (threadConnection) {
        setStatus(ConnectionStatus::DISCONNECTING);
        Logger::instance().info("Stopping connection...");

        std::promise<void> stopPromise;
        auto stopFuture = stopPromise.get_future();

        threadConnection->stop([&stopPromise](std::exception_ptr) {
            stopPromise.set_value();
        });

        if (stopFuture.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            Logger::instance().error("Connection stop timed out");
        }
    }

    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connection_ = nullptr;
    }
    threadConnection = nullptr;
    destroyed_ = true;

    setStatus(ConnectionStatus::DISCONNECTED);
    Logger::instance().info("Connection thread finished.");
}

void WebSClient::handleDisconnected(std::exception_ptr ex) {
    if (destroyed_.load() || status_.load() == ConnectionStatus::DISCONNECTED) {
        return;
    }

    if (ex) {
        setStatus(ConnectionStatus::DISCONNECTED);
        eventManager_.emit("OnError", { "Disconnected due to an error" });
        Logger::instance().error("Disconnected due to an error.");
    } else {
        setStatus(ConnectionStatus::DISCONNECTED);
        eventManager_.emit("OnDisconnect");
    }

    if (!stopThread_.load() && ex) {
        attemptReconnect();
    }
}

void WebSClient::attemptReconnect() {
    ReconnectConfig config;
    {
        std::lock_guard<std::mutex> lock(reconnectMutex_);
        config = reconnectConfig_;
    }

    if (!config.enabled) {
        return;
    }

    if (reconnecting_.load()) {
        return;
    }

    reconnecting_ = true;

    std::string url, token;
    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        url = currentUrl_;
        token = currentToken_;
    }

    while (!stopThread_.load()) {
        int attempts = reconnectAttempts_.fetch_add(1) + 1;

        if (config.maxAttempts > 0 && attempts > config.maxAttempts) {
            Logger::instance().error("Max reconnection attempts reached");
            eventManager_.emit("OnError", { "Max reconnection attempts reached" });
            reconnecting_ = false;
            return;
        }

        int delay = calculateBackoffDelay(attempts - 1);
        Logger::instance().info("Reconnecting in " + std::to_string(delay) + "ms (attempt " + std::to_string(attempts) + ")");

        setStatus(ConnectionStatus::RECONNECTING);
        eventManager_.emit("OnReconnecting", { std::to_string(attempts) });

        std::this_thread::sleep_for(std::chrono::milliseconds(delay));

        if (stopThread_.load()) {
            break;
        }

        try {
            setStatus(ConnectionStatus::CONNECTING);
            Logger::instance().info("Reconnecting to: " + url);

            auto newConnection = signalr::hub_connection_builder::create(url)
                .with_logging(Logger::getShared(), signalr::trace_level::verbose)
                .build();

            if (!token.empty()) {
                signalr::signalr_client_config cfg;
                cfg.get_http_headers().emplace("Authorization", token);
                cfg.set_proxy({ web::web_proxy::use_auto_discovery });
                newConnection.set_client_config(cfg);
            }

            newConnection.set_disconnected([this](std::exception_ptr ex) {
                handleDisconnected(ex);
            });

            registerAllServerMethods(newConnection);

            std::atomic<bool> started{ false };
            std::atomic<bool> failed{ false };

            newConnection.start([&started, &failed](std::exception_ptr ex) {
                if (ex) failed = true;
                else started = true;
            });

            auto start_time = std::chrono::steady_clock::now();
            while (!started.load() && !failed.load()) {
                if (stopThread_.load()) {
                    reconnecting_ = false;
                    return;
                }
                if (std::chrono::steady_clock::now() - start_time > std::chrono::seconds(15)) {
                    throw std::runtime_error("Connection timeout");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            if (failed.load()) {
                throw std::runtime_error("Failed to reconnect");
            }

            {
                std::lock_guard<std::mutex> lock(connectionMutex_);
                connection_ = std::make_shared<signalr::hub_connection>(std::move(newConnection));
                destroyed_ = false;
            }

            setStatus(ConnectionStatus::CONNECTED);
            reconnectAttempts_ = 0;
            reconnecting_ = false;
            Logger::instance().success("Reconnected successfully.");
            eventManager_.emit("OnReconnected");

            while (!stopThread_.load() && status_.load() == ConnectionStatus::CONNECTED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            return;

        } catch (const std::exception& e) {
            Logger::instance().error("Reconnect failed: " + std::string(e.what()));
            continue;
        }
    }

    reconnecting_ = false;
}

void WebSClient::disconnect() {
    if (status_.load() == ConnectionStatus::DISCONNECTED) {
        return;
    }

    stopThread_ = true;
    Logger::instance().info("Disconnect requested.");
}

std::string WebSClient::connectionId() const {
    std::lock_guard<std::mutex> lock(connectionMutex_);

    if (!connection_ || status_.load() != ConnectionStatus::CONNECTED) {
        return "";
    }

    try {
        return connection_->get_connection_id();
    } catch (...) {
        return "";
    }
}

bool WebSClient::send(const std::string& method, const std::vector<signalr::value>& args) {
    if (status_.load() != ConnectionStatus::CONNECTED) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        if (!connection_) {
            return false;
        }

        connection_->invoke(method, args, [](const signalr::value&, std::exception_ptr e) {
            if (e) {
                Logger::instance().error("SendMessage invoke callback reported failure.");
            }
        });
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("Send failed: " + std::string(e.what()));
        return false;
    }
}

bool WebSClient::sendAsync(const std::string& method, const std::vector<signalr::value>& args, int callbackRef) {
    if (status_.load() != ConnectionStatus::CONNECTED) {
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        if (!connection_) {
            return false;
        }

        connection_->invoke(method, args, [this, callbackRef](const signalr::value& result, std::exception_ptr e) {
            if (destroyed_.load()) {
                return;
            }
            AsyncResult res;
            res.callbackRef = callbackRef;
            res.success = !e;
            if (e) {
                res.error = "Invoke failed";
                Logger::instance().error("SendMessageAsync invoke callback reported failure.");
            } else {
                res.result = result;
            }
            asyncResultsQueue_.push(res);
        });
        return true;
    } catch (const std::exception& e) {
        Logger::instance().error("SendAsync failed: " + std::string(e.what()));
        return false;
    }
}

std::string WebSClient::getMessage() {
    std::string msg;
    if (messageQueue_.tryPop(msg)) {
        return msg;
    }
    return "";
}

size_t WebSClient::queueSize() const {
    return messageQueue_.size();
}

int WebSClient::processEvents(lua_State* L) {
    if (!L || destroyed_.load() || stopThread_.load()) {
        return 0;
    }

    int processed = eventManager_.processEvents(L);

    std::queue<ServerMessage> serverMsgsToProcess;
    {
        std::queue<ServerMessage> temp;
        serverMessageQueue_.swap(temp);
        serverMsgsToProcess = std::move(temp);
    }

    while (!serverMsgsToProcess.empty()) {
        ServerMessage msg = std::move(serverMsgsToProcess.front());
        serverMsgsToProcess.pop();

        std::vector<std::string> strArgs;
        for (const auto& arg : msg.args) {
            if (arg.is_string()) {
                strArgs.push_back(arg.as_string());
            } else if (arg.is_double()) {
                strArgs.push_back(std::to_string(arg.as_double()));
            } else if (arg.is_bool()) {
                strArgs.push_back(arg.as_bool() ? "true" : "false");
            }
        }

        eventManager_.emit(msg.method, strArgs);
        processed++;
    }

    processed += eventManager_.processEvents(L);

    std::queue<AsyncResult> resultsToProcess;
    {
        std::queue<AsyncResult> temp;
        asyncResultsQueue_.swap(temp);
        resultsToProcess = std::move(temp);
    }

    while (!resultsToProcess.empty()) {
        AsyncResult res = std::move(resultsToProcess.front());
        resultsToProcess.pop();

        if (res.callbackRef != LUA_NOREF && res.callbackRef != -1) {
            if (!lua_checkstack(L, 10)) {
                Logger::instance().error("Lua stack overflow risk in async callback");
                luaL_unref(L, LUA_REGISTRYINDEX, res.callbackRef);
                continue;
            }

            int top = lua_gettop(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, res.callbackRef);

            if (lua_isfunction(L, -1)) {
                lua_pushboolean(L, res.success);

                if (res.success) {
                    pushSignalRValueToLua(L, res.result);
                } else {
                    lua_pushstring(L, res.error.c_str());
                }

                if (lua_pcall(L, 2, 0, 0) != 0) {
                    const char* err = lua_tostring(L, -1);
                    Logger::instance().error("Error in async callback: " + std::string(err ? err : "unknown"));
                }
            } else {
                Logger::instance().error("Async callback ref is not a function!");
            }

            lua_settop(L, top);
            luaL_unref(L, LUA_REGISTRYINDEX, res.callbackRef);
            processed++;
        }
    }

    return processed;
}

void WebSClient::shutdown() {
    destroyed_ = true;
    stopThread_ = true;

    if (status_.load() != ConnectionStatus::DISCONNECTED) {
        try {
            std::lock_guard<std::mutex> lock(connectionMutex_);
            if (connection_) {
                connection_->stop([](std::exception_ptr) {});
            }
        } catch (...) {}
    }

    if (connectionThread_ && connectionThread_->joinable()) {
        connectionThread_->join();
    }
    connectionThread_.reset();

    {
        std::lock_guard<std::mutex> lock(connectionMutex_);
        connection_ = nullptr;
    }

    if (luaState_) {
        eventManager_.clear(luaState_);
    }

    messageQueue_.clear();
    asyncResultsQueue_.clear();
    serverMessageQueue_.clear();

    setStatus(ConnectionStatus::DISCONNECTED);
}

static void pushSignalRValueToLuaImpl(lua_State* L, const signalr::value& val, int depth) {
    if (depth > 50) {
        Logger::instance().error("SignalR value too deeply nested");
        lua_pushnil(L);
        return;
    }

    switch (val.type()) {
        case signalr::value_type::string:
            lua_pushstring(L, val.as_string().c_str());
            break;
        case signalr::value_type::float64:
            lua_pushnumber(L, val.as_double());
            break;
        case signalr::value_type::boolean:
            lua_pushboolean(L, val.as_bool());
            break;
        case signalr::value_type::null:
            lua_pushnil(L);
            break;
        case signalr::value_type::array: {
            const auto& arr = val.as_array();
            if (!lua_checkstack(L, 3)) {
                lua_pushnil(L);
                return;
            }
            lua_newtable(L);
            for (size_t i = 0; i < arr.size(); ++i) {
                pushSignalRValueToLuaImpl(L, arr[i], depth + 1);
                lua_rawseti(L, -2, static_cast<int>(i + 1));
            }
            break;
        }
        case signalr::value_type::map: {
            const auto& map = val.as_map();
            if (!lua_checkstack(L, 4)) {
                lua_pushnil(L);
                return;
            }
            lua_newtable(L);
            for (const auto& pair : map) {
                lua_pushstring(L, pair.first.c_str());
                pushSignalRValueToLuaImpl(L, pair.second, depth + 1);
                lua_settable(L, -3);
            }
            break;
        }
        case signalr::value_type::binary: {
            const auto& bin = val.as_binary();
            lua_pushlstring(L, reinterpret_cast<const char*>(bin.data()), bin.size());
            break;
        }
        default:
            lua_pushnil(L);
            break;
    }
}

void pushSignalRValueToLua(lua_State* L, const signalr::value& val) {
    pushSignalRValueToLuaImpl(L, val, 0);
}

} // namespace WebS
