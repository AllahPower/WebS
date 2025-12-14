#include "pch.h"

#define LUA_LIB
#define LUA_BUILD_AS_DLL

extern "C" {
#include "./lauxlib.h"
#include "./lua.h"
}

class GlobalLogger : public signalr::log_writer
{
public:
	virtual void write(const std::string& entry) override
	{
		std::string level;
		std::string cleanMsg;
		parseSignalRMessage(entry, level, cleanMsg);
		logInternal(level, cleanMsg);
	}

	void logInfo(const std::string& msg) { logInternal("info", msg); }
	void logError(const std::string& msg) { logInternal("error", msg); }
	void logSuccess(const std::string& msg) { logInternal("success", msg); }
	void logLuaError(const std::string& eventName, const std::string& err) {
		logInternal("lua_error", "Event '" + eventName + "': " + err);
	}

private:
	std::mutex _mutex;
	const std::string _fileName = "websocketLogging.txt";

	std::string formatLevel(const std::string& level)
	{
		std::string formatted = level;
		while (formatted.length() < 9) {
			formatted += " ";
		}
		return formatted;
	}

	void parseSignalRMessage(const std::string& entry, std::string& outLevel, std::string& outMessage)
	{
		outLevel = "info";
		outMessage = entry;

		size_t levelStart = entry.find('[');
		size_t levelEnd = entry.find(']', levelStart);

		if (levelStart != std::string::npos && levelEnd != std::string::npos) {
			std::string levelWithBrackets = entry.substr(levelStart + 1, levelEnd - levelStart - 1);
			outLevel = levelWithBrackets;
			outLevel.erase(outLevel.find_last_not_of(" ") + 1);

			size_t msgStart = levelEnd + 1;
			while (msgStart < entry.length() && entry[msgStart] == ' ') {
				msgStart++;
			}

			if (msgStart < entry.length()) {
				outMessage = entry.substr(msgStart);
			}
		}
	}

	void logInternal(const std::string& level, const std::string& msg)
	{
		std::lock_guard<std::mutex> lock(_mutex);

		std::ofstream logfile(_fileName, std::ios::app);
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
};

struct LuaEvent {
	std::string name;
	std::string arg;
};

enum class ConnectionStatus {
	DISCONNECTED = 0,
	CONNECTING = 1,
	CONNECTED = 2,
	DISCONNECTING = 3
};

std::queue<std::string> messageQueue;
std::mutex messageQueueMutex;

std::queue<LuaEvent> eventQueue;
std::mutex eventQueueMutex;

std::shared_ptr<GlobalLogger> g_Logger = nullptr;

std::shared_ptr<signalr::hub_connection> connection = nullptr;
ConnectionStatus connectionStatus = ConnectionStatus::DISCONNECTED;
std::string url;
std::string token;
lua_State* g_LuaState = nullptr;
std::thread* connectionThread = nullptr;
std::atomic<bool> stopThread{ false };
std::atomic<bool> connectionDestroyed{ false };

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
	case ConnectionStatus::DISCONNECTING:
		return "disconnecting";
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
		g_Logger->logLuaError(event.name, err);
	}

	lua_pop(g_LuaState, 1);
}

static void handleDisconnected(std::exception_ptr ex)
{
	if (connectionDestroyed.load() || connectionStatus == ConnectionStatus::DISCONNECTED) {
		return;
	}

	if (ex) {
		SetStatus(ConnectionStatus::DISCONNECTED);
		PushEvent("OnError", "Disconnected due to an error");
		g_Logger->logError("Disconnected due to an error.");
	}
	else {
		SetStatus(ConnectionStatus::DISCONNECTED);
		PushEvent("OnDisconnect");
	}
}

