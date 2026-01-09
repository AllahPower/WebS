#pragma once

extern "C" {
#include "lua.h"
}

namespace WebS {
namespace LuaBindings {

int Connect(lua_State* L);
int Disconnect(lua_State* L);
int Send(lua_State* L);
int SendAsync(lua_State* L);
int GetMsg(lua_State* L);
int GetQueueSize(lua_State* L);
int GetStatus(lua_State* L);
int GetConnectionId(lua_State* L);
int ProcessEvents(lua_State* L);

int On(lua_State* L);
int Off(lua_State* L);

int SetReconnect(lua_State* L);
int GetReconnectAttempts(lua_State* L);

int SetLogLevel(lua_State* L);
int GetLogLevel(lua_State* L);

void registerAll(lua_State* L);

} // namespace LuaBindings
} // namespace WebS
