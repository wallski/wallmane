#pragma once
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>

namespace Core
{
    struct TrackedAddonMeta
    {
        std::wstring folderName;
        std::wstring pageUrl;
        std::wstring lastUpdate;
    };

    struct Addon
    {
        std::wstring name;        // Display name
        std::wstring folderName;  // Actual folder name inside Interface/AddOns for detection
        std::wstring description;
        std::wstring pageUrl;     // Addon page URL (for scraping the download link)
        std::wstring downloadUrl; // Direct zip URL
        bool isInstalled = false;
        std::wstring thumbnailUrl; // Optional: for display
        std::wstring lastUpdate;
        bool hasUpdate = false;
    };

    class AddonManager
    {
    public:
        // Search addons dynamically from Felbite
        static std::vector<Addon> SearchAddons(const std::wstring& query, const std::wstring& wowPath);

        // Fetch the direct download URL for a specific addon page
        static std::wstring GetDownloadUrl(const std::wstring& pageUrl);

        // Fetch the latest update date from a Felbite addon page
        static winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLatestUpdateDateAsync(std::wstring pageUrl);

        // Get all installed addons by parsing .toc files locally
        static std::vector<Addon> GetInstalledAddons(const std::wstring& wowPath);

        // Download and extract an addon into the WoW Interface/AddOns folder
        static winrt::Windows::Foundation::IAsyncAction InstallAddonAsync(Addon addon, std::wstring wowPath);

        // Tracked addons metadata management
        static std::vector<TrackedAddonMeta> LoadTrackedAddons();
        static void SaveTrackedAddons(const std::vector<TrackedAddonMeta>& addons);
        static void TrackAddon(const std::wstring& folderName, const std::wstring& pageUrl, const std::wstring& lastUpdate);
    };
}