static void ConnectionThreadFunc(std::string urlStr, std::string tokenStr)
{
	std::shared_ptr<signalr::hub_connection> threadConnection = nullptr;

	try {
		SetStatus(ConnectionStatus::CONNECTING);
		g_Logger->logInfo("Connecting to: " + urlStr);

		auto newConnection = signalr::hub_connection_builder::create(urlStr)
			.with_logging(g_Logger, signalr::trace_level::verbose)
			.build();

		if (!tokenStr.empty()) {
			signalr::signalr_client_config config;
			config.get_http_headers().emplace("Authorization", tokenStr);
			config.set_proxy({ web::web_proxy::use_auto_discovery });
			newConnection.set_client_config(config);
		}

		newConnection.set_disconnected(handleDisconnected);

		newConnection.on("PendingMessage", [](const std::vector<signalr::value>& m) {
			if (connectionDestroyed.load()) return;
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
			throw std::runtime_error("Failed to start connection");
		}

		{
			std::lock_guard<std::mutex> lock(connectionMutex);
			threadConnection = std::make_shared<signalr::hub_connection>(std::move(newConnection));
			connection = threadConnection;
			connectionDestroyed = false;
		}

		SetStatus(ConnectionStatus::CONNECTED);
		g_Logger->logSuccess("Connected successfully to hub.");
		PushEvent("OnConnect");

		while (!stopThread.load() && connectionStatus == ConnectionStatus::CONNECTED) {
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
	}
	catch (const std::exception& e) {
		SetStatus(ConnectionStatus::DISCONNECTED);
		PushEvent("OnError", "Exception: " + std::string(e.what()));
		g_Logger->logError("ConnectionThreadFunc exception: " + std::string(e.what()));
	}
	catch (...) {
		SetStatus(ConnectionStatus::DISCONNECTED);
		PushEvent("OnError", "Unknown exception");
		g_Logger->logError("ConnectionThreadFunc unknown exception");
	}

	if (threadConnection) {
		SetStatus(ConnectionStatus::DISCONNECTING);
		g_Logger->logInfo("Stopping connection...");
		threadConnection->stop([](std::exception_ptr) {});
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	{
		std::lock_guard<std::mutex> lock(connectionMutex);
		connection = nullptr;
		threadConnection = nullptr;
		connectionDestroyed = true;
	}

	SetStatus(ConnectionStatus::DISCONNECTED);
	g_Logger->logInfo("Connection thread finished.");
}

static int ConnectWS(lua_State* L)
{
	int numArgs = lua_gettop(L);

	if (numArgs > 0 && numArgs < 3) {
		if (lua_isstring(L, 1)) {
			std::string tempUrl = lua_tostring(L, 1);
			token = (numArgs == 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "";

			size_t schemePos = tempUrl.find("://");

			if (schemePos == std::string::npos) {
				tempUrl = "https://" + tempUrl;
			}
			else {
				std::string scheme = tempUrl.substr(0, schemePos);
				std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);

				if (scheme != "http" && scheme != "https") {
					lua_pushboolean(L, false);
					std::string err = "Invalid URL scheme: '" + scheme + "'. Only 'http' and 'https' are supported.";
					lua_pushstring(L, err.c_str());
					g_Logger->logError("ConnectWS Error: " + err);
					return 2;
				}
			}

			url = tempUrl;

			if (connectionStatus == ConnectionStatus::CONNECTING) {
				lua_pushboolean(L, false);
				lua_pushstring(L, "Already connecting");
				return 2;
			}

			if (connectionStatus == ConnectionStatus::CONNECTED) {
				lua_pushboolean(L, false);
				lua_pushstring(L, "Already connected");
				return 2;
			}

			if (connectionStatus == ConnectionStatus::DISCONNECTING) {
				lua_pushboolean(L, false);
				lua_pushstring(L, "Disconnecting in progress");
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
			connectionDestroyed = false;

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

static int GetConnectionIdWS(lua_State* L)
{
	std::lock_guard<std::mutex> lock(connectionMutex);

	if (!connection || connectionStatus != ConnectionStatus::CONNECTED) {
		lua_pushstring(L, "");
		return 1;
	}

	try {
		std::string connId = connection->get_connection_id();
		lua_pushstring(L, connId.c_str());
		return 1;
	}
	catch (const std::exception& e) {
		g_Logger->logError("GetConnectionId failed: " + std::string(e.what()));
		lua_pushstring(L, "");
		return 1;
	}
	catch (...) {
		lua_pushstring(L, "");
		return 1;
	}
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
				g_Logger->logError("SendMessage invoke callback reported failure.");
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
	g_Logger->logInfo("Disconnect requested via Lua.");

	lua_pushboolean(L, true);
	return 1;
}

static int ProcessEventsWS(lua_State* L)
{
	if (connectionDestroyed.load() || stopThread.load()) {
		lua_pushinteger(L, 0);
		return 1;
	}

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
	{ "GetConnectionId", GetConnectionIdWS },
	{ "SendMessage", SendMessageWS },
	{ "Disconnect", DisconnectWS },
	{ "ProcessEvents", ProcessEventsWS },
	{ NULL, NULL }
};

extern "C" LUALIB_API int luaopen_WebS(lua_State* L) {
	if (!g_Logger) {
		g_Logger = std::make_shared<GlobalLogger>();
	}

	g_Logger->logInfo("WebS DLL Loaded and Initialized.");
	g_LuaState = L;
	luaL_openlib(L, "WebS", ls_lib, 0);
	return 0;
}

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
		HMODULE hSignalR = LoadLibraryA("microsoft-signalr.dll");
		HMODULE hCppRest = LoadLibraryA("cpprest_2_10.dll");

		if (!hSignalR) {
			std::ofstream logfile("websocketLogging.txt", std::ios::app);
			if (logfile.is_open()) {
				logfile << "[CRITICAL ERROR] Could not load microsoft-signalr.dll! Check game folder." << std::endl;
			}
		}
		break;
	}

	case DLL_PROCESS_DETACH:
		connectionDestroyed = true;

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

		g_Logger->logInfo("WebS DLL Unloading.");

		if (connectionThread != nullptr) {
			if (connectionThread->joinable()) {
				connectionThread->join();
			}
			delete connectionThread;
			connectionThread = nullptr;
		}

		g_Logger = nullptr;
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	}
	return TRUE;
}