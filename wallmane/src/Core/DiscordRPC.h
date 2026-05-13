#pragma once
#include <string>

namespace Core
{
    class DiscordRPC
    {
    public:
        // Initialize the Discord connection in the background
        static void Initialize(const std::string& clientId);

        // Disconnect from Discord
        static void Shutdown();

        // Set the user's playing status
        static void SetPresence(const std::string& details, const std::string& state);

    private:
        static void ConnectPipe();
        static bool WriteFrame(int opcode, const std::string& payload);
    };
}
