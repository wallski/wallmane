#pragma once
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>

namespace Core
{
    struct Addon
    {
        std::wstring name;        // Display name
        std::wstring folderName;  // Actual folder name inside Interface/AddOns for detection
        std::wstring description;
        std::wstring pageUrl;     // Addon page URL (for scraping the download link)
        std::wstring downloadUrl; // Direct zip URL
        bool isInstalled = false;
        std::wstring thumbnailUrl; // Optional: for display
    };

    class AddonManager
    {
    public:
        // Search addons dynamically from Felbite
        static std::vector<Addon> SearchAddons(const std::wstring& query, const std::wstring& wowPath);

        // Fetch the direct download URL for a specific addon page
        static std::wstring GetDownloadUrl(const std::wstring& pageUrl);

        // Get all installed addons by parsing .toc files locally
        static std::vector<Addon> GetInstalledAddons(const std::wstring& wowPath);

        // Download and extract an addon into the WoW Interface/AddOns folder
        static winrt::Windows::Foundation::IAsyncAction InstallAddonAsync(Addon addon, std::wstring wowPath);
    };
}
