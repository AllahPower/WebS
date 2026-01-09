#include "pch.h"
#include "EventManager.h"
#include "Logger.h"

extern "C" {
#include "lauxlib.h"
}

namespace WebS {

    int EventManager::on(lua_State* L, const std::string& eventName, int callbackStackIndex) {
        if (!L) return -1;

        if (!lua_isfunction(L, callbackStackIndex)) {
            Logger::instance().luaError("EventManager::on", "Argument is not a function");
            return -1;
        }

        lua_pushvalue(L, callbackStackIndex);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);

        std::lock_guard<std::mutex> lock(callbacksMutex_);
        callbacks_[eventName].push_back({ ref });

        return ref;
    }

    void EventManager::off(lua_State* L, const std::string& eventName, int callbackRef) {
        if (!L) return;

        std::lock_guard<std::mutex> lock(callbacksMutex_);

        auto it = callbacks_.find(eventName);
        if (it == callbacks_.end()) return;

        auto& vec = it->second;
        for (auto vecIt = vec.begin(); vecIt != vec.end(); ++vecIt) {
            if (vecIt->ref == callbackRef) {
                luaL_unref(L, LUA_REGISTRYINDEX, callbackRef);
                vec.erase(vecIt);
                break;
            }
        }

        if (vec.empty()) {
            callbacks_.erase(it);
        }
    }

    void EventManager::offAll(lua_State* L, const std::string& eventName) {
        if (!L) return;

        std::lock_guard<std::mutex> lock(callbacksMutex_);

        auto it = callbacks_.find(eventName);
        if (it == callbacks_.end()) return;

        for (auto& cb : it->second) {
            luaL_unref(L, LUA_REGISTRYINDEX, cb.ref);
        }
        callbacks_.erase(it);
    }

    void EventManager::emit(const std::string& eventName, const std::vector<std::string>& args) {
        eventQueue_.push({ eventName, args });
    }

    int EventManager::processEvents(lua_State* L) {
        if (!L) return 0;

        std::queue<LuaEvent> eventsToProcess;
        eventQueue_.swap(eventsToProcess);

        int processed = 0;
        while (!eventsToProcess.empty()) {
            LuaEvent event = std::move(eventsToProcess.front());
            eventsToProcess.pop();

            int top = lua_gettop(L);

            callLegacyCallback(L, event.name, event.args);

            lua_settop(L, top);

            callCallbacks(L, event.name, event.args);

            lua_settop(L, top);

            processed++;
        }

        return processed;
    }

    void EventManager::clear(lua_State* L) {
        if (!L) return;

        std::lock_guard<std::mutex> lock(callbacksMutex_);

        for (auto& pair : callbacks_) {
            for (auto& cb : pair.second) {
                luaL_unref(L, LUA_REGISTRYINDEX, cb.ref);
            }
        }
        callbacks_.clear();
        std::queue<LuaEvent> empty;
        eventQueue_.swap(empty);
    }

    size_t EventManager::callbackCount(const std::string& eventName) const {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        auto it = callbacks_.find(eventName);
        if (it == callbacks_.end()) return 0;
        return it->second.size();
    }

    bool EventManager::isRefValid(const std::string& eventName, int ref) const {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        auto it = callbacks_.find(eventName);
        if (it == callbacks_.end()) return false;
        for (const auto& cb : it->second) {
            if (cb.ref == ref) return true;
        }
        return false;
    }

    void EventManager::callCallbacks(lua_State* L, const std::string& eventName, const std::vector<std::string>& args) {
        if (!L) return;

        std::vector<int> refs;
        {
            std::lock_guard<std::mutex> lock(callbacksMutex_);
            auto it = callbacks_.find(eventName);
            if (it == callbacks_.end()) return;
            for (const auto& cb : it->second) {
                refs.push_back(cb.ref);
            }
        }

        for (int ref : refs) {
            if (!isRefValid(eventName, ref)) {
                continue;
            }

            if (!lua_checkstack(L, static_cast<int>(args.size() + 2))) {
                Logger::instance().luaError(eventName, "Stack overflow risk: too many arguments");
                continue;
            }

            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            if (!lua_isfunction(L, -1)) {
                lua_pop(L, 1);
                continue;
            }

            for (const auto& arg : args) {
                lua_pushstring(L, arg.c_str());
            }

            if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != 0) {
                const char* err = lua_tostring(L, -1);
                Logger::instance().luaError(eventName, err ? err : "unknown error");
                lua_pop(L, 1);
            }
        }
    }

    void EventManager::callLegacyCallback(lua_State* L, const std::string& eventName, const std::vector<std::string>& args) {
        if (!L) return;

        int top = lua_gettop(L);

        if (!lua_checkstack(L, static_cast<int>(args.size() + 3))) {
            Logger::instance().luaError(eventName, "Legacy stack overflow risk");
            return;
        }

        lua_getglobal(L, "WebS");
        if (!lua_istable(L, -1)) {
            lua_settop(L, top);
            return;
        }

        lua_getfield(L, -1, eventName.c_str());
        if (!lua_isfunction(L, -1)) {
            lua_settop(L, top);
            return;
        }

        for (const auto& arg : args) {
            lua_pushstring(L, arg.c_str());
        }

        if (lua_pcall(L, static_cast<int>(args.size()), 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            Logger::instance().luaError(eventName, err ? err : "unknown error");
        }

        lua_settop(L, top);
    }

} // namespace WebS