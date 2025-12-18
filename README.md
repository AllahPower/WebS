# WebS - SignalR Bridge for MoonLoader (SAMP)

**WebS** is a C++ DLL library designed to integrate **SignalR** WebSocket functionality into **MoonLoader** for GTA: San Andreas MultiPlayer (SAMP). It provides a thread-safe, non-blocking interface for real-time communication between the game client and a SignalR hub.

---

## 🛠️ Lua API Methods

The library exports the following methods under the `WebS` namespace:

| Method | Description |
| :--- | :--- |
| `WebS.Connect(url, [token])` | Initiates a connection to the SignalR hub. `url` is required; `token` (JWT) is optional. Returns `true` on success (start of process). |
| `WebS.Disconnect()` | Disconnects from the hub safely. |
| `WebS.SendMessage(method, argsTable)` | **Fire-and-Forget.** Invokes a method on the hub without waiting for a result. |
| `WebS.SendMessageAsync(method, argsTable, callback)` | **New!** Invokes a method and waits for a result asynchronously. `callback` is a function `func(success, result)` called when the server responds. |
| `WebS.GetMessage()` | Retrieves the next message from the incoming queue. Returns an empty string if the queue is empty. |
| `WebS.GetConnectionId()` | Returns the unique Connection ID string assigned by the SignalR hub. Returns an empty string if not connected. |
| `WebS.ProcessEvents()` | **Must be called in a loop.** Processes internal events (`OnConnect`, `OnError`) and **executes async callbacks**. |
| `WebS.GetStatus()` | Returns the current connection status: `"disconnected"`, `"connecting"`, `"connected"`, `"disconnecting"`. |
| `WebS.GetQueueSize()` | Returns the number of unread messages in the queue. |

### Lua Callbacks
Define these functions in your script to handle events:

*   `WebS.OnConnect()` - Triggered when the connection is successfully established.
*   `WebS.OnDisconnect()` - Triggered when the connection is closed.
*   `WebS.OnError(message)` - Triggered on connection failure or errors, passing the error message string.

---

## 📝 Lua Example

Below is a simple MoonLoader script demonstrating how to connect, handle events, and send messages.

```js
require "WebS"

local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8

WebS.OnConnect = function()
    sampAddChatMessage("SignalR: Connected successfully!", 0x00FF00)
end

WebS.OnError = function(msg)
    sampAddChatMessage("SignalR Error: " .. tostring(msg), 0xFF0000)
end

WebS.OnDisconnect = function()
    sampAddChatMessage("SignalR: Disconnected.", 0xFFAA00)
end

function main()
    if not isSampLoaded() then return end
    while not isSampAvailable() do wait(100) end

    WebS.Connect("http://localhost:5000/chatHub", "MY_SECRET_TOKEN")

    sampRegisterChatCommand("ws_send", function(text)
        local _, myId = sampGetPlayerIdByCharHandle(PLAYER_PED)
        
        local payload_table = { myId, u8(text) }
        WebS.SendMessage("SendMessage", payload_table)
    end)

    while true do
        wait(0)
        
        WebS.ProcessEvents()

        local msg = WebS.GetMessage()
        if msg ~= "" then
            sampAddChatMessage("Server: " .. u8:decode(msg), 0xFFFFFF)
        end
    end
end
```

---

## ✅ TODO / Roadmap

1.  [x] **Async Architecture:** Move connection logic to a background thread to prevent game freezes.
2.  [x] **Event System:** Implement `OnConnect`, `OnError` callbacks with arguments.
3.  [x] **Crash Fixes:** Resolve Access Violations by removing unsafe `std::rethrow_exception` usage.
4.  [x] **General Logging:** Implement a robust logging system with log levels (Info/Debug/Error) and clean formatting.
5.  [x] **Async Requests:** Added `SendMessageAsync` to handle server responses via Lua callbacks.
6.  [ ] **Custom Lua Events:** Allow registering dynamic event handlers from Lua (e.g., `WebS.On("MyEvent", callback)`) instead of hardcoded callbacks.
7.  [ ] **OOP Refactoring:** Encapsulate global state into a proper Singleton class or Object instances.
8.  [ ] **Reconnect Logic:** Add automatic reconnection strategies with exponential backoff.

---

## ⚠️ Building

*   **Platform:** Windows (x86, 32-bit only)
*   **Compiler:** MSVC (Visual Studio 2019/2022/2026) with C++17 support.

### Dependencies:
1.  **[SignalR-Client-Cpp](https://github.com/aspnet/SignalR-Client-Cpp):** The C++ client library for SignalR. You must build this dependency first.
2.  **Lua 5.1 Sources:**
    *   **Required Files:** `lua.h`, `lualib.h`, `lauxlib.h`.
    *   **Reason:** The project needs these header files to link against the Lua API provided by MoonLoader.
    *   **Get them here:** You can download the source code from the official [Lua 5.1.5 release page](https://www.lua.org/ftp/lua-5.1.5.tar.gz).
    
### Project Configuration:
*   **Precompiled Headers (PCH):** The project uses `pch.h` for faster build times. Ensure this is configured correctly in Visual Studio.
*   **Include Directories:** Add the paths to the SignalR client headers and the Lua 5.1 source files to your project's include directories.
*   **Linker Directories:** Add the path to the compiled SignalR `.lib` file to your project's linker directories.
