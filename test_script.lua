require("WebS")

local encoding = require 'encoding'
encoding.default = 'CP1251'
local u8 = encoding.UTF8

local SERVER_URL = "https://localhost:7243/message-receiver-hub"
local DEBUG_MODE = true
local CURRENT_NICKNAME = ""

function main()
    if not isSampLoaded() or not isSampfuncsLoaded() then return end
    while not isSampAvailable() do wait(100) end

    checkApi()

    local _, id = sampGetPlayerIdByCharHandle(PLAYER_PED)
    CURRENT_NICKNAME = sampGetPlayerNickname(id)

    registerEvents()
    registerCommands()

    WebS.SetReconnect({
        enabled = true,
        maxAttempts = 5,
        initialDelay = 1000,
        maxDelay = 30000,
        multiplier = 2.0
    })

    sampAddChatMessage("[WebS] Debug script loaded. Use /ws_connect to start.", 0xCCCCCC)

    lua_thread.create(function()
        while true do
            WebS.ProcessEvents()

            local msg = WebS.GetMessage()
            while msg ~= "" do
                sampAddChatMessage("[HUB] " .. u8:decode(msg), 0x00DD00)
                msg = WebS.GetMessage()
            end

            wait(50)
        end
    end)

    wait(-1)
end

function registerEvents()
    WebS.On("OnConnect", function()
        sampAddChatMessage("[WebS] Connected to HUB!", 0x00FF00)
        print("[WebS] Event: OnConnect")
    end)

    WebS.On("OnError", function(msg)
        sampAddChatMessage("[WebS] Error: " .. tostring(msg), 0xFF0000)
        print("[WebS] Event: OnError - " .. tostring(msg))
    end)

    WebS.On("OnDisconnect", function()
        local status = WebS.GetStatus()
        if status ~= "connected" and status ~= "connecting" then
            sampAddChatMessage("[WebS] Disconnected.", 0xFFAA00)
            print("[WebS] Event: OnDisconnect")
        else
            if DEBUG_MODE then print("[WebS] Ignored OnDisconnect (status: " .. status .. ")") end
        end
    end)

    WebS.On("OnReconnecting", function(attempt)
        sampAddChatMessage("[WebS] Reconnecting... Attempt: " .. tostring(attempt), 0xFFFF00)
        print("[WebS] Event: OnReconnecting - Attempt " .. tostring(attempt))
    end)

    WebS.On("OnReconnected", function()
        sampAddChatMessage("[WebS] Reconnected successfully!", 0x00FF00)
        print("[WebS] Event: OnReconnected")
    end)

    WebS.On("PendingMessage", function(msg)
        if msg and msg ~= "" then
            sampAddChatMessage("[Server Event] " .. u8:decode(tostring(msg)), 0x00DDDD)
        end
    end)
end

function registerCommands()
    sampRegisterChatCommand("ws_connect", cmd_connect)
    sampRegisterChatCommand("ws_disconnect", cmd_disconnect)
    sampRegisterChatCommand("ws_status", cmd_status)
    sampRegisterChatCommand("ws_send", cmd_send)
    sampRegisterChatCommand("ws_send_async", cmd_send_async)
    sampRegisterChatCommand("ws_id", cmd_getid)
    sampRegisterChatCommand("ws_debug", cmd_toggle_debug)
    sampRegisterChatCommand("ws_reconnect", cmd_reconnect_config)
end

function cmd_connect(arg)
    local url = arg ~= "" and arg or SERVER_URL
    sampAddChatMessage("[WebS] Connecting to " .. url .. " ...", 0xFFFF00)

    local threadStarted, err = WebS.Connect(url)
    if threadStarted then
        if DEBUG_MODE then print("[WebS] Connection thread started successfully.") end
    else
        sampAddChatMessage("[WebS] Failed to start connection thread! " .. tostring(err), 0xFF0000)
    end
end

function cmd_disconnect()
    WebS.Disconnect()
    sampAddChatMessage("[WebS] Disconnect requested.", 0xFFAA00)
end

function cmd_status()
    local status = WebS.GetStatus()
    local queueSize = WebS.GetQueueSize()
    local reconnectAttempts = WebS.GetReconnectAttempts()

    local color = 0xFFFFFF
    if status == "connected" then color = 0x00FF00
    elseif status == "connecting" then color = 0xFFFF00
    elseif status == "disconnecting" then color = 0xFFAA00
    elseif status == "reconnecting" then color = 0xFF9900
    else color = 0xFF0000 end

    sampAddChatMessage(string.format("[WebS] Status: {%06X}%s{FFFFFF} | Queue: %d | Reconnects: %d", color, status, queueSize, reconnectAttempts), 0xFFFFFF)

    if status == "connected" then
        cmd_getid()
    end
