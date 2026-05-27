#include "pch.h"
#include "WowDetector.h"
#include <windows.h>
#include <commdlg.h>
#include <filesystem>
#include <winrt/Windows.Storage.h>
#include <chrono>
#include <thread>

namespace Core
{
    std::wstring WowDetector::BrowseForWowExe(void* hwnd)
    {
        wchar_t filename[MAX_PATH] = { 0 };
        OPENFILENAMEW ofn = { 0 };
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = (HWND)hwnd;
        ofn.lpstrFilter = L"Wow.exe\0Wow.exe\0All Files\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrTitle = L"Select WotLK Wow.exe";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

        if (GetOpenFileNameW(&ofn))
        {
            return std::wstring(filename);
        }
        return L"";
    }

    bool WowDetector::LaunchWow(const std::wstring& path)
    {
        if (!IsValidWowPath(path)) return false;

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        
        std::wstring cmd = path;
        std::filesystem::path dir = std::filesystem::path(path).parent_path();

        if (CreateProcessW(
            cmd.c_str(),
            nullptr, // cmd params
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            dir.c_str(), // working dir
            &si,
            &pi))
        {
            // Track playtime in background
            std::thread([hProcess = pi.hProcess]() {
                auto start = std::chrono::steady_clock::now();
                WaitForSingleObject(hProcess, INFINITE);
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

                try {
                    auto settings = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
                    uint64_t total = 0;
                    if (settings.HasKey(L"TotalPlaytime"))
                        total = winrt::unbox_value<uint64_t>(settings.Lookup(L"TotalPlaytime"));
                    settings.Insert(L"TotalPlaytime", winrt::box_value(total + duration));
                } catch(...) {}

                CloseHandle(hProcess);
            }).detach();

            CloseHandle(pi.hThread);
            return true;
        }
        return false;
    }

    uint64_t WowDetector::GetTotalPlaytimeSeconds()
    {
        try {
            auto settings = winrt::Windows::Storage::ApplicationData::Current().LocalSettings().Values();
            if (settings.HasKey(L"TotalPlaytime")) {
                return winrt::unbox_value<uint64_t>(settings.Lookup(L"TotalPlaytime"));
            }
        } catch(...) {}
        return 0;
    }

    std::wstring WowDetector::FormatPlaytime(uint64_t seconds)
    {
        uint64_t h = seconds / 3600;
        uint64_t m = (seconds % 3600) / 60;
        if (h > 0) return std::to_wstring(h) + L"h " + std::to_wstring(m) + L"m";
        return std::to_wstring(m) + L"m";
    }

    bool WowDetector::IsValidWowPath(const std::wstring& path)
    {
        return std::filesystem::exists(path) && std::filesystem::path(path).filename() == L"Wow.exe";
    }
}
