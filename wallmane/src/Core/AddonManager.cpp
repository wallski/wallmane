#include "pch.h"
#include "AddonManager.h"
#include <winrt/Windows.Foundation.h>
#include <windows.h>
#include <urlmon.h>
#include <filesystem>

#pragma comment(lib, "urlmon.lib")

namespace fs = std::filesystem;

namespace Core
{
    std::vector<Addon> AddonManager::GetAvailableAddons(const std::wstring& wowPath)
    {
        std::vector<Addon> list = {
            // name                folderName (what WoW sees)     description                                         url
            { L"pfQuest",
              L"pfQuest-wotlk",   // shagu/pfQuest ZIP extracts: pfQuest-master/pfQuest-wotlk/
              L"Quest helper with a huge database. Works great on Warmane.",
              L"https://github.com/shagu/pfQuest/archive/refs/heads/master.zip", false },

            { L"Deadly Boss Mods",
              L"DBM-Core",        // DBM-Warmane ZIP extracts multiple folders, DBM-Core is the key one
              L"Essential boss timers for all raids & dungeons on Warmane.",
              L"https://github.com/Zidras/DBM-Warmane/archive/refs/heads/main.zip", false },

            { L"Recount",
              L"Recount",
              L"Graphical DPS & healing meter. Shows damage, heals, deaths and more.",
              L"https://github.com/guldo77/Recount-AddonsCustom-3.3.5/archive/refs/heads/master.zip", false },

            { L"ElvUI",
              L"ElvUI",           // ElvUI-WotLK ZIP extracts: ElvUI-master/ElvUI/ and ElvUI_OptionsUI/
              L"Full UI replacement with a sleek, modern look and tons of features.",
              L"https://github.com/ElvUI-WotLK/ElvUI/archive/refs/heads/master.zip", false },

            { L"OmniCC",
              L"OmniCC",
              L"Shows cooldown countdowns as numbers directly on your spell icons.",
              L"https://github.com/tullamods/OmniCC/archive/refs/heads/master.zip", false },
        };

        if (!wowPath.empty() && fs::exists(wowPath))
        {
            // wowPath = full path to Wow.exe, so parent = WoW install dir
            auto addonsDir = fs::path(wowPath).parent_path() / L"Interface" / L"AddOns";
            for (auto& addon : list)
            {
                if (fs::exists(addonsDir / addon.folderName))
                    addon.isInstalled = true;
            }
        }
        return list;
    }

    bool AddonManager::IsHDPatchInstalled(const std::wstring& wowPath)
    {
        if (wowPath.empty()) return false;
        auto patchFile = fs::path(wowPath).parent_path() / L"Data" / L"patch-w.mpq";
        return fs::exists(patchFile);
    }

    // ─────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────
    static bool RunHiddenProcess(std::wstring cmdLine)
    {
        STARTUPINFOW si = { sizeof(si) };
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};

        BOOL ok = CreateProcessW(
            nullptr, cmdLine.data(),
            nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, nullptr, &si, &pi);

        if (ok)
        {
            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        return ok;
    }

    // GitHub ZIPs always wrap everything in a single "reponame-branch/" folder.
    // This function moves the CONTENTS of that wrapper into destDir cleanly.
    static void ExtractAndFlatten(const fs::path& zipPath, const fs::path& destDir)
    {
        // Extract to a temporary staging dir first
        fs::path stagingDir = zipPath.parent_path() / L"_addon_staging";
        std::error_code ec;
        fs::remove_all(stagingDir, ec);
        fs::create_directories(stagingDir);

        std::wstring cmd = L"cmd.exe /c tar -xf \""
            + zipPath.wstring() + L"\" -C \""
            + stagingDir.wstring() + L"\"";
        RunHiddenProcess(cmd);

        // The staging dir now has one top-level folder (e.g. "pfQuest-master/")
        // Move every directory INSIDE it to destDir
        for (auto& topEntry : fs::directory_iterator(stagingDir, ec))
        {
            if (!topEntry.is_directory()) continue;

            // Check if this top folder itself has a .toc → it's a direct addon folder
            bool hasToC = false;
            for (auto& f : fs::directory_iterator(topEntry, ec))
                if (f.path().extension() == L".toc") { hasToC = true; break; }

            if (hasToC)
            {
                // Move directly to AddOns/<foldername>
                fs::path dest = destDir / topEntry.path().filename();
                fs::remove_all(dest, ec);
                fs::rename(topEntry, dest, ec);
            }
            else
            {
                // It's a wrapper (pfQuest-master/) — move its sub-folders to AddOns
                for (auto& inner : fs::directory_iterator(topEntry, ec))
                {
                    if (!inner.is_directory()) continue;
                    fs::path dest = destDir / inner.path().filename();
                    fs::remove_all(dest, ec);
                    fs::rename(inner, dest, ec);
                }
            }
        }

        fs::remove_all(stagingDir, ec);
    }

    // ─────────────────────────────────────────────────────────────────
    // Install Addon
    // ─────────────────────────────────────────────────────────────────
    winrt::Windows::Foundation::IAsyncAction AddonManager::InstallAddonAsync(Addon addon, std::wstring wowPath)
    {
        co_await winrt::resume_background();

        if (wowPath.empty()) co_return;

        // COM required by URLDownloadToFileW
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        fs::path wowDir    = fs::path(wowPath).parent_path();
        fs::path addonsDir = wowDir / L"Interface" / L"AddOns";
        fs::path tempZip   = wowDir / L"temp_addon.zip";

        fs::create_directories(addonsDir);

        HRESULT hr = URLDownloadToFileW(nullptr, addon.downloadUrl.c_str(), tempZip.c_str(), 0, nullptr);
        if (SUCCEEDED(hr))
        {
            ExtractAndFlatten(tempZip, addonsDir);
            std::error_code ec;
            fs::remove(tempZip, ec);
        }

        CoUninitialize();
    }

    // ─────────────────────────────────────────────────────────────────
    // Install HD Patch
    // ─────────────────────────────────────────────────────────────────
    winrt::Windows::Foundation::IAsyncAction AddonManager::InstallHDPatchAsync(std::wstring patchUrl, std::wstring wowPath)
    {
        co_await winrt::resume_background();

        if (wowPath.empty()) co_return;

        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        fs::path wowDir  = fs::path(wowPath).parent_path();
        fs::path dataDir = wowDir / L"Data";
        fs::create_directories(dataDir);

        // patch-w.mpq is loaded after all stock Blizzard patches alphabetically
        fs::path dest = dataDir / L"patch-w.mpq";
        URLDownloadToFileW(nullptr, patchUrl.c_str(), dest.c_str(), 0, nullptr);

        CoUninitialize();
    }
}
