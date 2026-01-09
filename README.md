# WebS - SignalR Bridge for MoonLoader (SAMP)

**WebS** is a C++ DLL library designed to integrate **SignalR** WebSocket functionality into **MoonLoader** for GTA: San Andreas MultiPlayer (SAMP). It provides a thread-safe, non-blocking interface for real-time communication between the game client and a SignalR hub.

---

## Architecture

The library is built with OOP principles using the following components:

| Component | Description |
| :--- | :--- |
| `WebSClient` | Singleton managing connection lifecycle, reconnection, and message queues |
| `EventManager` | Dynamic event registration system with callback management |
| `Logger` | Thread-safe file logger implementing `signalr::log_writer` |
| `ThreadSafeQueue<T>` | Generic thread-safe queue for cross-thread communication |

---

## Lua API Methods

The library exports the following methods under the `WebS` namespace:

### Connection

| Method | Description |
| :--- | :--- |
| `WebS.Connect(url, [token])` | Initiates connection to SignalR hub. Returns `true` on success. |
| `WebS.Disconnect()` | Disconnects from the hub safely. |
| `WebS.GetStatus()` | Returns status: `"disconnected"`, `"connecting"`, `"connected"`, `"disconnecting"`, `"reconnecting"`. |
| `WebS.GetConnectionId()` | Returns the Connection ID assigned by the hub. |

### Messaging

| Method | Description |
| :--- | :--- |
| `WebS.SendMessage(method, argsTable)` | Fire-and-Forget. Invokes a hub method without waiting. |
| `WebS.SendMessageAsync(method, argsTable, callback)` | Invokes a method and calls `callback(success, result)` on response. |
| `WebS.GetMessage()` | Retrieves next message from queue. Returns empty string if empty. |
| `WebS.GetQueueSize()` | Returns number of unread messages. |
| `WebS.ProcessEvents()` | **Must be called in a loop.** Processes events and callbacks. |

### Events

| Method | Description |
| :--- | :--- |
| `WebS.On(eventName, callback)` | Registers a callback for an event. Returns callback reference. |
| `WebS.Off(eventName, callbackRef)` | Removes a previously registered callback. |

**Built-in events:** `OnConnect`, `OnDisconnect`, `OnError`, `OnReconnecting`, `OnReconnected`

**Server methods:** Any server-side method can be subscribed via `WebS.On("MethodName", callback)`.

### Reconnection

| Method | Description |
| :--- | :--- |
| `WebS.SetReconnect(config)` | Configures auto-reconnect behavior. |
| `WebS.GetReconnectAttempts()` | Returns current reconnection attempt count. |

**Config options:**
```lua
WebS.SetReconnect({
    enabled = true,        -- Enable auto-reconnect
    maxAttempts = 5,       -- Max attempts (0 = infinite)
    initialDelay = 1000,   -- Initial delay in ms
    maxDelay = 30000,      -- Max delay in ms
    multiplier = 2.0       -- Exponential backoff multiplier
})
```

---

## Lua Example

```lua
require("WebS")

local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8

local SERVER_URL = "https://localhost:7243/hub"

function main()
    if not isSampLoaded() or not isSampfuncsLoaded() then return end
    while not isSampAvailable() do wait(100) end

    -- Register event handlers
    WebS.On("OnConnect", function()
        sampAddChatMessage("[WebS] Connected!", 0x00FF00)
    end)

    WebS.On("OnDisconnect", function()
        sampAddChatMessage("[WebS] Disconnected.", 0xFFAA00)
    end)

    WebS.On("OnError", function(msg)
        sampAddChatMessage("[WebS] Error: " .. tostring(msg), 0xFF0000)
    end)

    WebS.On("OnReconnecting", function(attempt)
        sampAddChatMessage("[WebS] Reconnecting... Attempt: " .. attempt, 0xFFFF00)
    end)

    WebS.On("OnReconnected", function()
        sampAddChatMessage("[WebS] Reconnected!", 0x00FF00)
    end)

    -- Register server method handler
    WebS.On("ReceiveMessage", function(msg)
        sampAddChatMessage("[Server] " .. u8:decode(msg), 0x00DDDD)
    end)

    -- Configure reconnection
    WebS.SetReconnect({
        enabled = true,
        maxAttempts = 5,
        initialDelay = 1000,
        maxDelay = 30000,
        multiplier = 2.0
    })

    -- Register commands
    sampRegisterChatCommand("ws_connect", function()
        WebS.Connect(SERVER_URL)
    end)

    sampRegisterChatCommand("ws_send", function(text)
        if WebS.GetStatus() ~= "connected" then
            sampAddChatMessage("[WebS] Not connected!", 0xFF0000)
            return
        end
        WebS.SendMessage("SendMessage", { u8(text) })
    end)

    -- Main loop
    while true do
        wait(50)
        WebS.ProcessEvents()
    end
end
```

---

## Installation

Place all DLL files in `moonloader/lib/`:

```
moonloader/lib/
  WebS.dll
  microsoft-signalr.dll
  cpprest_2_10.dll
  jsoncpp.dll
  libcrypto-3.dll
  libssl-3.dll
  zlib1.dll
  brotlicommon.dll
  brotlidec.dll
  brotlienc.dll
```

---

## TODO / Roadmap

- [x] **Async Architecture:** Background thread for connection logic
- [x] **Event System:** `OnConnect`, `OnError`, `OnDisconnect` callbacks
- [x] **Crash Fixes:** Safe exception handling without `std::rethrow_exception`
- [x] **General Logging:** Robust logging with levels (Info/Debug/Error)
- [x] **Async Requests:** `SendMessageAsync` with Lua callbacks
- [x] **Custom Lua Events:** Dynamic event handlers via `WebS.On("Event", callback)`
- [x] **OOP Refactoring:** Singleton architecture with separated concerns
- [x] **Reconnect Logic:** Auto-reconnection with exponential backoff

---

## Building

* **Platform:** Windows (x86, 32-bit only)
* **Compiler:** MSVC (Visual Studio 2019/2022) with C++17 support

### Dependencies

1. **[SignalR-Client-Cpp](https://github.com/aspnet/SignalR-Client-Cpp):** Build this dependency first.
2. **Lua 5.1 Headers:** `lua.h`, `lualib.h`, `lauxlib.h` from [Lua 5.1.5](https://www.lua.org/ftp/lua-5.1.5.tar.gz)

### Project Configuration

* **Precompiled Headers:** Uses `pch.h`
* **Runtime Library:** Static (`/MT`)
* **Delay-Loaded DLLs:** All SignalR dependencies use delay-load for custom path resolution
* **Include Directories:** SignalR headers, Lua 5.1 sources
* **Linker Directories:** SignalR `.lib` files
