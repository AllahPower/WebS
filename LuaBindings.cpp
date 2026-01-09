#include "pch.h"
#include "LuaBindings.h"
#include "WebSClient.h"
#include "Logger.h"
#include "Types.h"
#include "Version.h"

extern "C" {
#include "lauxlib.h"
}

namespace WebS {
	namespace LuaBindings {

		static const std::set<std::string> internalEvents = {
			"OnConnect", "OnDisconnect", "OnError", "OnReconnecting", "OnReconnected"
		};

		static bool isInternalEvent(const std::string& name) {
			return internalEvents.find(name) != internalEvents.end();
		}

		static std::vector<signalr::value> tableToArgs(lua_State* L, int index) {
			std::vector<signalr::value> args;
			int arraySize = static_cast<int>(lua_objlen(L, index));

			for (int i = 1; i <= arraySize; ++i) {
				lua_rawgeti(L, index, i);
				if (lua_isstring(L, -1)) {
					args.push_back(lua_tostring(L, -1));
				}
				else if (lua_isnumber(L, -1)) {
					args.push_back(lua_tonumber(L, -1));
				}
				else if (lua_isboolean(L, -1)) {
					args.push_back(lua_toboolean(L, -1) != 0);
				}
				lua_pop(L, 1);
			}

			return args;
		}

		int Connect(lua_State* L) {
			int numArgs = lua_gettop(L);

			if (numArgs < 1 || numArgs > 2) {
				return luaL_error(L, "Connect: One or Two arguments expected (url, [token])");
			}

			if (!lua_isstring(L, 1)) {
				return luaL_error(L, "Connect: URL must be a string");
			}

			std::string url = lua_tostring(L, 1);
			std::string token = (numArgs == 2 && lua_isstring(L, 2)) ? lua_tostring(L, 2) : "";

			bool result = WebSClient::instance().connect(url, token);
			lua_pushboolean(L, result);

			if (!result) {
				lua_pushstring(L, "Connection failed to start");
				return 2;
			}

			return 1;
		}

		int Disconnect(lua_State* L) {
			WebSClient::instance().disconnect();
			lua_pushboolean(L, true);
			return 1;
		}

		int Send(lua_State* L) {
			int numArgs = lua_gettop(L);

			if (numArgs != 2) {
				return luaL_error(L, "Usage: SendMessage(methodName, argsTable)");
			}

			if (!lua_isstring(L, 1) || !lua_istable(L, 2)) {
				return luaL_error(L, "Arguments must be (string, table)");
			}

			if (WebSClient::instance().status() != ConnectionStatus::CONNECTED) {
				lua_pushboolean(L, false);
				lua_pushstring(L, "Not connected");
				return 2;
			}

			const char* methodName = lua_tostring(L, 1);
			std::vector<signalr::value> args = tableToArgs(L, 2);

			bool result = WebSClient::instance().send(methodName, args);
			lua_pushboolean(L, result);

			if (!result) {
				lua_pushstring(L, "Send failed");
				return 2;
			}

			return 1;
		}

		int SendAsync(lua_State* L) {
			int numArgs = lua_gettop(L);

			if (numArgs < 3) {
				return luaL_error(L, "Usage: SendMessageAsync(methodName, argsTable, callback)");
			}

			if (!lua_isstring(L, 1) || !lua_istable(L, 2) || !lua_isfunction(L, 3)) {
				return luaL_error(L, "Arguments: (string, table, function)");
			}

			if (WebSClient::instance().status() != ConnectionStatus::CONNECTED) {
				lua_pushboolean(L, false);
				lua_pushstring(L, "Not connected");
				return 2;
			}

			const char* methodName = lua_tostring(L, 1);

			lua_pushvalue(L, 3);
			int callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);

			std::vector<signalr::value> args = tableToArgs(L, 2);

			bool result = WebSClient::instance().sendAsync(methodName, args, callbackRef);

			if (!result) {
				luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
				lua_pushboolean(L, false);
				lua_pushstring(L, "SendAsync failed");
				return 2;
			}

			lua_pushboolean(L, true);
			return 1;
		}

		int GetMsg(lua_State* L) {
			std::string msg = WebSClient::instance().getMessage();
			lua_pushstring(L, msg.c_str());
			return 1;
		}

		int GetQueueSize(lua_State* L) {
			lua_pushinteger(L, static_cast<int>(WebSClient::instance().queueSize()));
			return 1;
		}

		int GetStatus(lua_State* L) {
			ConnectionStatus status = WebSClient::instance().status();
			lua_pushstring(L, ConnectionStatusToString(status));
			return 1;
		}

		int GetConnectionId(lua_State* L) {
			std::string connId = WebSClient::instance().connectionId();
			lua_pushstring(L, connId.c_str());
			return 1;
		}

