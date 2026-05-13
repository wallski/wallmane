#pragma once
#include <string>

namespace Core
{
    class WowDetector
    {
    public:
        static std::wstring BrowseForWowExe(void* hwnd);
        static bool LaunchWow(const std::wstring& path);
        static bool IsValidWowPath(const std::wstring& path);
    };
}
