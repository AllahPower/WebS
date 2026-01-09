#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include "Types.h"
#include "ThreadSafeQueue.h"

extern "C" {
#include "lua.h"
}

namespace WebS {

class EventManager {
public:
    int on(lua_State* L, const std::string& eventName, int callbackStackIndex);
    void off(lua_State* L, const std::string& eventName, int callbackRef);
    void offAll(lua_State* L, const std::string& eventName);
    void emit(const std::string& eventName, const std::vector<std::string>& args = {});
    int processEvents(lua_State* L);
    void clear(lua_State* L);
    size_t callbackCount(const std::string& eventName) const;
    bool isRefValid(const std::string& eventName, int ref) const;

private:
    struct CallbackInfo {
        int ref;
    };

    void callCallbacks(lua_State* L, const std::string& eventName, const std::vector<std::string>& args);
    void callLegacyCallback(lua_State* L, const std::string& eventName, const std::vector<std::string>& args);

    std::map<std::string, std::vector<CallbackInfo>> callbacks_;
    ThreadSafeQueue<LuaEvent> eventQueue_;
    mutable std::mutex callbacksMutex_;
};

} // namespace WebS
