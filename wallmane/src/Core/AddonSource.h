#pragma once
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>

namespace Core
{
    struct RemoteAddon
    {
        std::wstring id;              // Unique identifier (source + slug)
        std::wstring name;
        std::wstring description;
        std::wstring author;
        std::wstring version;
        std::wstring downloadUrl;
        std::wstring sourceName;      // "Felbite", "Warperia", etc.
        std::wstring category;        // "UI", "Raid", "PvP", etc.
        int downloadCount = 0;
        std::wstring thumbnailUrl;    // For rich UI cards
        std::wstring addonFolderName; // The ACTUAL folder name expected by WoW
    };

    class IAddonSource
    {
    public:
        virtual ~IAddonSource() = default;
        virtual std::wstring GetName() const = 0;
        virtual winrt::Windows::Foundation::IAsyncOperation<std::vector<RemoteAddon>> SearchAsync(std::wstring query) = 0;
        virtual winrt::Windows::Foundation::IAsyncOperation<std::vector<RemoteAddon>> GetPopularAsync() = 0;
        virtual winrt::Windows::Foundation::IAsyncOperation<std::vector<RemoteAddon>> GetCategoryAsync(std::wstring category) = 0;
    };
}