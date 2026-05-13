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
        std::wstring downloadUrl;
        bool isInstalled = false;
    };

    class AddonManager
    {
    public:
        // Get the curated list of available addons
        static std::vector<Addon> GetAvailableAddons(const std::wstring& wowPath);

        // Check if the HD patch file is already installed
        static bool IsHDPatchInstalled(const std::wstring& wowPath);

        // Download and extract an addon into the WoW Interface/AddOns folder
        static winrt::Windows::Foundation::IAsyncAction InstallAddonAsync(Addon addon, std::wstring wowPath);

        // Install an HD patch into the WoW Data folder
        static winrt::Windows::Foundation::IAsyncAction InstallHDPatchAsync(std::wstring patchUrl, std::wstring wowPath);
    };
}
