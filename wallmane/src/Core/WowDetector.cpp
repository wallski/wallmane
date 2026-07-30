#include "pch.h"
#include "WowDetector.h"
#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <algorithm>
#include <regex>

namespace fs = std::filesystem;

namespace Core
{
    static fs::path GetConfigDirectory()
    {
        char* appdata = nullptr;
        size_t len = 0;
        _dupenv_s(&appdata, &len, "APPDATA");
        if (appdata) {
            fs::path p = fs::path(appdata) / L"Wallmane";
            free(appdata);
            std::error_code ec;
            fs::create_directories(p, ec);
            return p;
        }
        return fs::current_path();
    }

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

    bool WowDetector::LaunchWow(const std::wstring& path, std::function<void(uint64_t)> onGameClosed)
    {
        if (!IsValidWowPath(path)) return false;

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        
        std::wstring cmd = path;
        fs::path dir = fs::path(path).parent_path();

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
            std::thread([hProcess = pi.hProcess, onGameClosed]() {
                auto start = std::chrono::steady_clock::now();
                WaitForSingleObject(hProcess, INFINITE);
                auto end = std::chrono::steady_clock::now();
                uint64_t duration = (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

                CloseHandle(hProcess);

                if (onGameClosed) {
                    onGameClosed(duration);
                }
            }).detach();

            CloseHandle(pi.hThread);
            return true;
        }
        return false;
    }

    uint64_t WowDetector::GetTotalPlaytimeSeconds(const std::wstring& wowPath)
    {
        uint64_t total = 0;
        try {
            // Walk WTF/Account/*/SavedVariables/DataStore_Characters.lua
            // and sum all ["played"] = <seconds> entries (Altoholic addon)
            if (wowPath.empty()) return 0;
            fs::path accountsDir = fs::path(wowPath).parent_path() / L"WTF" / L"Account";
            if (!fs::exists(accountsDir)) return 0;

            std::wregex rxPlayed(LR"(\[\"played\"\]\s*=\s*(\d+))");

            for (auto& accEntry : fs::directory_iterator(accountsDir))
            {
                if (!accEntry.is_directory()) continue;
                fs::path luaPath = accEntry.path() / L"SavedVariables" / L"DataStore_Characters.lua";
                if (!fs::exists(luaPath)) continue;

                std::wifstream f(luaPath);
                f.imbue(std::locale(""));
                std::wstring line;
                while (std::getline(f, line))
                {
                    std::wsmatch m;
                    if (std::regex_search(line, m, rxPlayed)) {
                        total += std::stoull(m[1].str());
                    }
                }
            }
        } catch(...) {}
        return total;
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
        return fs::exists(path) && fs::path(path).filename() == L"Wow.exe";
    }

    bool WowDetector::IsWowRunning()
    {
        bool running = false;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W pe;
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(hSnap, &pe))
            {
                do {
                    if (_wcsicmp(pe.szExeFile, L"Wow.exe") == 0)
                    {
                        running = true;
                        break;
                    }
                } while (Process32NextW(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        return running;
    }

    std::wstring WowDetector::SanitizeRealmlistName(const std::wstring& name)
    {
        std::wstring s = name;
        for (auto& ch : s)
        {
            if (ch == L':' || ch == L'/' || ch == L'\\' || ch == L' ' || ch == L'\t')
            {
                ch = L'_';
            }
        }
        return s;
    }

    fs::path WowDetector::FindRealmlistFile(const std::wstring& wowPath)
    {
        if (wowPath.empty()) return fs::path();
        fs::path wowDir = fs::path(wowPath).parent_path();

        // 1. Root realmlist.wtf
        fs::path rootWtf = wowDir / L"realmlist.wtf";
        if (fs::exists(rootWtf)) return rootWtf;

        // 2. Data/[Locale]/realmlist.wtf
        fs::path dataDir = wowDir / L"Data";
        if (fs::exists(dataDir))
        {
            std::error_code ec;
            for (auto& entry : fs::directory_iterator(dataDir, ec))
            {
                if (entry.is_directory())
                {
                    fs::path candidate = entry.path() / L"realmlist.wtf";
                    if (fs::exists(candidate))
                    {
                        return candidate;
                    }
                }
            }
        }

        // Fallback default
        return wowDir / L"Data" / L"enUS" / L"realmlist.wtf";
    }

    std::wstring WowDetector::ReadRealmlist(const std::wstring& wowPath)
    {
        fs::path file = FindRealmlistFile(wowPath);
        if (!fs::exists(file)) return L"logon.warmane.com";

        try {
            std::ifstream in(file);
            std::string line;
            std::regex rx("^set\\s+realmlist\\s+(.+)$", std::regex_constants::icase);
            while (std::getline(in, line))
            {
                // Trim trailing CR
                if (!line.empty() && line.back() == '\r') line.pop_back();

                std::smatch m;
                if (std::regex_search(line, m, rx))
                {
                    std::string host = m[1].str();
                    // Trim spaces
                    host.erase(0, host.find_first_not_of(" \t"));
                    host.erase(host.find_last_not_of(" \t") + 1);

                    int size_needed = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), (int)host.size(), NULL, 0);
                    std::wstring wstr(size_needed, 0);
                    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), (int)host.size(), &wstr[0], size_needed);
                    return wstr;
                }
            }
        } catch(...) {}

        return L"logon.warmane.com";
    }

    bool WowDetector::SetRealmlist(const std::wstring& wowPath, const std::wstring& newRealmlist)
    {
        if (wowPath.empty() || newRealmlist.empty()) return false;
        if (IsWowRunning()) return false; // Prevent switching while WoW is running

        std::wstring currentRealmlist = ReadRealmlist(wowPath);
        std::wstring cleanCurrent = SanitizeRealmlistName(currentRealmlist);
        std::wstring cleanNew = SanitizeRealmlistName(newRealmlist);

        fs::path wowDir = fs::path(wowPath).parent_path();
        fs::path wtfDir = wowDir / L"WTF";
        fs::path backupsDir = wowDir / L"WTF_Backups";

        // Swap WTF directories if realmlist is actually changing
        if (_wcsicmp(cleanCurrent.c_str(), cleanNew.c_str()) != 0)
        {
            std::error_code ec;

            // 1. Move active WTF to WTF_Backups/[cleanCurrent]
            if (fs::exists(wtfDir) && !cleanCurrent.empty())
            {
                fs::create_directories(backupsDir, ec);
                fs::path oldBackupDir = backupsDir / cleanCurrent;
                fs::remove_all(oldBackupDir, ec);
                fs::rename(wtfDir, oldBackupDir, ec);
            }

            // 2. Restore WTF_Backups/[cleanNew] if it exists
            if (!cleanNew.empty())
            {
                fs::path newBackupDir = backupsDir / cleanNew;
                if (fs::exists(newBackupDir))
                {
                    fs::rename(newBackupDir, wtfDir, ec);
                }
            }
        }

        // Write new realmlist.wtf
        fs::path targetFile = FindRealmlistFile(wowPath);
        std::error_code ec;
        fs::create_directories(targetFile.parent_path(), ec);

        std::ofstream out(targetFile);
        if (!out) return false;

        // Convert wide string to utf-8
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, newRealmlist.c_str(), (int)newRealmlist.size(), NULL, 0, NULL, NULL);
        std::string utf8Host(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, newRealmlist.c_str(), (int)newRealmlist.size(), &utf8Host[0], size_needed, NULL, NULL);

        out << "set realmlist " << utf8Host << "\n";
        out.close();

        return true;
    }
}

