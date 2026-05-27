#pragma once
#include <string>
#include <vector>

namespace Core
{
    struct NewsItem {
        std::wstring title;
        std::wstring date;
        std::wstring url;
    };

    struct RealmStatus {
        std::wstring name;
        int population;
    };

    class NewsFetcher
    {
    public:
        static std::vector<NewsItem> GetNews();
        static std::vector<RealmStatus> GetRealmStatus();
    };
}
