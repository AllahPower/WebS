#include "pch.h"
#include "WebSClient.h"
#include "LuaBindings.h"
#include "Logger.h"
#include <delayimp.h>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

static HMODULE g_hModule = nullptr;
static std::string g_dllDirectory;

static std::string GetDllDirectory(HMODULE hModule) {
    char path[MAX_PATH] = { 0 };
    if (GetModuleFileNameA(hModule, path, MAX_PATH) == 0) {
        return "";
    }
    std::string fullPath(path);
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(0, lastSlash + 1);
    }
    return "";
}

static FARPROC WINAPI DelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli) {
    if (dliNotify == dliNotePreLoadLibrary) {
        if (!g_dllDirectory.empty() && pdli->szDll) {
            std::string fullPath = g_dllDirectory + pdli->szDll;
            HMODULE hMod = LoadLibraryA(fullPath.c_str());
            if (hMod) {
                return reinterpret_cast<FARPROC>(hMod);
            }
        }
    }
    return nullptr;
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DelayLoadHook;

static bool LoadDependencies(HMODULE hModule) {
    if (g_dllDirectory.empty()) {
        return false;
    }

    const char* dependencies[] = {
        "zlib1.dll",
        "brotlicommon.dll",
        "brotlidec.dll",
        "brotlienc.dll",
        "libcrypto-3.dll",
        "libssl-3.dll",
        "jsoncpp.dll",
        "cpprest_2_10.dll",
        "microsoft-signalr.dll"
    };

    std::ofstream logfile("websocketLogging.txt", std::ios::app);
    bool allLoaded = true;

    for (const char* dep : dependencies) {
        std::string fullPath = g_dllDirectory + dep;
        HMODULE hDep = LoadLibraryA(fullPath.c_str());
        if (!hDep) {
            DWORD err = GetLastError();
            if (logfile.is_open()) {
                logfile << "[WARN] Could not load " << dep << " from " << g_dllDirectory
                        << " (Error: " << err << "). Trying system path..." << std::endl;
            }
            hDep = LoadLibraryA(dep);
            if (!hDep) {
                if (logfile.is_open()) {
                    logfile << "[ERROR] Failed to load " << dep << " from any location." << std::endl;
                }
                allLoaded = false;
            }
        }
    }

    return allLoaded;
}

extern "C" __declspec(dllexport) int luaopen_WebS(lua_State* L) {
    WebS::Logger::instance().info("WebS DLL Loaded and Initialized.");
    WebS::WebSClient::instance().setLuaState(L);
    WebS::LuaBindings::registerAll(L);
    return 1;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: {
        g_hModule = hModule;
        g_dllDirectory = GetDllDirectory(hModule);

        if (!g_dllDirectory.empty()) {
            SetDllDirectoryA(g_dllDirectory.c_str());
        }

        DisableThreadLibraryCalls(hModule);

        WebS::Logger::instance().clear();
        WebS::Logger::instance().info("=== New session started ===");

        if (!LoadDependencies(hModule)) {
            std::ofstream logfile("websocketLogging.txt", std::ios::app);
            if (logfile.is_open()) {
                logfile << "[CRITICAL] Some dependencies failed to load!" << std::endl;
            }
        }
        break;
    }

    case DLL_PROCESS_DETACH:
        if (lpReserved == nullptr) {
            SetDllDirectoryA(nullptr);
            WebS::WebSClient::instance().shutdown();
            WebS::Logger::instance().info("WebS DLL Unloading.");
        }
        break;
    }
    return TRUE;
}
