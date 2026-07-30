#include "pch.h"
#include "AddonManager.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Storage.Streams.h>
#include <windows.h>
#include <filesystem>
#include <regex>
#include <fstream>
#include <sstream>
#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

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

    std::vector<TrackedAddonMeta> AddonManager::LoadTrackedAddons()
    {
        std::vector<TrackedAddonMeta> list;
        try {
            fs::path file = GetConfigDirectory() / L"installed_addons.json";
            if (fs::exists(file)) {
                std::ifstream in(file);
                json j;
                in >> j;
                if (j.contains("addons") && j["addons"].is_array()) {
                    for (const auto& item : j["addons"]) {
                        TrackedAddonMeta meta;
                        std::string f = item.value("folderName", "");
                        std::string p = item.value("pageUrl", "");
                        std::string u = item.value("lastUpdate", "");

                        int szF = MultiByteToWideChar(CP_UTF8, 0, f.c_str(), -1, NULL, 0);
                        meta.folderName.resize(szF ? szF - 1 : 0);
                        if (szF > 1) MultiByteToWideChar(CP_UTF8, 0, f.c_str(), -1, &meta.folderName[0], szF);

                        int szP = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, NULL, 0);
                        meta.pageUrl.resize(szP ? szP - 1 : 0);
                        if (szP > 1) MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &meta.pageUrl[0], szP);

                        int szU = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, NULL, 0);
                        meta.lastUpdate.resize(szU ? szU - 1 : 0);
                        if (szU > 1) MultiByteToWideChar(CP_UTF8, 0, u.c_str(), -1, &meta.lastUpdate[0], szU);

                        list.push_back(meta);
                    }
                }
            }
        } catch(...) {}
        return list;
    }

    void AddonManager::SaveTrackedAddons(const std::vector<TrackedAddonMeta>& addons)
    {
        try {
            json j;
            j["addons"] = json::array();
            for (const auto& item : addons) {
                int szF = WideCharToMultiByte(CP_UTF8, 0, item.folderName.c_str(), -1, NULL, 0, NULL, NULL);
                std::string f(szF ? szF - 1 : 0, 0);
                if (szF > 1) WideCharToMultiByte(CP_UTF8, 0, item.folderName.c_str(), -1, &f[0], szF, NULL, NULL);

                int szP = WideCharToMultiByte(CP_UTF8, 0, item.pageUrl.c_str(), -1, NULL, 0, NULL, NULL);
                std::string p(szP ? szP - 1 : 0, 0);
                if (szP > 1) WideCharToMultiByte(CP_UTF8, 0, item.pageUrl.c_str(), -1, &p[0], szP, NULL, NULL);

                int szU = WideCharToMultiByte(CP_UTF8, 0, item.lastUpdate.c_str(), -1, NULL, 0, NULL, NULL);
                std::string u(szU ? szU - 1 : 0, 0);
                if (szU > 1) WideCharToMultiByte(CP_UTF8, 0, item.lastUpdate.c_str(), -1, &u[0], szU, NULL, NULL);

                json entry;
                entry["folderName"] = f;
                entry["pageUrl"] = p;
                entry["lastUpdate"] = u;
                j["addons"].push_back(entry);
            }

            fs::path file = GetConfigDirectory() / L"installed_addons.json";
            std::ofstream out(file);
            out << j.dump(4);
        } catch(...) {}
    }

    void AddonManager::TrackAddon(const std::wstring& folderName, const std::wstring& pageUrl, const std::wstring& lastUpdate)
    {
        auto tracked = LoadTrackedAddons();
        bool found = false;
        for (auto& t : tracked) {
            if (_wcsicmp(t.folderName.c_str(), folderName.c_str()) == 0) {
                if (!pageUrl.empty()) t.pageUrl = pageUrl;
                if (!lastUpdate.empty()) t.lastUpdate = lastUpdate;
                found = true;
                break;
            }
        }
        if (!found) {
            TrackedAddonMeta meta;
            meta.folderName = folderName;
            meta.pageUrl = pageUrl;
            meta.lastUpdate = lastUpdate;
            tracked.push_back(meta);
        }
        SaveTrackedAddons(tracked);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AddonManager::GetLatestUpdateDateAsync(std::wstring pageUrl)
    {
        if (pageUrl.empty()) co_return L"";
        try {
            winrt::Windows::Web::Http::HttpClient client;
            winrt::hstring html = co_await client.GetStringAsync(winrt::Windows::Foundation::Uri(pageUrl));
            std::wstring htmlStr = html.c_str();

            std::wregex rx(L"Last Update</span>\\s*<span[^>]*>([^<]+)</span>", std::regex_constants::icase);
            std::wsmatch m;
            if (std::regex_search(htmlStr, m, rx)) {
                co_return winrt::hstring(m[1].str());
            }
        } catch(...) {}
        co_return L"";
    }

    std::vector<Addon> AddonManager::GetInstalledAddons(const std::wstring& wowPath)
    {
        std::vector<Addon> list;
        if (wowPath.empty() || !fs::exists(wowPath)) return list;

        auto addonsDir = fs::path(wowPath).parent_path() / L"Interface" / L"AddOns";
        if (!fs::exists(addonsDir)) return list;

        auto trackedList = LoadTrackedAddons();

        std::error_code ec;
        for (auto& entry : fs::directory_iterator(addonsDir, ec))
        {
            if (!entry.is_directory()) continue;

            std::wstring folderName = entry.path().filename().wstring();
            fs::path tocPath = entry.path() / (folderName + L".toc");

            if (fs::exists(tocPath))
            {
                Addon a;
                a.folderName = folderName;
                a.name = folderName; // fallback
                a.description = L"Local Addon";
                a.isInstalled = true;

                for (const auto& t : trackedList) {
                    if (_wcsicmp(t.folderName.c_str(), folderName.c_str()) == 0) {
                        a.pageUrl = t.pageUrl;
                        a.lastUpdate = t.lastUpdate;
                        break;
                    }
                }

                try {
                    std::ifstream f(tocPath);
                    std::string line;
                    while (std::getline(f, line))
                    {
                        if (line.find("## Title:") == 0)
                        {
                            std::string t = line.substr(9);
                            t.erase(0, t.find_first_not_of(" \t\r\n"));
                            t.erase(t.find_last_not_of(" \t\r\n") + 1);

                            std::regex colorRegex("\\|c[a-fA-F0-9]{8}|\\|r");
                            t = std::regex_replace(t, colorRegex, "");

                            int size_needed = MultiByteToWideChar(CP_UTF8, 0, &t[0], (int)t.size(), NULL, 0);
                            std::wstring wstrTo(size_needed, 0);
                            MultiByteToWideChar(CP_UTF8, 0, &t[0], (int)t.size(), &wstrTo[0], size_needed);
                            a.name = wstrTo;
                        }
                        else if (line.find("## Notes:") == 0)
                        {
                            std::string n = line.substr(9);
                            n.erase(0, n.find_first_not_of(" \t\r\n"));
                            n.erase(n.find_last_not_of(" \t\r\n") + 1);

                            std::regex colorRegex("\\|c[a-fA-F0-9]{8}|\\|r");
                            n = std::regex_replace(n, colorRegex, "");

                            int size_needed = MultiByteToWideChar(CP_UTF8, 0, &n[0], (int)n.size(), NULL, 0);
                            std::wstring wstrTo(size_needed, 0);
                            MultiByteToWideChar(CP_UTF8, 0, &n[0], (int)n.size(), &wstrTo[0], size_needed);
                            a.description = wstrTo;
                        }
                    }
                }
                catch (...) {}

                list.push_back(a);
            }
        }
        return list;
    }



    std::vector<Addon> AddonManager::SearchAddons(const std::wstring& query, const std::wstring& wowPath)
    {
        std::vector<Addon> results;
        if (query.empty()) return results;

        try
        {
            winrt::Windows::Web::Http::HttpClient client;
            std::wstring url = L"https://felbite.com/?s=" + query + L"&post_type=addon";
            winrt::hstring html = client.GetStringAsync(winrt::Windows::Foundation::Uri(url)).get();
            std::wstring htmlStr = html.c_str();

            std::wregex blockRegex(L"<a class=\"card card-wide[^\"]*\" href=\"([^\"]+)\">([\\s\\S]*?)</a>");
            std::wsregex_iterator it(htmlStr.begin(), htmlStr.end(), blockRegex);
            std::wsregex_iterator end;

            std::wregex imgRegex(L"(?:data-src|src)=\"([^\"]+)\"");
            std::wregex titleRegex(L"<h5[^>]*>([^<]+)</h5>");
            std::wregex descRegex(L"<p class=\"text-light[^>]*>([^<]+)</p>");

            auto addonsDir = wowPath.empty() ? fs::path() : fs::path(wowPath).parent_path() / L"Interface" / L"AddOns";

            for (; it != end; ++it)
            {
                Addon a;
                a.pageUrl = (*it)[1].str();
                std::wstring innerHtml = (*it)[2].str();

                std::wsmatch match;
                if (std::regex_search(innerHtml, match, titleRegex)) a.name = match[1].str();
                if (std::regex_search(innerHtml, match, descRegex)) a.description = match[1].str();
                if (std::regex_search(innerHtml, match, imgRegex)) a.thumbnailUrl = match[1].str();

                std::wstring sanitized = a.name;
                sanitized.erase(std::remove(sanitized.begin(), sanitized.end(), L' '), sanitized.end());
                a.folderName = sanitized;
                a.isInstalled = false;

                if (!wowPath.empty() && fs::exists(addonsDir / a.folderName))
                {
                    a.isInstalled = true;
                }

                if (!a.name.empty()) results.push_back(a);
            }
        }
        catch (...) {}

        return results;
    }

    std::wstring AddonManager::GetDownloadUrl(const std::wstring& pageUrl)
    {
        if (pageUrl.empty()) return L"";
        try
        {
            winrt::Windows::Web::Http::HttpClient client;
            winrt::hstring html = client.GetStringAsync(winrt::Windows::Foundation::Uri(pageUrl)).get();
            std::wstring htmlStr = html.c_str();

            // Match the real zip file URL for the WotLK expansion inside Felbite's modal
            // Avoiding [\\s\\S]*? to prevent std::regex stack overflow
            std::wregex wotlkRegex(L"<a href=\"([^\"]+\\.zip)\"[^>]*>(?:(?:<img[^>]*>)|\\s)*Wrath of the Lich King</a>");
            std::wsmatch match;
            if (std::regex_search(htmlStr, match, wotlkRegex))
            {
                return match[1].str();
            }

            // Fallback for Github releases if button text is different
            std::wregex rx2(L"<a href=\"([^\"]+\\.zip)\"[^>]*class=\"btn btn-primary");
            if (std::regex_search(htmlStr, match, rx2))
            {
                return match[1].str();
            }
        }
        catch (...) {}
        return L"";
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

    // RAII guard to ensure staging directory is always cleaned up
    struct StagingDirGuard
    {
        fs::path dir;
        StagingDirGuard(const fs::path& d) : dir(d) {}
        ~StagingDirGuard()
        {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    };

    // GitHub ZIPs always wrap everything in a single "reponame-branch/" folder.
    // This function moves the CONTENTS of that wrapper into destDir cleanly.
    static void ExtractAndFlatten(const fs::path& zipPath, const fs::path& destDir)
    {
        // Extract to a temporary staging dir first
        fs::path stagingDir = zipPath.parent_path() / L"_addon_staging";
        std::error_code ec;
        fs::remove_all(stagingDir, ec);
        fs::create_directories(stagingDir);

        // RAII guard ensures cleanup even if we crash/throw
        StagingDirGuard guard(stagingDir);

        std::wstring cmd = L"cmd.exe /c tar -xf \""
            + zipPath.wstring() + L"\" -C \""
            + stagingDir.wstring() + L"\"";
        RunHiddenProcess(cmd);

        // Find all .toc files recursively
        for (auto& entry : fs::recursive_directory_iterator(stagingDir, ec))
        {
            if (entry.is_regular_file() && entry.path().extension() == L".toc")
            {
                fs::path tocFolder = entry.path().parent_path();
                std::wstring addonName = entry.path().stem().wstring(); // The exact .toc name
                fs::path destFolder = destDir / addonName;

                if (fs::exists(tocFolder))
                {
                    fs::remove_all(destFolder, ec);
                    fs::rename(tocFolder, destFolder, ec);
                }
            }
        }

        // guard destructor cleans up stagingDir automatically
    }

    // ─────────────────────────────────────────────────────────────────
    // Install Addon
    // ─────────────────────────────────────────────────────────────────
    winrt::Windows::Foundation::IAsyncAction AddonManager::InstallAddonAsync(Addon addon, std::wstring wowPath)
    {
        co_await winrt::resume_background();

        if (wowPath.empty()) co_return;

        // If it's a scraped addon, fetch the direct zip link first
        std::wstring directUrl = addon.downloadUrl;
        if (directUrl.empty() && !addon.pageUrl.empty())
        {
            directUrl = GetDownloadUrl(addon.pageUrl);
        }

        if (directUrl.empty()) co_return; // Could not find a zip link

        fs::path wowDir = fs::path(wowPath).parent_path();
        fs::path addonsDir = wowDir / L"Interface" / L"AddOns";
        fs::path tempZip = wowDir / L"temp_addon.zip";

        fs::create_directories(addonsDir);

        // Use HttpClient for async download instead of URLDownloadToFileW
        // This avoids COM apartment issues and blocking the thread
        try
        {
            winrt::Windows::Web::Http::HttpClient client;
            winrt::Windows::Foundation::Uri uri(directUrl);

            auto response = co_await client.GetAsync(uri);
            if (!response.IsSuccessStatusCode()) co_return;

            auto buffer = co_await response.Content().ReadAsBufferAsync();

            // Write buffer to file
            {
                std::ofstream out(tempZip, std::ios::binary);
                if (!out) co_return;
                auto data = buffer.data();
                out.write(reinterpret_cast<const char*>(data), buffer.Length());
                out.close();
            }

            ExtractAndFlatten(tempZip, addonsDir);

            winrt::hstring latestDate = co_await GetLatestUpdateDateAsync(addon.pageUrl);
            TrackAddon(addon.folderName, addon.pageUrl, latestDate.c_str());

            std::error_code ec;
            fs::remove(tempZip, ec);
        }
        catch (...)
        {
            // Clean up temp zip on failure
            std::error_code ec;
            fs::remove(tempZip, ec);
        }
    }

}