end

function cmd_send(arg)
    if arg == nil or arg == "" then
        sampAddChatMessage("Usage: /ws_send <message>", 0xAAAAAA)
        return
    end

    local status = WebS.GetStatus()
    if status ~= "connected" then
        sampAddChatMessage("[WebS] Cannot send: Not connected!", 0xFF0000)
        return
    end

    local t = {
        Nick = CURRENT_NICKNAME,
        Text = u8(arg),
        Money = getPlayerMoney()
    }

    local jsonString = encodeJson(t)

    local success, err = WebS.SendMessage("SendMessageWS", { jsonString })

    if success then
        sampAddChatMessage("[WebS] Sent (Fire-and-Forget)!", 0x00FF00)
    else
        sampAddChatMessage("[WebS] Send failed: " .. tostring(err), 0xFF0000)
    end
end

function cmd_send_async(arg)
    if arg == nil or arg == "" then
        sampAddChatMessage("Usage: /ws_send_async <message>", 0xAAAAAA)
        return
    end

    local t = {
        Nick = CURRENT_NICKNAME,
        Text = u8(arg),
        Money = getPlayerMoney()
    }

    local jsonString = encodeJson(t)

    sampAddChatMessage("[WebS] Sending async request...", 0xCCCCCC)

    WebS.SendMessageAsync("SendMessageWS", { jsonString }, function(success, result)
        if success then
            sampAddChatMessage("[WebS] Async Success!", 0x00FF00)

            if type(result) == "table" then
                if DEBUG_MODE then
                    sampAddChatMessage("[Async] JSON: " .. u8:decode(encodeJson(result)), 0x00FF00)
                end

                local textValue = result.text or result.Text
                if textValue then
                    sampAddChatMessage("[HUB Reply] " .. u8:decode(textValue), 0x00FF00)
                else
                    sampAddChatMessage("[HUB Reply] (No text field)", 0xAAAAAA)
                end
            elseif type(result) == "string" then
                sampAddChatMessage("[HUB Reply] " .. u8:decode(result), 0x00FF00)
            else
                sampAddChatMessage("[HUB Reply] Type: " .. type(result), 0x00FF00)
            end
        else
            sampAddChatMessage("[WebS] Async Error: " .. tostring(result), 0xFF0000)
        end
    end)
end

function cmd_getid()
    local id = WebS.GetConnectionId()
    if id and id ~= "" then
        sampAddChatMessage("[WebS] Connection ID: " .. id, 0x00DDDD)
    else
        if DEBUG_MODE then sampAddChatMessage("[WebS] Connection ID not available yet.", 0xAAAAAA) end
    end
end

function cmd_toggle_debug()
    DEBUG_MODE = not DEBUG_MODE
    sampAddChatMessage("[WebS] Debug mode: " .. (DEBUG_MODE and "ON" or "OFF"), 0xCCCCCC)
end

function cmd_reconnect_config(arg)
    if arg == "on" then
        WebS.SetReconnect({ enabled = true })
        sampAddChatMessage("[WebS] Auto-reconnect enabled", 0x00FF00)
    elseif arg == "off" then
        WebS.SetReconnect({ enabled = false })
        sampAddChatMessage("[WebS] Auto-reconnect disabled", 0xFF0000)
    else
        sampAddChatMessage("Usage: /ws_reconnect <on|off>", 0xAAAAAA)
    end
end

function checkApi()
    local methods = {
        "Connect",
        "GetMessage",
        "GetQueueSize",
        "GetStatus",
        "SendMessage",
        "SendMessageAsync",
        "Disconnect",
        "ProcessEvents",
        "GetConnectionId",
        "On",
        "Off",
        "SetReconnect",
        "GetReconnectAttempts"
    }

    print("--- WebS API Check ---")
    local allOk = true
    for _, method in ipairs(methods) do
        if WebS[method] then
            if DEBUG_MODE then print("[API] " .. method .. " -> OK") end
        else
            print("[API] " .. method .. " -> MISSING")
            sampAddChatMessage("[WebS] Warning: Method '" .. method .. "' missing!", 0xFF0000)
            allOk = false
        end
    end

    if allOk then
        sampAddChatMessage("[WebS] API Check passed. Library loaded.", 0x00DD00)
    end
end

function onScriptTerminate(scr, quitGame)
    if scr == thisScript() then
        print("[WebS] Script terminated.")
    end
end
