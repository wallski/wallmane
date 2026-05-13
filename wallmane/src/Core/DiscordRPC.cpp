#include "pch.h"
#include "DiscordRPC.h"
#include <windows.h>
#include <thread>
#include <format>
#include <processthreadsapi.h>

namespace Core
{
    static HANDLE s_pipe = INVALID_HANDLE_VALUE;
    static std::string s_clientId;

    void DiscordRPC::ConnectPipe()
    {
        if (s_pipe != INVALID_HANDLE_VALUE) return;

        for (int i = 0; i < 10; ++i)
        {
            std::wstring pipeName = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(i);
            s_pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (s_pipe != INVALID_HANDLE_VALUE)
                break;
        }

        if (s_pipe != INVALID_HANDLE_VALUE)
        {
            // Send Handshake (Opcode 0)
            std::string payload = std::format(R"({{"v":1,"client_id":"{}"}})", s_clientId);
            WriteFrame(0, payload);
        }
    }

    bool DiscordRPC::WriteFrame(int opcode, const std::string& payload)
    {
        if (s_pipe == INVALID_HANDLE_VALUE) return false;

        struct Header {
            uint32_t opcode;
            uint32_t length;
        } header;

        header.opcode = opcode;
        header.length = (uint32_t)payload.size();

        DWORD written = 0;
        if (!WriteFile(s_pipe, &header, sizeof(header), &written, nullptr) ||
            !WriteFile(s_pipe, payload.data(), header.length, &written, nullptr))
        {
            CloseHandle(s_pipe);
            s_pipe = INVALID_HANDLE_VALUE;
            return false;
        }
        return true;
    }

    void DiscordRPC::Initialize(const std::string& clientId)
    {
        s_clientId = clientId;
        std::thread([]() {
            ConnectPipe();
        }).detach();
    }

    void DiscordRPC::Shutdown()
    {
        if (s_pipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(s_pipe);
            s_pipe = INVALID_HANDLE_VALUE;
        }
    }

    void DiscordRPC::SetPresence(const std::string& details, const std::string& state)
    {
        std::thread([details, state]() {
            if (s_pipe == INVALID_HANDLE_VALUE) ConnectPipe();
            if (s_pipe == INVALID_HANDLE_VALUE) return;

            std::string payload = std::format(R"({{
                "cmd": "SET_ACTIVITY",
                "args": {{
                    "pid": {},
                    "activity": {{
                        "details": "{}",
                        "state": "{}",
                        "assets": {{
                            "large_image": "wallmane_logo",
                            "large_text": "Wallmane Launcher"
                        }}
                    }}
                }},
                "nonce": "1"
            }})", GetCurrentProcessId(), details, state);

            WriteFrame(1, payload);
        }).detach();
    }
}