		int ProcessEvents(lua_State* L) {
			int processed = WebSClient::instance().processEvents(L);
			lua_pushinteger(L, processed);
			return 1;
		}

		int On(lua_State* L) {
			int numArgs = lua_gettop(L);

			if (numArgs != 2) {
				return luaL_error(L, "Usage: On(eventName, callback)");
			}

			if (!lua_isstring(L, 1) || !lua_isfunction(L, 2)) {
				return luaL_error(L, "Arguments must be (string, function)");
			}

			const char* eventName = lua_tostring(L, 1);
			std::string eventStr(eventName);

			if (!isInternalEvent(eventStr)) {
				WebSClient::instance().registerServerMethod(eventStr);
			}

			int ref = WebSClient::instance().events().on(L, eventStr, 2);

			lua_pushinteger(L, ref);
			return 1;
		}

		int Off(lua_State* L) {
			int numArgs = lua_gettop(L);

			if (numArgs != 2) {
				return luaL_error(L, "Usage: Off(eventName, callbackRef)");
			}

			if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
				return luaL_error(L, "Arguments must be (string, number)");
			}

			const char* eventName = lua_tostring(L, 1);
			int callbackRef = static_cast<int>(lua_tointeger(L, 2));

			WebSClient::instance().events().off(L, eventName, callbackRef);

			lua_pushboolean(L, true);
			return 1;
		}

		int SetReconnect(lua_State* L) {
			if (!lua_istable(L, 1)) {
				return luaL_error(L, "Usage: SetReconnect({ enabled=bool, maxAttempts=int, initialDelay=int, maxDelay=int, multiplier=float })");
			}

			ReconnectConfig config;

			lua_getfield(L, 1, "enabled");
			if (!lua_isnil(L, -1)) {
				config.enabled = lua_toboolean(L, -1) != 0;
			}
			lua_pop(L, 1);

			lua_getfield(L, 1, "maxAttempts");
			if (!lua_isnil(L, -1)) {
				config.maxAttempts = static_cast<int>(lua_tointeger(L, -1));
			}
			lua_pop(L, 1);

			lua_getfield(L, 1, "initialDelay");
			if (!lua_isnil(L, -1)) {
				config.initialDelayMs = static_cast<int>(lua_tointeger(L, -1));
			}
			lua_pop(L, 1);

			lua_getfield(L, 1, "maxDelay");
			if (!lua_isnil(L, -1)) {
				config.maxDelayMs = static_cast<int>(lua_tointeger(L, -1));
			}
			lua_pop(L, 1);

			lua_getfield(L, 1, "multiplier");
			if (!lua_isnil(L, -1)) {
				config.multiplier = static_cast<float>(lua_tonumber(L, -1));
			}
			lua_pop(L, 1);

			WebSClient::instance().setReconnectConfig(config);

			lua_pushboolean(L, true);
			return 1;
		}

		int GetReconnectAttempts(lua_State* L) {
			lua_pushinteger(L, WebSClient::instance().reconnectAttempts());
			return 1;
		}

		int SetLogLevel(lua_State* L) {
			if (!lua_isstring(L, 1)) {
				return luaL_error(L, "Usage: SetLogLevel(level) where level is 'none', 'critical', 'error', 'warning', 'info', 'debug', or 'verbose'");
			}

			const char* levelStr = lua_tostring(L, 1);
			LogLevel level = StringToLogLevel(levelStr);
			Logger::instance().setMinLevel(level);

			Logger::instance().info("Log level set to: " + std::string(LogLevelToString(level)));

			lua_pushboolean(L, true);
			return 1;
		}

		int GetLogLevel(lua_State* L) {
			LogLevel level = Logger::instance().minLevel();
			lua_pushstring(L, LogLevelToString(level));
			return 1;
		}

		int GetVersion(lua_State* L) {
			lua_pushstring(L, WebS::Version);
			return 1;
		}

		static const struct luaL_Reg websFunctions[] = {
			{ "Connect", Connect },
			{ "Disconnect", Disconnect },
			{ "SendMessage", Send },
			{ "SendMessageAsync", SendAsync },
			{ "GetMessage", GetMsg },
			{ "GetQueueSize", GetQueueSize },
			{ "GetStatus", GetStatus },
			{ "GetConnectionId", GetConnectionId },
			{ "ProcessEvents", ProcessEvents },
			{ "On", On },
			{ "Off", Off },
			{ "SetReconnect", SetReconnect },
			{ "GetReconnectAttempts", GetReconnectAttempts },
			{ "SetLogLevel", SetLogLevel },
			{ "GetLogLevel", GetLogLevel },
			{ "GetVersion", GetVersion },
			{ NULL, NULL }
		};

		void registerAll(lua_State* L) {
			luaL_openlib(L, "WebS", websFunctions, 0);
		}

	} // namespace LuaBindings
} // namespace WebS
