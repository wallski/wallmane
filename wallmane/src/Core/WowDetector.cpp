#include "pch.h"
#include "WowDetector.h"
#include <windows.h>
#include <commdlg.h>
#include <filesystem>

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
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return true;
        }
        return false;
    }

    bool WowDetector::IsValidWowPath(const std::wstring& path)
    {
        return std::filesystem::exists(path) && std::filesystem::path(path).filename() == L"Wow.exe";
    }
}
