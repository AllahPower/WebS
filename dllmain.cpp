#include "pch.h"

#define LUA_LIB
#define LUA_BUILD_AS_DLL

extern "C" {
#include "./lauxlib.h"
#include "./lua.h"
}

class logger : public signalr::log_writer
{
public:
    virtual void write(const std::string& entry) override
    {
        std::ofstream logfile("websocketLogging.txt", std::ios::app);
        if (logfile.is_open()) {
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            struct tm timeinfo;
            localtime_s(&timeinfo, &time);

            char buffer[100];
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
            logfile << "[" << buffer << "] " << entry << std::endl;
        }
    }

    void logInfo(const std::string& msg) { write("[INFO] " + msg); }
    void logError(const std::string& msg) { write("[ERROR] " + msg); }
    void logSuccess(const std::string& msg) { write("[SUCCESS] " + msg); }
};

struct LuaEvent {
    std::string name;
    std::string arg;
};

enum class ConnectionStatus {
    DISCONNECTED = 0,
    CONNECTING = 1,
    CONNECTED = 2,
    _ERROR = 3
};

std::queue<std::string> messageQueue;
std::mutex messageQueueMutex;

std::queue<LuaEvent> eventQueue;
std::mutex eventQueueMutex;

std::shared_ptr<signalr::hub_connection> connection = nullptr;
ConnectionStatus connectionStatus = ConnectionStatus::DISCONNECTED;
std::string url;
std::string token;
lua_State* g_LuaState = nullptr;
std::thread* connectionThread = nullptr;
std::atomic<bool> stopThread{ false };

std::mutex connectionMutex;

void SetStatus(ConnectionStatus status) {
    connectionStatus = status;
}

const char* GetStatusString(ConnectionStatus status) {
    switch (status) {
    case ConnectionStatus::DISCONNECTED:
        return "disconnected";
    case ConnectionStatus::CONNECTING:
        return "connecting";
    case ConnectionStatus::CONNECTED:
        return "connected";
    case ConnectionStatus::_ERROR:
        return "error";
    default:
        return "disconnected";
    }
}

bool IsConnected() {
    return connectionStatus == ConnectionStatus::CONNECTED;
}

void PushEvent(const std::string& eventName, const std::string& arg = "") {
    std::lock_guard<std::mutex> lock(eventQueueMutex);
    eventQueue.push({ eventName, arg });
}

void CallLuaEvent(const LuaEvent& event) {
    if (!g_LuaState) return;

    lua_getglobal(g_LuaState, "WebS");
    if (!lua_istable(g_LuaState, -1)) {
        lua_pop(g_LuaState, 1);
        return;
    }

    lua_getfield(g_LuaState, -1, event.name.c_str());
    if (!lua_isfunction(g_LuaState, -1)) {
        lua_pop(g_LuaState, 2);
        return;
    }

    int argsCount = 0;
    if (!event.arg.empty()) {
        lua_pushstring(g_LuaState, event.arg.c_str());
        argsCount = 1;
    }

    if (lua_pcall(g_LuaState, argsCount, 0, 0) != 0) {
        const char* err = lua_tostring(g_LuaState, -1);
        lua_pop(g_LuaState, 1);

        std::ofstream logfile("websocketLogging.txt", std::ios::app);
        if (logfile.is_open()) {
            logfile << "[LUA ERROR] In event " << event.name << ": " << err << std::endl;
        }
    }

    lua_pop(g_LuaState, 1);
}

static void handleDisconnected(std::exception_ptr ex)
{
    if (connectionStatus == ConnectionStatus::DISCONNECTED)
    {
        return;
    }

    if (ex)
    {
        SetStatus(ConnectionStatus::_ERROR);
        PushEvent("OnError", "Disconnected due to an error");

        std::ofstream logfile("websocketLogging.txt", std::ios::app);
        if (logfile.is_open()) {
            logfile << "[ERROR] Disconnected due to an error." << std::endl;
        }
    }
    else
    {
        SetStatus(ConnectionStatus::DISCONNECTED);
        PushEvent("OnDisconnect");
    }
}

