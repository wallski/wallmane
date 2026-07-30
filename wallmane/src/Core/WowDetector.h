#pragma once
#include <string>
#include <functional>
#include <filesystem>

namespace Core
{
    class WowDetector
    {
    public:
        static std::wstring BrowseForWowExe(void* hwnd);
        static bool LaunchWow(const std::wstring& path, std::function<void(uint64_t)> onGameClosed = nullptr);
        static bool IsValidWowPath(const std::wstring& path);
        static uint64_t GetTotalPlaytimeSeconds(const std::wstring& wowPath);
        static std::wstring FormatPlaytime(uint64_t seconds);

        // Realmlist & WTF Swapping
        static std::filesystem::path FindRealmlistFile(const std::wstring& wowPath);
        static std::wstring ReadRealmlist(const std::wstring& wowPath);
        static bool SetRealmlist(const std::wstring& wowPath, const std::wstring& newRealmlist);
        static bool IsWowRunning();
        static std::wstring SanitizeRealmlistName(const std::wstring& name);
    };
}

