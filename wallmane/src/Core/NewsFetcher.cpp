#include "pch.h"
#include "NewsFetcher.h"
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Foundation.h>
#include "json.hpp"

using namespace winrt;
using namespace Windows::Web::Http;

namespace Core
{
    std::vector<NewsItem> NewsFetcher::GetNews()
    {
        std::vector<NewsItem> news;
        try {
            news.push_back({ L"ONYXIA PHASE TWO", L"April 4, 2026", L"#" });
            news.push_back({ L"BATTLEGROUP 1 ARENA", L"March 13, 2026", L"#" });
            news.push_back({ L"BATTLEGROUP 1 SEASON", L"March 3, 2026", L"#" });
            news.push_back({ L"MAINTENANCE", L"February 16, 2026", L"#" });
            news.push_back({ L"SCHEDULED DOWNTIME", L"February 15, 2026", L"#" });
            news.push_back({ L"ONYXIA", L"January 17, 2026", L"#" });
        } catch(...) {
            news.push_back({ L"Failed to load news.", L"", L"" });
        }
        return news;
    }

    std::vector<RealmStatus> NewsFetcher::GetRealmStatus()
    {
        std::vector<RealmStatus> realms;
        try {
            realms.push_back({ L"Onyxia", 12000 });
            realms.push_back({ L"Lordaeron", 6838 });
            realms.push_back({ L"Icecrown", 12000 });
            realms.push_back({ L"Blackrock", 1540 });
        } catch(...) {
        }
        return realms;
    }
}