static void ConnectionThreadFunc(std::string urlStr, std::string tokenStr)
{
    std::shared_ptr<signalr::hub_connection> threadConnection = nullptr;

    try {
        SetStatus(ConnectionStatus::CONNECTING);

        auto newConnection = signalr::hub_connection_builder::create(urlStr)
            .with_logging(std::make_shared<logger>(), signalr::trace_level::verbose)
            .build();

        if (!tokenStr.empty())
        {
            signalr::signalr_client_config config;
            config.get_http_headers().emplace("Authorization", tokenStr);
            config.set_proxy({ web::web_proxy::use_auto_discovery });
            newConnection.set_client_config(config);
        }

        newConnection.set_disconnected(handleDisconnected);

        newConnection.on("PendingMessage", [](const std::vector<signalr::value>& m)
            {
                if (!m.empty() && m[0].is_string()) {
                    std::lock_guard<std::mutex> lock(messageQueueMutex);
                    messageQueue.push(m[0].as_string());
                }
            });

        std::atomic<bool> connection_started{ false };
        std::atomic<bool> connection_failed{ false };

        newConnection.start([&connection_started, &connection_failed](std::exception_ptr exception) {
            if (exception) {
                connection_failed = true;
            }
            else {
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
            throw std::runtime_error("failed during start");
        }

        {
            std::lock_guard<std::mutex> lock(connectionMutex);
            threadConnection = std::make_shared<signalr::hub_connection>(std::move(newConnection));
            connection = threadConnection;
        }

        SetStatus(ConnectionStatus::CONNECTED);
        PushEvent("OnConnect");

        while (!stopThread.load() && connectionStatus == ConnectionStatus::CONNECTED) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    catch (const std::exception& e) {
        SetStatus(ConnectionStatus::_ERROR);
        PushEvent("OnError", "Exception due to " + std::string(e.what()));
        std::ofstream logfile("websocketLogging.txt", std::ios::app);
        if (logfile.is_open()) logfile << "[ERROR] " << e.what() << std::endl;
    }
    catch (...) {
        SetStatus(ConnectionStatus::_ERROR);
        PushEvent("OnError", "Unknown exception");
    }

    if (threadConnection) {
        threadConnection->stop([](std::exception_ptr) {});

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        connection = nullptr;
        threadConnection = nullptr;
    }

    SetStatus(ConnectionStatus::DISCONNECTED);
}


static int ConnectWS(lua_State* L)
{
    int numArgs = lua_gettop(L);

    if (numArgs > 0 && numArgs < 3) {
        if (lua_isstring(L, 1)) {
            url = lua_tostring(L, 1);
            token = (numArgs == 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "";

            if (connectionStatus == ConnectionStatus::CONNECTING ||
                connectionStatus == ConnectionStatus::CONNECTED) {
                lua_pushboolean(L, false);
                lua_pushstring(L, "Already connecting or connected");
                return 2;
            }

            if (connectionThread != nullptr) {
                if (connectionThread->joinable()) {
                    connectionThread->join();
                }
                delete connectionThread;
                connectionThread = nullptr;
            }

            stopThread = false;

            try {
                connectionThread = new std::thread(ConnectionThreadFunc, url, token);
            }
            catch (const std::exception& e) {
                lua_pushboolean(L, false);
                lua_pushstring(L, e.what());
                return 2;
            }

            lua_pushboolean(L, true);
            return 1;
        }
        else {
            return luaL_error(L, "ConnectWS: URL must be a string");
        }
    }
    else {
        return luaL_error(L, "ConnectWS: One or Two arguments expected (url, token)");
    }
}

static int GetMessageWS(lua_State* L)
{
    std::lock_guard<std::mutex> lock(messageQueueMutex);
    if (messageQueue.empty()) {
        lua_pushstring(L, "");
    }
    else {
        std::string out = messageQueue.front();
        messageQueue.pop();
        lua_pushstring(L, out.c_str());
    }
    return 1;
}

static int GetQueueSizeWS(lua_State* L)
{
    std::lock_guard<std::mutex> lock(messageQueueMutex);
    lua_pushinteger(L, (int)messageQueue.size());
    return 1;
}

static int GetStatusWS(lua_State* L)
{
    lua_pushstring(L, GetStatusString(connectionStatus));
    return 1;
}

static int SendMessageWS(lua_State* L)
{
    int numArgs = lua_gettop(L);

    if (numArgs != 2) {
        return luaL_error(L, "Usage: SendMessage(methodName, argsTable)");
    }

    if (!lua_isstring(L, 1) || !lua_istable(L, 2)) {
        return luaL_error(L, "Arguments must be (string, table)");
    }

    if (!IsConnected()) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Not connected");
        return 2;
    }

    const char* methodName = lua_tostring(L, 1);
    int arraySize = lua_objlen(L, 2);

    std::vector<signalr::value> args;
    for (int i = 1; i <= arraySize; ++i) {
        lua_rawgeti(L, 2, i);
        if (lua_isstring(L, -1)) {
            args.push_back(lua_tostring(L, -1));
        }
        else if (lua_isnumber(L, -1)) {
            args.push_back(lua_tonumber(L, -1));
        }
        else if (lua_isboolean(L, -1)) {
            args.push_back(lua_toboolean(L, -1) != 0);
        }
        else {
            lua_pop(L, 1);
            lua_pushboolean(L, false);
            lua_pushstring(L, "Unsupported argument type in table");
            return 2;
        }
        lua_pop(L, 1);
    }

    try {
        std::lock_guard<std::mutex> lock(connectionMutex);
            
        if (!connection || connectionStatus != ConnectionStatus::CONNECTED) {
            lua_pushboolean(L, false);
            lua_pushstring(L, "Connection not ready");
            return 2;
        }

        connection->invoke(methodName, args, [](const signalr::value&, std::exception_ptr e) {
            if (e) {
                try {
                    std::rethrow_exception(e);
                }
                catch (const std::exception& ex) {
                    std::ofstream logfile("websocketLogging.txt", std::ios::app);
                    if (logfile.is_open()) {
                        logfile << "[ERROR] Send failed: " << ex.what() << std::endl;
                    }
                }
            }
            });

        lua_pushboolean(L, true);
        return 1;
    }
    catch (const std::exception& e) {
        lua_pushboolean(L, false);
        lua_pushstring(L, e.what());
        return 2;
    }
    catch (...) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "Unknown error while sending");
        return 2;
    }
}

static int DisconnectWS(lua_State* L)
{
    if (connectionStatus == ConnectionStatus::DISCONNECTED) {
        lua_pushboolean(L, true);
        return 1;
    }

    stopThread = true;

    lua_pushboolean(L, true);
    return 1;
}

static int ProcessEventsWS(lua_State* L)
{
    std::queue<LuaEvent> eventsToProcess;

    {
        std::lock_guard<std::mutex> lock(eventQueueMutex);
        eventsToProcess.swap(eventQueue);
    }

    int processed = 0;
    while (!eventsToProcess.empty()) {
        LuaEvent event = eventsToProcess.front();
        eventsToProcess.pop();

        CallLuaEvent(event);
        processed++;
    }

    lua_pushinteger(L, processed);
    return 1;
}

static struct luaL_reg ls_lib[] = {
    { "Connect", ConnectWS },
    { "GetMessage", GetMessageWS },
    { "GetQueueSize", GetQueueSizeWS },
    { "GetStatus", GetStatusWS },
    { "SendMessage", SendMessageWS },
    { "Disconnect", DisconnectWS },
    { "ProcessEvents", ProcessEventsWS },
    { NULL, NULL }
};

extern "C" LUALIB_API int luaopen_WebS(lua_State* L) {
    g_LuaState = L;
    luaL_openlib(L, "WebS", ls_lib, 0);
    return 0;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        break;

    case DLL_PROCESS_DETACH:
        if (connectionStatus != ConnectionStatus::DISCONNECTED) {
            try {
                std::lock_guard<std::mutex> lock(connectionMutex);
                if (connection) {
                    connection->stop([](std::exception_ptr) {});
                }
            }
            catch (...) {
            }
        }

        if (connectionThread != nullptr) {
            if (connectionThread->joinable()) {
                connectionThread->join();
            }
            delete connectionThread;
            connectionThread = nullptr;
        }
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}