#include "pch.h"
#include "MainWindow.xaml.h"
#include "resource.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "src/Core/WowDetector.h"
#include "src/Core/NewsFetcher.h"
#include "src/Core/DiscordRPC.h"
#include "src/Core/AddonManager.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.Web.WebView2.Core.h>
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Composition.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <filesystem>
#include <random>
#include <map>
#include <regex>
#include <sstream>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Shapes;
using namespace Windows::Storage;
using namespace Windows::Foundation;
using namespace Microsoft::UI::Composition;

static std::mt19937 g_rng{ std::random_device{}() };

namespace winrt::wallmane::implementation
{
    MainWindow::MainWindow()
    {
        InitializeComponent();

        SetupCustomTitleBar();

        auto appWindow = this->AppWindow();
        appWindow.Resize({ 1080, 730 });

        auto presenter = appWindow.Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
        presenter.IsResizable(false);
        presenter.IsMaximizable(false);

        // Set the window icon from resource IDI_APPICON
        HWND hwnd = nullptr;
        this->try_as<::IWindowNative>()->get_WindowHandle(&hwnd);
        if (hwnd)
        {
            HICON hIcon = (HICON)::LoadImage(
                ::GetModuleHandle(nullptr),
                MAKEINTRESOURCE(IDI_APPICON),
                IMAGE_ICON,
                0, 0,
                LR_DEFAULTSIZE | LR_SHARED
            );
            if (hIcon)
            {
                ::SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                ::SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
            }
        }

        try
        {
            char* appdata = nullptr;
            size_t len = 0;
            _dupenv_s(&appdata, &len, "APPDATA");
            if (appdata) {
                std::filesystem::path configPath = std::filesystem::path(appdata) / L"Wallmane" / L"wowpath.txt";
                free(appdata);
                if (std::filesystem::exists(configPath)) {
                    FILE* f;
                    if (_wfopen_s(&f, configPath.c_str(), L"r, ccs=UTF-8") == 0) {
                        wchar_t buffer[MAX_PATH];
                        if (fgetws(buffer, MAX_PATH, f)) {
                            WowPathBox().Text(buffer);
                        }
                        fclose(f);
                    }
                }
            }
        }
        catch (...) {}

        // Start Discord IPC
        Core::DiscordRPC::Initialize("1349887134988713498");

        StartAnimations();
        LoadDataAsync();
    }

    // ─────────────────────────────────────────────────────────────────
    // Custom TitleBar
    // ─────────────────────────────────────────────────────────────────
    void MainWindow::SetupCustomTitleBar()
    {
        auto appWindow = this->AppWindow();
        appWindow.TitleBar().ExtendsContentIntoTitleBar(true);

        // Make buttons match the dark theme
        auto transparent = Microsoft::UI::ColorHelper::FromArgb(0, 0, 0, 0);
        auto btnHover = Microsoft::UI::ColorHelper::FromArgb(40, 255, 255, 255);
        auto btnPressed = Microsoft::UI::ColorHelper::FromArgb(20, 255, 255, 255);
        auto fg = Microsoft::UI::ColorHelper::FromArgb(255, 200, 200, 200);

        appWindow.TitleBar().ButtonBackgroundColor(transparent);
        appWindow.TitleBar().ButtonInactiveBackgroundColor(transparent);
        appWindow.TitleBar().ButtonHoverBackgroundColor(btnHover);
        appWindow.TitleBar().ButtonPressedBackgroundColor(btnPressed);
        appWindow.TitleBar().ButtonForegroundColor(fg);
        appWindow.TitleBar().ButtonHoverForegroundColor(fg);
        appWindow.TitleBar().ButtonPressedForegroundColor(fg);

        // Tell WinUI 3 which element handles dragging
        AppWindow().TitleBar().PreferredHeightOption(Microsoft::UI::Windowing::TitleBarHeightOption::Standard);
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());
    }

    // ─────────────────────────────────────────────────────────────────
    // Animations (Native GPU Composition)
    // ─────────────────────────────────────────────────────────────────
    void MainWindow::StartAnimations()
    {
        SetupCompositionRain();

        m_lightningCountdown = std::uniform_int_distribution<int>(16, 40)(g_rng);
        m_lightningTimer = DispatcherTimer();
        m_lightningTimer.Interval(std::chrono::milliseconds(500));
        m_lightningTimer.Tick({ this, &MainWindow::OnLightningTick });
        m_lightningTimer.Start();
    }

    void MainWindow::SetupCompositionRain()
    {
        // Get the compositor from our placeholder border
        auto hostVisual = Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(CompositionHost());
        auto compositor = hostVisual.Compositor();

        // Create a container to hold all our rain sprites
        auto container = compositor.CreateContainerVisual();
        Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::SetElementChildVisual(CompositionHost(), container);

        // 120 rain drops for a nice dense effect
        const int RAIN_COUNT = 120;

        for (int i = 0; i < RAIN_COUNT; i++)
        {
            double startX = std::uniform_real_distribution<double>(-200, 1400)(g_rng);
            double length = std::uniform_real_distribution<double>(12, 30)(g_rng);
            double speedMs = std::uniform_real_distribution<double>(400, 900)(g_rng);
            float opacity = std::uniform_real_distribution<float>(0.05f, 0.25f)(g_rng);

            // The visual representation of a raindrop
            auto drop = compositor.CreateSpriteVisual();
            drop.Brush(compositor.CreateColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 140, 180, 220)));
            drop.Size({ 1.5f, (float)length });
            drop.Opacity(opacity);
            drop.RotationAngleInDegrees(15.0f); // Fall at an angle

            // Animate it endlessly falling down the screen
            auto animation = compositor.CreateVector3KeyFrameAnimation();
            animation.InsertKeyFrame(0.0f, { (float)startX, -100.0f, 0.0f });
            animation.InsertKeyFrame(1.0f, { (float)(startX - 200), 800.0f, 0.0f }); // Moves left/down due to angle
            animation.Duration(std::chrono::milliseconds((int)speedMs));
            animation.IterationBehavior(AnimationIterationBehavior::Forever);

            // Random start time so they don't all fall in waves
            animation.DelayTime(std::chrono::milliseconds(std::uniform_int_distribution<int>(0, 1000)(g_rng)));

            drop.StartAnimation(L"Offset", animation);
            container.Children().InsertAtTop(drop);
        }
    }

    void MainWindow::OnLightningTick(IInspectable const&, IInspectable const&)
    {
        m_lightningCountdown--;
        if (m_lightningCountdown <= 0)
        {
            TriggerLightningFlash();
            m_lightningCountdown = std::uniform_int_distribution<int>(20, 60)(g_rng);
        }
    }

    void MainWindow::TriggerLightningFlash()
    {
        auto flash = LightningFlash();

        DispatcherTimer t1;
        t1.Interval(std::chrono::milliseconds(60));
        t1.Tick([flash, t1](auto, auto) mutable {
            flash.Opacity(0.0);
            t1.Stop();

            DispatcherTimer t2;
            t2.Interval(std::chrono::milliseconds(80));
            t2.Tick([flash, t2](auto, auto) mutable {
                flash.Opacity(0.45);
                t2.Stop();

                DispatcherTimer t3;
                t3.Interval(std::chrono::milliseconds(180));
                t3.Tick([flash, t3](auto, auto) mutable {
                    flash.Opacity(0.0);
                    t3.Stop();
                    });
                t3.Start();
                });
            t2.Start();
            });

        flash.Opacity(0.55);
        t1.Start();
    }

    // ─────────────────────────────────────────────────────────────────
    // Data Loading
    // ─────────────────────────────────────────────────────────────────
    winrt::fire_and_forget MainWindow::LoadDataAsync()
    {
        auto lifetime = get_strong();
        co_await winrt::resume_background();

        auto news = Core::NewsFetcher::GetNews();
        auto realms = Core::NewsFetcher::GetRealmStatus();

        DispatcherQueue().TryEnqueue([this, lifetime, news, realms]()
            {
                NewsPanel().Children().Clear();
                for (const auto& item : news)
                {
                    Border card;
                    card.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(30, 200, 153, 59)));
                    card.BorderBrush(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 153, 59)));
                    card.BorderThickness({ 1,1,1,1 });
                    card.CornerRadius({ 6,6,6,6 });
                    card.Padding({ 12,8,12,8 });

                    StackPanel sp;
                    sp.Spacing(2);

                    TextBlock title;
                    title.Text(item.title);
                    title.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
                    title.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 200, 153, 59)));
                    title.FontSize(12);
                    title.CharacterSpacing(100);

                    TextBlock date;
                    date.Text(item.date);
                    date.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(120, 200, 200, 200)));
                    date.FontSize(10);

                    sp.Children().Append(title);
                    sp.Children().Append(date);
                    card.Child(sp);
                    NewsPanel().Children().Append(card);
                }

                RealmPanel().Children().Clear();
                int totalPlayers = 0;
                for (const auto& realm : realms)
                {
                    totalPlayers += realm.population;
                    Grid row;
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().Append(ColumnDefinition());
                    row.ColumnDefinitions().GetAt(0).Width({ 1, GridUnitType::Star });
                    row.ColumnDefinitions().GetAt(1).Width({ 1, GridUnitType::Star });

                    TextBlock name;
                    name.Text(realm.name);
                    name.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(200, 220, 220, 220)));
                    name.FontSize(12);
                    Grid::SetColumn(name, 0);

                    uint8_t r = 80, g = 200, b = 80;
                    if (realm.population > 10000) { r = 200; g = 220; b = 80; }
                    if (realm.population < 3000) { r = 160; g = 160; b = 160; }

                    TextBlock pop;
                    pop.Text(to_hstring(realm.population) + L" online");
                    pop.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, r, g, b)));
                    pop.FontSize(12);
                    pop.HorizontalAlignment(HorizontalAlignment::Right);
                    Grid::SetColumn(pop, 1);

                    row.Children().Append(name);
                    row.Children().Append(pop);
                    RealmPanel().Children().Append(row);
                }

                TotalPlayersLabel().Text(L"Total online: " + to_hstring(totalPlayers));

                uint64_t playtimeSeconds = Core::WowDetector::GetTotalPlaytimeSeconds(WowPathBox().Text().c_str());
                PlaytimeLabel().Text(Core::WowDetector::FormatPlaytime(playtimeSeconds));
            });
    }

    // ─────────────────────────────────────────────────────────────────
    // Navigation & Addons
    // ─────────────────────────────────────────────────────────────────
    void MainWindow::NavView_SelectionChanged(NavigationView const&, NavigationViewSelectionChangedEventArgs const& args)
    {
        if (!args.SelectedItemContainer()) return;
        auto tag = unbox_value<hstring>(args.SelectedItemContainer().Tag());

        HomePage().Visibility(tag == L"home_page" ? Visibility::Visible : Visibility::Collapsed);
        AddonsPage().Visibility(tag == L"addons_page" ? Visibility::Visible : Visibility::Collapsed);
        CharactersPage().Visibility(tag == L"characters_page" ? Visibility::Visible : Visibility::Collapsed);
        SettingsPage().Visibility(tag == L"settings_page" ? Visibility::Visible : Visibility::Collapsed);

        if (tag == L"addons_page")
        {
            PerformAddonSearch(L"");
        }
        else if (tag == L"characters_page")
        {
            LoadCharacters();
        }
        else if (tag == L"settings_page")
        {
            RefreshRealmlistUI();
        }
    }

    void MainWindow::SearchAddonsBtn_Click(IInspectable const&, RoutedEventArgs const&)
    {
        PerformAddonSearch(AddonSearchBox().Text().c_str());
    }

    void MainWindow::AddonSearchBox_KeyDown(IInspectable const&, Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e)
    {
        if (e.Key() == winrt::Windows::System::VirtualKey::Enter)
        {
            PerformAddonSearch(AddonSearchBox().Text().c_str());
        }
    }

    void MainWindow::TabDiscover_Click(IInspectable const&, RoutedEventArgs const&)
    {
        TabDiscover().IsChecked(true);
        TabInstalled().IsChecked(false);
        DiscoverScrollViewer().Visibility(Visibility::Visible);
        AddonSearchContainer().Visibility(Visibility::Visible);
        InstalledScrollViewer().Visibility(Visibility::Collapsed);
    }

    void MainWindow::TabInstalled_Click(IInspectable const&, RoutedEventArgs const&)
    {
        TabInstalled().IsChecked(true);
        TabDiscover().IsChecked(false);
        DiscoverScrollViewer().Visibility(Visibility::Collapsed);
        AddonSearchContainer().Visibility(Visibility::Collapsed);
        InstalledScrollViewer().Visibility(Visibility::Visible);
        LoadInstalledAddons();
    }

    winrt::fire_and_forget MainWindow::PerformAddonSearch(std::wstring query)
    {
        auto lifetime = get_strong();
        std::wstring path = WowPathBox().Text().c_str();

        DispatcherQueue().TryEnqueue([this]() {
            AddonsPanel().Visibility(Visibility::Collapsed);
            SkeletonPanel().Visibility(Visibility::Visible);
            ShimmerAnimation().Begin();
            AddonsPanel().Children().Clear();
            SearchAddonsBtn().IsEnabled(false);
            });

        co_await winrt::resume_background();
        auto addons = Core::AddonManager::SearchAddons(query, path);

        DispatcherQueue().TryEnqueue([this, lifetime, addons]() {
            ShimmerAnimation().Stop();
            SkeletonPanel().Visibility(Visibility::Collapsed);
            AddonsPanel().Visibility(Visibility::Visible);
            SearchAddonsBtn().IsEnabled(true);
            AddonsPanel().Children().Clear();

            for (const auto& addon : addons)
            {
                Border card;
                card.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(30, 200, 153, 59)));
                card.BorderBrush(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 153, 59)));
                card.BorderThickness({ 1,1,1,1 });
                card.CornerRadius({ 6,6,6,6 });
                card.Padding({ 16,12,16,12 });

                Grid grid;
                grid.ColumnDefinitions().Append(ColumnDefinition());
                grid.ColumnDefinitions().Append(ColumnDefinition());
                grid.ColumnDefinitions().Append(ColumnDefinition());
                grid.ColumnDefinitions().GetAt(0).Width({ 60, GridUnitType::Pixel });
                grid.ColumnDefinitions().GetAt(1).Width({ 1, GridUnitType::Star });
                grid.ColumnDefinitions().GetAt(2).Width({ 1, GridUnitType::Auto });

                if (!addon.thumbnailUrl.empty())
                {
                    Microsoft::UI::Xaml::Shapes::Ellipse thumbnail;
                    thumbnail.Width(48);
                    thumbnail.Height(48);

                    Microsoft::UI::Xaml::Media::ImageBrush brush;
                    brush.Stretch(Stretch::UniformToFill);
                    brush.ImageSource(Microsoft::UI::Xaml::Media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(addon.thumbnailUrl)));

                    thumbnail.Fill(brush);
                    Grid::SetColumn(thumbnail, 0);
                    grid.Children().Append(thumbnail);
                }

                StackPanel textPanel;
                textPanel.Spacing(4);
                textPanel.VerticalAlignment(VerticalAlignment::Center);

                TextBlock name;
                name.Text(addon.name);
                name.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
                name.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 200, 153, 59)));
                name.FontSize(14);

                TextBlock desc;
                desc.Text(addon.description);
                desc.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(180, 255, 255, 255)));
                desc.FontSize(12);

                textPanel.Children().Append(name);
                textPanel.Children().Append(desc);
                Grid::SetColumn(textPanel, 1);

                Button btn;
                btn.Content(box_value(addon.isInstalled ? L"Installed" : L"Install"));
                btn.IsEnabled(!addon.isInstalled);
                btn.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(btn, 2);

                // Install Logic
                if (!addon.isInstalled)
                {
                    btn.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 30, 150, 80)));
                    btn.Foreground(SolidColorBrush(Microsoft::UI::Colors::White()));

                    btn.Click([this, addon, btn](auto, auto) mutable {
                        std::wstring p = WowPathBox().Text().c_str();
                        if (p.empty()) return;

                        btn.IsEnabled(false);
                        btn.Content(box_value(L"Installing..."));

                        // Kick off background work cleanly
                        [](auto self, auto addonCopy, auto path, auto button) -> winrt::fire_and_forget {
                            co_await Core::AddonManager::InstallAddonAsync(addonCopy, path);
                            self->DispatcherQueue().TryEnqueue([button]() mutable {
                                button.Content(winrt::box_value(L"Installed \u2713"));
                                button.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                                    winrt::Microsoft::UI::ColorHelper::FromArgb(60, 30, 200, 90)));
                                });
                            }(get_strong(), addon, p, btn);
                        });
                }
                else
                {
                    btn.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 153, 59)));
                }

                grid.Children().Append(textPanel);
                grid.Children().Append(btn);
                card.Child(grid);
                AddonsPanel().Children().Append(card);
            }
            });
    }

    void MainWindow::LoadInstalledAddons()
    {
        std::wstring path = WowPathBox().Text().c_str();
        InstalledPanel().Children().Clear();

        if (path.empty())
        {
            TextBlock tb;
            tb.Text(L"Please configure your Wow.exe path in Settings first.");
            tb.Foreground(SolidColorBrush(Microsoft::UI::Colors::Gray()));
            InstalledPanel().Children().Append(tb);
            return;
        }

        auto addons = Core::AddonManager::GetInstalledAddons(path);
        if (addons.empty())
        {
            TextBlock tb;
            tb.Text(L"No addons found in your Interface/AddOns folder.");
            tb.Foreground(SolidColorBrush(Microsoft::UI::Colors::Gray()));
            InstalledPanel().Children().Append(tb);
            return;
        }

        for (const auto& addon : addons)
        {
            Border card;
            card.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(30, 200, 153, 59)));
            card.BorderBrush(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 153, 59)));
            card.BorderThickness({ 1,1,1,1 });
            card.CornerRadius({ 6,6,6,6 });
            card.Padding({ 16,12,16,12 });

            Grid grid;
            grid.ColumnDefinitions().Append(ColumnDefinition());
            grid.ColumnDefinitions().Append(ColumnDefinition());
            grid.ColumnDefinitions().GetAt(0).Width({ 1, GridUnitType::Star });
            grid.ColumnDefinitions().GetAt(1).Width({ 1, GridUnitType::Auto });

            StackPanel textPanel;
            textPanel.Spacing(4);
            textPanel.VerticalAlignment(VerticalAlignment::Center);

            TextBlock name;
            name.Text(addon.name);
            name.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
            name.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 200, 153, 59)));
            name.FontSize(14);

            TextBlock desc;
            desc.Text(addon.description);
            desc.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(180, 255, 255, 255)));
            desc.FontSize(12);

            textPanel.Children().Append(name);
            textPanel.Children().Append(desc);

            // Show last updated date if tracked
            if (!addon.lastUpdate.empty())
            {
                TextBlock updateDate;
                updateDate.Text(L"Last updated: " + addon.lastUpdate);
                updateDate.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(120, 255, 255, 255)));
                updateDate.FontSize(11);
                textPanel.Children().Append(updateDate);
            }

            Grid::SetColumn(textPanel, 0);

            // Buttons panel
            StackPanel btnPanel;
            btnPanel.Orientation(Orientation::Horizontal);
            btnPanel.Spacing(8);
            btnPanel.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(btnPanel, 1);

            // "Update" badge - only shown if addon has a tracked pageUrl
            if (!addon.pageUrl.empty())
            {
                Button updateBtn;
                updateBtn.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(60, 50, 200, 100)));
                updateBtn.CornerRadius({ 4,4,4,4 });
                updateBtn.Padding({ 10,4,10,4 });
                updateBtn.Content(box_value(L"↑ Update"));
                updateBtn.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 80, 220, 120)));
                updateBtn.VerticalAlignment(VerticalAlignment::Center);
                updateBtn.Tag(box_value(winrt::hstring(addon.folderName)));

                updateBtn.Click([this, addon](auto sender, auto) mutable {
                    auto btn = sender.as<Button>();
                    btn.IsEnabled(false);
                    btn.Content(box_value(L"Updating..."));
                    std::wstring p = WowPathBox().Text().c_str();

                    [](auto self, auto a, auto path, auto btn) -> winrt::fire_and_forget {
                        co_await Core::AddonManager::InstallAddonAsync(a, path);
                        self->DispatcherQueue().TryEnqueue([self, btn]() {
                            self->LoadInstalledAddons();
                        });
                    }(get_strong(), addon, p, btn);
                });

                btnPanel.Children().Append(updateBtn);
            }

            Button removeTag;
            removeTag.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 50, 50)));
            removeTag.CornerRadius({ 4,4,4,4 });
            removeTag.Padding({ 10,4,10,4 });
            removeTag.Content(box_value(L"Remove"));
            removeTag.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 255, 100, 100)));
            removeTag.VerticalAlignment(VerticalAlignment::Center);

            removeTag.Click([this, addon, removeTag](auto, auto) mutable {
                std::wstring p = WowPathBox().Text().c_str();
                if (p.empty()) return;

                removeTag.IsEnabled(false);
                removeTag.Content(box_value(L"Removing..."));

                [](auto self, auto folder, auto path) -> winrt::fire_and_forget {
                    co_await winrt::resume_background();
                    std::error_code ec;
                    std::filesystem::path addonDir = std::filesystem::path(path).parent_path() / L"Interface" / L"AddOns" / folder;
                    if (std::filesystem::exists(addonDir)) {
                        std::filesystem::remove_all(addonDir, ec);
                    }
                    self->DispatcherQueue().TryEnqueue([self]() {
                        self->LoadInstalledAddons();
                        });
                    }(get_strong(), addon.folderName, p);
                });

            btnPanel.Children().Append(removeTag);

            grid.Children().Append(textPanel);
            grid.Children().Append(btnPanel);
            card.Child(grid);
            InstalledPanel().Children().Append(card);
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // Characters & Armory
    // ─────────────────────────────────────────────────────────────────
    void MainWindow::LoadCharacters()
    {
        std::wstring path = WowPathBox().Text().c_str();
        CharacterCardsPanel().Children().Clear();

        if (path.empty())
        {
            TextBlock tb;
            tb.Text(L"Please configure your Wow.exe path in Settings first.");
            tb.Foreground(SolidColorBrush(Microsoft::UI::Colors::Gray()));
            CharacterCardsPanel().Children().Append(tb);
            return;
        }

        std::filesystem::path accountsDir = std::filesystem::path(path).parent_path() / L"WTF" / L"Account";
        if (!std::filesystem::exists(accountsDir))
        {
            TextBlock tb;
            tb.Text(L"No WTF/Account folder found. Have you logged in yet?");
            tb.Foreground(SolidColorBrush(Microsoft::UI::Colors::Gray()));
            CharacterCardsPanel().Children().Append(tb);
            return;
        }

        // Ensure CoreWebView2 is initializing
        ArmoryWebView().EnsureCoreWebView2Async();

        try
        {
            for (const auto& accountEntry : std::filesystem::directory_iterator(accountsDir))
            {
                if (!accountEntry.is_directory()) continue;
                std::wstring accName = accountEntry.path().filename().wstring();
                if (accName == L"SavedVariables") continue;

                for (const auto& realmEntry : std::filesystem::directory_iterator(accountEntry.path()))
                {
                    if (!realmEntry.is_directory()) continue;
                    std::wstring realmName = realmEntry.path().filename().wstring();
                    if (realmName == L"SavedVariables") continue;

                    for (const auto& charEntry : std::filesystem::directory_iterator(realmEntry.path()))
                    {
                        if (!charEntry.is_directory()) continue;
                        std::wstring charName = charEntry.path().filename().wstring();
                        if (charName == L"SavedVariables") continue;

                        // Create UI Card
                        Button card;
                        card.HorizontalAlignment(HorizontalAlignment::Stretch);
                        card.HorizontalContentAlignment(HorizontalAlignment::Left);
                        card.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(30, 200, 153, 59)));
                        card.BorderBrush(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 153, 59)));
                        card.BorderThickness({ 1,1,1,1 });
                        card.CornerRadius({ 6,6,6,6 });
                        card.Padding({ 16,12,16,12 });

                        StackPanel sp;
                        sp.Spacing(4);

                        TextBlock nameBlock;
                        nameBlock.Text(charName);
                        nameBlock.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
                        nameBlock.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 200, 153, 59)));
                        nameBlock.FontSize(16);

                        TextBlock realmBlock;
                        realmBlock.Text(realmName);
                        realmBlock.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(180, 255, 255, 255)));
                        realmBlock.FontSize(12);

                        sp.Children().Append(nameBlock);
                        sp.Children().Append(realmBlock);
                        card.Content(sp);

                        card.Click([this, charName, realmName](auto, auto) {
                            ArmoryPlaceholder().Visibility(Visibility::Collapsed);
                            NativeArmoryLayout().Visibility(Visibility::Visible);

                            // Set WebView transparent so it blends perfectly
                            ArmoryWebView().DefaultBackgroundColor(winrt::Microsoft::UI::Colors::Transparent());

                            std::wstring charStr = charName.c_str();
                            std::wstring realmStr = realmName.c_str();
                            std::wstring cacheFile = L"cache_" + charStr + L"_" + realmStr + L".json";

                            try {
                                char* appdata = nullptr;
                                size_t len = 0;
                                _dupenv_s(&appdata, &len, "APPDATA");
                                if (appdata) {
                                    std::filesystem::path cachePath = std::filesystem::path(appdata) / L"Wallmane" / L"Cache";
                                    free(appdata);
                                    if (std::filesystem::exists(cachePath / cacheFile)) {
                                        FILE* f;
                                        if (_wfopen_s(&f, (cachePath / cacheFile).c_str(), L"rt, ccs=UTF-8") == 0) {
                                            std::wstring content;
                                            wchar_t buf[1024];
                                            while (fgetws(buf, 1024, f)) {
                                                content += buf;
                                            }
                                            fclose(f);
                                            if (!content.empty()) {
                                                UpdateArmoryUI(winrt::hstring(content));
                                            }
                                        }
                                    }
                                }
                            }
                            catch (...) {}

                            std::wstring url = L"https://armory.warmane.com/character/" + charStr + L"/" + realmStr + L"/summary";
                            ArmoryWebView().Source(winrt::Windows::Foundation::Uri(url));
                            });

                        CharacterCardsPanel().Children().Append(card);
                    }
                }
            }
        }
        catch (...) {}

        if (CharacterCardsPanel().Children().Size() == 0)
        {
            TextBlock tb;
            tb.Text(L"No characters found.");
            tb.Foreground(SolidColorBrush(Microsoft::UI::Colors::Gray()));
            CharacterCardsPanel().Children().Append(tb);
        }
    }

    static winrt::Windows::UI::Color GetWoWColor(const std::wstring& className) {
        if (className == L"q0" || className == L"poor") return Microsoft::UI::ColorHelper::FromArgb(255, 157, 157, 157); // Gray
        if (className == L"q1" || className == L"common") return Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255); // White
        if (className == L"q2" || className == L"uncommon") return Microsoft::UI::ColorHelper::FromArgb(255, 30, 255, 0); // Green
        if (className == L"q3" || className == L"rare") return Microsoft::UI::ColorHelper::FromArgb(255, 0, 112, 221); // Blue
        if (className == L"q4" || className == L"epic") return Microsoft::UI::ColorHelper::FromArgb(255, 163, 53, 238); // Purple
        if (className == L"q5" || className == L"legendary") return Microsoft::UI::ColorHelper::FromArgb(255, 255, 128, 0); // Orange
        if (className == L"q6" || className == L"artifact") return Microsoft::UI::ColorHelper::FromArgb(255, 229, 204, 128); // Gold
        if (className == L"q8") return Microsoft::UI::ColorHelper::FromArgb(255, 230, 204, 128); // Heirloom
        
        // Classes
        if (className == L"c1") return Microsoft::UI::ColorHelper::FromArgb(255, 199, 156, 110); // Warrior
        if (className == L"c2") return Microsoft::UI::ColorHelper::FromArgb(255, 245, 140, 186); // Paladin
        if (className == L"c3") return Microsoft::UI::ColorHelper::FromArgb(255, 171, 212, 115); // Hunter
        if (className == L"c4") return Microsoft::UI::ColorHelper::FromArgb(255, 255, 245, 105); // Rogue
        if (className == L"c5") return Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255); // Priest
        if (className == L"c6") return Microsoft::UI::ColorHelper::FromArgb(255, 196, 31, 59); // Death Knight
        if (className == L"c7") return Microsoft::UI::ColorHelper::FromArgb(255, 0, 112, 222); // Shaman
        if (className == L"c8") return Microsoft::UI::ColorHelper::FromArgb(255, 105, 204, 240); // Mage
        if (className == L"c9") return Microsoft::UI::ColorHelper::FromArgb(255, 148, 130, 201); // Warlock
        if (className == L"c11") return Microsoft::UI::ColorHelper::FromArgb(255, 255, 125, 10); // Druid
        
        // Money
        if (className == L"moneygold") return Microsoft::UI::ColorHelper::FromArgb(255, 229, 197, 25);
        if (className == L"moneysilver") return Microsoft::UI::ColorHelper::FromArgb(255, 162, 162, 162);
        if (className == L"moneycopper") return Microsoft::UI::ColorHelper::FromArgb(255, 198, 125, 63);

        return Microsoft::UI::ColorHelper::FromArgb(255, 220, 220, 220); // Default light gray
    }

    static std::wstring ExtractTooltipHtml(const std::wstring& jsResponse) {
        size_t pos = jsResponse.find(L"tooltip_enus: '");
        if (pos == std::wstring::npos) {
            pos = jsResponse.find(L"tooltip_enus: \"");
            if (pos == std::wstring::npos) return L"";
            pos += 15;
            size_t endPos = jsResponse.find(L"\"", pos);
            while (endPos != std::wstring::npos && jsResponse[endPos - 1] == L'\\') {
                endPos = jsResponse.find(L"\"", endPos + 1);
            }
            if (endPos == std::wstring::npos) return L"";
            return jsResponse.substr(pos, endPos - pos);
        }
        pos += 15;
        size_t endPos = jsResponse.find(L"'", pos);
        while (endPos != std::wstring::npos && jsResponse[endPos - 1] == L'\\') {
            endPos = jsResponse.find(L"'", endPos + 1);
        }
        if (endPos == std::wstring::npos) return L"";
        std::wstring rawHtml = jsResponse.substr(pos, endPos - pos);
        
        // Unescape: \" -> ", \/ -> /, \' -> ', \n -> newline
        std::wstring cleanHtml;
        cleanHtml.reserve(rawHtml.size());
        for (size_t i = 0; i < rawHtml.size(); ++i) {
            if (rawHtml[i] == L'\\' && i + 1 < rawHtml.size()) {
                wchar_t next = rawHtml[i + 1];
                if (next == L'"' || next == L'/' || next == L'\'' || next == L'\\') {
                    cleanHtml += next;
                    ++i;
                } else if (next == L'n') {
                    cleanHtml += L'\n';
                    ++i;
                } else {
                    cleanHtml += L'\\';
                }
            } else {
                cleanHtml += rawHtml[i];
            }
        }
        return cleanHtml;
    }

    static std::vector<std::wstring> GetTopLevelTables(const std::wstring& html) {
        std::vector<std::wstring> tables;
        size_t pos = 0;
        while (true) {
            size_t start = html.find(L"<table", pos);
            if (start == std::wstring::npos) break;
            
            int nest = 1;
            size_t searchPos = start + 6;
            size_t end = std::wstring::npos;
            while (nest > 0 && searchPos < html.size()) {
                size_t nextOpen = html.find(L"<table", searchPos);
                size_t nextClose = html.find(L"</table>", searchPos);
                if (nextClose == std::wstring::npos) break;
                
                if (nextOpen != std::wstring::npos && nextOpen < nextClose) {
                    nest++;
                    searchPos = nextOpen + 6;
                } else {
                    nest--;
                    if (nest == 0) {
                        end = nextClose;
                    }
                    searchPos = nextClose + 8;
                }
            }
            if (end != std::wstring::npos) {
                tables.push_back(html.substr(start, end + 8 - start));
                pos = end + 8;
            } else {
                break;
            }
        }
        return tables;
    }

    static std::vector<std::wstring> GetRows(const std::wstring& tableContent) {
        std::vector<std::wstring> rows;
        size_t pos = 0;
        while (true) {
            size_t start = tableContent.find(L"<tr", pos);
            if (start == std::wstring::npos) break;
            size_t end = tableContent.find(L"</tr>", start);
            if (end == std::wstring::npos) break;
            rows.push_back(tableContent.substr(start, end + 5 - start));
            pos = end + 5;
        }
        return rows;
    }

    static std::vector<std::wstring> GetCells(const std::wstring& rowContent) {
        std::vector<std::wstring> cells;
        size_t pos = 0;
        while (true) {
            size_t start = rowContent.find(L"<td", pos);
            if (start == std::wstring::npos) {
                start = rowContent.find(L"<th", pos);
            }
            if (start == std::wstring::npos) break;
            
            size_t end = rowContent.find(L"</td>", start);
            size_t endTagLen = 5;
            if (end == std::wstring::npos) {
                end = rowContent.find(L"</th>", start);
                endTagLen = 5;
            }
            if (end == std::wstring::npos) break;
            
            size_t openTagEnd = rowContent.find(L">", start);
            if (openTagEnd == std::wstring::npos || openTagEnd > end) break;
            
            cells.push_back(rowContent.substr(openTagEnd + 1, end - (openTagEnd + 1)));
            pos = end + endTagLen;
        }
        return cells;
    }

    static TextBlock ParseInlineHtml(const std::wstring& inlineHtml) {
        TextBlock tb;
        tb.TextWrapping(TextWrapping::Wrap);
        
        size_t pos = 0;
        struct StyleState {
            winrt::Windows::UI::Color color;
            bool isBold;
            bool isSmall;
        };
        std::vector<StyleState> styleStack;
        styleStack.push_back({ Microsoft::UI::ColorHelper::FromArgb(255, 220, 220, 220), false, false });
        
        auto getActiveColor = [&]() { return styleStack.back().color; };
        auto getActiveBold = [&]() { return styleStack.back().isBold; };
        auto getActiveSmall = [&]() { return styleStack.back().isSmall; };
        
        while (pos < inlineHtml.size()) {
            if (inlineHtml[pos] == L'<') {
                size_t tagEnd = inlineHtml.find(L'>', pos);
                if (tagEnd == std::wstring::npos) {
                    winrt::Microsoft::UI::Xaml::Documents::Run run;
                    run.Text(inlineHtml.substr(pos));
                    run.Foreground(SolidColorBrush(getActiveColor()));
                    if (getActiveBold()) run.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
                    if (getActiveSmall()) run.FontSize(10);
                    tb.Inlines().Append(run);
                    break;
                }
                
                std::wstring tag = inlineHtml.substr(pos + 1, tagEnd - pos - 1);
                pos = tagEnd + 1;
                
                if (tag.rfind(L"/", 0) == 0) {
                    if (styleStack.size() > 1) {
                        styleStack.pop_back();
                    }
                } else if (tag == L"br" || tag == L"br/" || tag == L"br /" || tag.rfind(L"br ", 0) == 0) {
                    // Line break — insert a LineBreak inline element
                    winrt::Microsoft::UI::Xaml::Documents::LineBreak lb;
                    tb.Inlines().Append(lb);
                } else {
                    StyleState nextStyle = styleStack.back();
                    size_t classPos = tag.find(L"class=");
                    if (classPos != std::wstring::npos) {
                        size_t quoteStart = tag.find_first_of(L"\"'", classPos);
                        if (quoteStart != std::wstring::npos) {
                            size_t quoteEnd = tag.find_first_of(L"\"'", quoteStart + 1);
                            if (quoteEnd != std::wstring::npos) {
                                std::wstring className = tag.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
                                nextStyle.color = GetWoWColor(className);
                            }
                        }
                    }
                    
                    if (tag.rfind(L"b", 0) == 0) {
                        nextStyle.isBold = true;
                    } else if (tag.rfind(L"small", 0) == 0) {
                        nextStyle.isSmall = true;
                    }
                    
                    styleStack.push_back(nextStyle);
                }
            } else {
                size_t nextTag = inlineHtml.find(L'<', pos);
                std::wstring text = (nextTag == std::wstring::npos) ? inlineHtml.substr(pos) : inlineHtml.substr(pos, nextTag - pos);
                pos = (nextTag == std::wstring::npos) ? inlineHtml.size() : nextTag;
                
                text = std::regex_replace(text, std::wregex(L"&nbsp;"), L" ");
                text = std::regex_replace(text, std::wregex(L"&gt;"), L">");
                text = std::regex_replace(text, std::wregex(L"&lt;"), L"<");
                text = std::regex_replace(text, std::wregex(L"&amp;"), L"&");
                
                if (!text.empty()) {
                    winrt::Microsoft::UI::Xaml::Documents::Run run;
                    run.Text(text);
                    run.Foreground(SolidColorBrush(getActiveColor()));
                    if (getActiveBold()) run.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
                    if (getActiveSmall()) {
                        run.FontSize(10);
                    } else {
                        run.FontSize(12);
                    }
                    tb.Inlines().Append(run);
                }
            }
        }
        return tb;
    }

    // Extract inner content of all <td>/<th> cells within a <tr> block
    static std::vector<std::wstring> ExtractTdContents(const std::wstring& rowHtml) {
        std::vector<std::wstring> cells;
        std::wregex tdRx(L"<t[dh][^>]*>([\\s\\S]*?)<\/t[dh]>", std::regex_constants::icase);
        auto begin = std::wsregex_iterator(rowHtml.begin(), rowHtml.end(), tdRx);
        auto end   = std::wsregex_iterator();
        for (auto it = begin; it != end; ++it)
            cells.push_back((*it)[1].str());
        return cells;
    }

    static void ParseTooltipHtml(const std::wstring& html, StackPanel const& parentPanel) {
        std::wregex trRx(L"<tr[^>]*>([\\s\\S]*?)<\/tr>", std::regex_constants::icase);
        std::wregex brRx(L"<br\\s*/?>",              std::regex_constants::icase);
        std::wregex commentRx(L"<!--[\\s\\S]*?-->",  std::regex_constants::icase);

        // Emit one text segment as a TextBlock (strips comments, trims)
        auto emitLine = [&](std::wstring line) {
            line = std::regex_replace(line, commentRx, L"");
            line = std::regex_replace(line, std::wregex(L"^\\s+|\\s+$"), L"");
            if (!line.empty())
                parentPanel.Children().Append(ParseInlineHtml(line));
        };

        // Split a text block on <br> and emit each segment
        auto emitBrBlock = [&](const std::wstring& block) {
            std::wsregex_token_iterator it(block.begin(), block.end(), brRx, -1);
            std::wsregex_token_iterator itEnd;
            for (; it != itEnd; ++it) emitLine(it->str());
        };

        size_t pos = 0;
        auto trBegin = std::wsregex_iterator(html.begin(), html.end(), trRx);
        auto trEnd   = std::wsregex_iterator();

        for (auto it = trBegin; it != trEnd; ++it) {
            // Emit bare text BEFORE this <tr>
            size_t trStart = (size_t)it->position();
            if (trStart > pos)
                emitBrBlock(html.substr(pos, trStart - pos));

            // Extract td/th cells from this row
            std::wstring rowInner = (*it)[1].str();
            auto cells = ExtractTdContents(rowInner);

            if (cells.size() >= 2) {
                // Two-column row (e.g. "172–259 Damage" | "Speed 1.80")
                Grid grid;
                grid.Margin({ 0, 1, 0, 1 });
                ColumnDefinition col0, col1;
                col0.Width({ 1, GridUnitType::Star });
                col1.Width({ 1, GridUnitType::Auto });
                grid.ColumnDefinitions().Append(col0);
                grid.ColumnDefinitions().Append(col1);
                TextBlock tbL = ParseInlineHtml(cells[0]);
                tbL.HorizontalAlignment(HorizontalAlignment::Left);
                Grid::SetColumn(tbL, 0);
                grid.Children().Append(tbL);
                TextBlock tbR = ParseInlineHtml(cells[1]);
                tbR.HorizontalAlignment(HorizontalAlignment::Right);
                Grid::SetColumn(tbR, 1);
                grid.Children().Append(tbR);
                parentPanel.Children().Append(grid);
            } else if (cells.size() == 1) {
                // Single-cell: recurse (handles nested tables + br-text inside the cell)
                ParseTooltipHtml(cells[0], parentPanel);
            } else {
                // No cells found — emit raw row text
                emitBrBlock(rowInner);
            }

            pos = trStart + (size_t)(*it)[0].length();
        }

        // Emit bare text AFTER the last <tr>
        if (pos < html.size())
            emitBrBlock(html.substr(pos));
    }


    void MainWindow::AttachItemTooltip(Microsoft::UI::Xaml::FrameworkElement const& element, std::wstring const& itemName, std::wstring const& itemQuality, std::wstring const& itemRel)
    {
        winrt::Windows::UI::Color qualityColor = GetWoWColor(itemQuality);

        // Build the tooltip content panel up-front
        StackPanel panel;
        panel.Spacing(3);
        panel.MaxWidth(320);

        // Show the item name immediately
        TextBlock nameText;
        nameText.Text(itemName.empty() ? L"Loading..." : winrt::hstring(itemName));
        nameText.Foreground(SolidColorBrush(qualityColor));
        nameText.FontSize(13);
        nameText.FontWeight(winrt::Microsoft::UI::Text::FontWeights::Bold());
        nameText.TextWrapping(TextWrapping::Wrap);
        panel.Children().Append(nameText);

        // Wrap in a styled border
        Border tooltipBorder;
        tooltipBorder.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(240, 8, 8, 12)));
        tooltipBorder.BorderBrush(SolidColorBrush(qualityColor));
        tooltipBorder.BorderThickness(Microsoft::UI::Xaml::Thickness{ 1,1,1,1 });
        tooltipBorder.CornerRadius(Microsoft::UI::Xaml::CornerRadius{ 4,4,4,4 });
        tooltipBorder.Padding(Microsoft::UI::Xaml::Thickness{ 12, 10, 12, 10 });
        tooltipBorder.Child(panel);

        // Attach via ToolTipService — uses WinUI's built-in hover mechanism
        ToolTip tip;
        tip.Content(tooltipBorder);
        tip.Placement(Microsoft::UI::Xaml::Controls::Primitives::PlacementMode::Top);
        tip.Background(SolidColorBrush(Microsoft::UI::Colors::Transparent()));
        tip.BorderThickness(Microsoft::UI::Xaml::Thickness{ 0,0,0,0 });
        tip.Padding(Microsoft::UI::Xaml::Thickness{ 0,0,0,0 });
        ToolTipService::SetToolTip(element, tip);

        // On first hover, trigger async fetch to fill rich content
        if (!itemRel.empty()) {
            auto self = get_strong();
            element.PointerEntered([this, self, panel, itemRel, qualityColor](auto const&, auto const&) mutable {
                FetchAndUpdateTooltip(panel, itemRel);
            });
        }
    }

    winrt::fire_and_forget MainWindow::FetchAndUpdateTooltip(StackPanel panel, std::wstring itemRel)
    {
        auto lifetime = get_strong();

        // Already have cached html?
        auto it = m_tooltipCache.find(itemRel);
        if (it != m_tooltipCache.end() && !it->second.empty()) {
            // Repopulate panel with rich content
            panel.Children().Clear();
            ParseTooltipHtml(it->second, panel);
            co_return;
        }

        // Fetch from cavernoftime
        try {
            winrt::Windows::Web::Http::HttpClient client;
            client.DefaultRequestHeaders().UserAgent().TryParseAdd(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

            std::wstring url = L"http://wotlk.cavernoftime.com/" + itemRel + L"&power=true";
            auto uri = winrt::Windows::Foundation::Uri(url);
            auto response = co_await client.GetStringAsync(uri);

            std::wstring jsResponse = response.c_str();
            std::wstring html = ExtractTooltipHtml(jsResponse);

            if (!html.empty()) {
                m_tooltipCache[itemRel] = html;

                // Update panel on UI thread (co_await already resumes on UI thread for fire_and_forget)
                panel.Children().Clear();
                ParseTooltipHtml(html, panel);
            }
        } catch (...) {}
    }

    void MainWindow::UpdateArmoryUI(winrt::hstring const& wjsonStr)
    {
        std::wstring wjson = wjsonStr.c_str();
        try {
            winrt::Windows::Data::Json::JsonObject root = winrt::Windows::Data::Json::JsonObject::Parse(wjson);
            DispatcherQueue().TryEnqueue([this, root, wjson]() {
                try {
                    CharNameBlock().Text(root.GetNamedString(L"name", L""));
                    CharTitleBlock().Text(root.GetNamedString(L"title", L""));
                    CharPointsBlock().Text(root.GetNamedString(L"points", L""));
                    CharSpecBlock().Text(root.GetNamedString(L"specialization", L"None"));

                    LeftGearPanel().Children().Clear();
                    RightGearPanel().Children().Clear();
                    BottomGearPanel().Children().Clear();

                    auto fillPanel = [&](StackPanel panel, winrt::Windows::Data::Json::JsonArray arr) {
                        for (uint32_t i = 0; i < arr.Size(); i++) {
                            auto itemObj = arr.GetObjectAt(i);
                            std::wstring src = itemObj.GetNamedString(L"img", L"").c_str();
                            std::wstring itemName = itemObj.GetNamedString(L"name", L"").c_str();
                            std::wstring itemQuality = itemObj.GetNamedString(L"quality", L"").c_str();
                            std::wstring itemRel = itemObj.GetNamedString(L"rel", L"").c_str();

                            Border slot;
                            slot.Width(40); slot.Height(40);
                            slot.CornerRadius({ 4,4,4,4 });
                            slot.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 255, 255, 255)));
                            slot.BorderBrush(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(60, 200, 153, 59)));
                            slot.BorderThickness({ 1,1,1,1 });

                            if (!src.empty()) {
                                Microsoft::UI::Xaml::Shapes::Rectangle rect;
                                Microsoft::UI::Xaml::Media::ImageBrush brush;
                                brush.Stretch(Stretch::UniformToFill);
                                brush.ImageSource(Microsoft::UI::Xaml::Media::Imaging::BitmapImage(winrt::Windows::Foundation::Uri(src)));
                                rect.Fill(brush);
                                slot.Child(rect);
                            }

                            // Attach tooltip with item name, quality, and rel
                            AttachItemTooltip(slot, itemName, itemQuality, itemRel);

                            panel.Children().Append(slot);
                        }
                        };

                    fillPanel(LeftGearPanel(), root.GetNamedArray(L"leftItems", winrt::Windows::Data::Json::JsonArray{}));
                    fillPanel(RightGearPanel(), root.GetNamedArray(L"rightItems", winrt::Windows::Data::Json::JsonArray{}));
                    fillPanel(BottomGearPanel(), root.GetNamedArray(L"bottomItems", winrt::Windows::Data::Json::JsonArray{}));

                    // Pre-fetch all item tooltips in background so hovering is instant
                    for (auto const& key : { L"leftItems", L"rightItems", L"bottomItems" }) {
                        auto arr = root.GetNamedArray(key, winrt::Windows::Data::Json::JsonArray{});
                        for (uint32_t i = 0; i < arr.Size(); i++) {
                            auto itemObj = arr.GetObjectAt(i);
                            std::wstring rel = itemObj.GetNamedString(L"rel", L"").c_str();
                            if (!rel.empty() && m_tooltipCache.find(rel) == m_tooltipCache.end()) {
                                StackPanel dummy;
                                FetchAndUpdateTooltip(dummy, rel);
                            }
                        }
                    }

                    auto statsPairs = root.GetNamedArray(L"statsPairs");
                    StatsCol0().Children().Clear();
                    StatsCol1().Children().Clear();
                    StatsCol2().Children().Clear();
                    StatsCol3().Children().Clear();

                    for (uint32_t i = 0; i < statsPairs.Size(); i++) {
                        auto pair = statsPairs.GetObjectAt(i);
                        std::wstring key = pair.GetNamedString(L"key").c_str();
                        std::wstring val = pair.GetNamedString(L"value").c_str();

                        StackPanel sp;
                        sp.Orientation(Orientation::Horizontal);
                        TextBlock tbKey; tbKey.Text(key + L": "); tbKey.Foreground(SolidColorBrush(Microsoft::UI::Colors::Gray())); tbKey.FontSize(12);
                        TextBlock tbVal; tbVal.Text(val); tbVal.Foreground(SolidColorBrush(Microsoft::UI::Colors::White())); tbVal.FontSize(12); tbVal.FontWeight(Microsoft::UI::Text::FontWeights::Bold());
                        sp.Children().Append(tbKey);
                        sp.Children().Append(tbVal);

                        if (i % 4 == 0) StatsCol0().Children().Append(sp);
                        else if (i % 4 == 1) StatsCol1().Children().Append(sp);
                        else if (i % 4 == 2) StatsCol2().Children().Append(sp);
                        else StatsCol3().Children().Append(sp);
                    }
                }
                catch (...) {
                    // Update failed, possibly missing array elements
                }
                });
        }
        catch (...) {
            // Write to error file
            char* appdata = nullptr;
            size_t len = 0;
            _dupenv_s(&appdata, &len, "APPDATA");
            if (appdata) {
                std::filesystem::path errPath = std::filesystem::path(appdata) / L"Wallmane" / L"error_ui.txt";
                free(appdata);
                FILE* f;
                if (_wfopen_s(&f, errPath.c_str(), L"w, ccs=UTF-8") == 0) {
                    fwprintf(f, L"JSON Parse Error in UpdateArmoryUI:\n%s\n", wjson.c_str());
                    fclose(f);
                }
            }
        }
    }

    void MainWindow::ArmoryWebView_NavigationCompleted(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& sender, winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const& args)
    {
        if (args.IsSuccess())
        {
            // Scrape the DOM
            hstring jsScraper = LR"(
                (function() {
                    var data = {
                        name: '',
                        title: '',
                        points: '0',
                        leftItems: [],
                        rightItems: [],
                        bottomItems: [],
                        statsPairs: [],
                        specialization: ''
                    };
                    try {
                        var nameNode = document.querySelector('.information-left .name');
                        if (nameNode && nameNode.childNodes.length > 0 && nameNode.childNodes[0].nodeValue) {
                            data.name = nameNode.childNodes[0].nodeValue.trim();
                        } else if (nameNode) {
                            data.name = nameNode.innerText.trim();
                        }
                        var titleNode = document.querySelector('.information-left .level-race-class');
                        if (titleNode) data.title = titleNode.innerText.trim();
                        var pointsNode = document.querySelector('.information-right .achievement-points');
                        if (pointsNode) data.points = pointsNode.innerText.trim();
                    } catch(e) {}

                    function getSlotItems(container) {
                        var arr = [];
                        if (!container) return arr;
                        var slots = container.querySelectorAll('.item-slot');
                        for (var i = 0; i < slots.length; i++) {
                            var img = slots[i].querySelector('img');
                            var link = slots[i].querySelector('a');
                            var itemData = { img: '', name: '', quality: '', rel: '' };

                            if (img) itemData.img = img.src;

                            // Try to get item name from link title or data attribute
                            if (link) {
                                itemData.name = link.getAttribute('data-item-name') || 
                                                link.getAttribute('title') || 
                                                link.textContent.trim();
                                itemData.quality = link.getAttribute('data-quality') || '';
                                
                                var rel = link.getAttribute('rel') || '';
                                if (!rel) {
                                    var href = link.getAttribute('href') || '';
                                    var idMatch = href.match(/item=(\d+)/) || href.match(/\/item\/(\d+)/);
                                    if (idMatch) {
                                        rel = 'item=' + idMatch[1];
                                    }
                                }
                                itemData.rel = rel;
                            }

                            // Fallback: try to extract from img alt or title
                            if (!itemData.name && img) {
                                itemData.name = img.getAttribute('alt') || img.getAttribute('title') || '';
                            }

                            // Try to infer quality from CSS class on the slot or link
                            if (!itemData.quality && link) {
                                var classes = link.className || '';
                                if (classes.indexOf('q0') !== -1) itemData.quality = 'poor';
                                else if (classes.indexOf('q1') !== -1) itemData.quality = 'common';
                                else if (classes.indexOf('q2') !== -1) itemData.quality = 'uncommon';
                                else if (classes.indexOf('q3') !== -1) itemData.quality = 'rare';
                                else if (classes.indexOf('q4') !== -1) itemData.quality = 'epic';
                                else if (classes.indexOf('q5') !== -1) itemData.quality = 'legendary';
                                else if (classes.indexOf('q6') !== -1) itemData.quality = 'artifact';
                            }

                            arr.push(itemData);
                        }
                        return arr;
                    }

                    data.leftItems   = getSlotItems(document.querySelector('.item-left'));
                    data.rightItems  = getSlotItems(document.querySelector('.item-right'));
                    data.bottomItems = getSlotItems(document.querySelector('.item-bottom'));

                    var stubs = document.querySelectorAll('.character-stats .stub');
                    for (var i = 0; i < stubs.length; i++) {
                        var text = stubs[i].innerHTML.replace(/<br\s*[\/?]>/gi, '\n').replace(/<[^>]+>/g, '');
                        var lines = text.split('\n');
                        for (var j = 0; j < lines.length; j++) {
                            var line = lines[j].trim();
                            if (line.indexOf(':') !== -1) {
                                var parts = line.split(':');
                                data.statsPairs.push({ key: parts[0].trim(), value: parts[1].trim() });
                            }
                        }
                    }

                    var spec = document.querySelector('.specialization .text');
                    if (spec) data.specialization = spec.innerText.replace(/\s+/g, ' ').trim();
                    return JSON.stringify(data);
                })();
            )";

            auto asyncOp = sender.ExecuteScriptAsync(jsScraper);
            asyncOp.Completed([this, sender](auto&& op, auto status) {
                if (status == winrt::Windows::Foundation::AsyncStatus::Completed) {
                    winrt::hstring rawJson = op.GetResults();
                    // rawJson is a JSON string literal like "\"{\\\"name\\\":...}\""
                    if (rawJson.size() > 2) {
                        std::wstring wjson = L"";
                        try {
                            winrt::Windows::Data::Json::JsonValue val = winrt::Windows::Data::Json::JsonValue::Parse(rawJson);
                            wjson = val.GetString().c_str();
                        }
                        catch (...) {
                            wjson = rawJson.c_str();
                        }

                        try {
                            UpdateArmoryUI(winrt::hstring(wjson));

                            // Save to cache after successfully verifying we can parse it
                            winrt::Windows::Data::Json::JsonObject root = winrt::Windows::Data::Json::JsonObject::Parse(wjson);
                            std::wstring charName = root.GetNamedString(L"name").c_str();
                            if (!charName.empty()) {
                                char* appdata = nullptr;
                                size_t len = 0;
                                _dupenv_s(&appdata, &len, "APPDATA");
                                if (appdata) {
                                    std::filesystem::path cachePath = std::filesystem::path(appdata) / L"Wallmane" / L"Cache";
                                    free(appdata);
                                    std::filesystem::create_directories(cachePath);

                                    // Extract realm from URL (https://armory.warmane.com/character/Name/Realm/summary)
                                    std::wstring url = sender.Source().ToString().c_str();
                                    size_t realmStart = url.find(L"/character/" + charName + L"/");
                                    if (realmStart != std::wstring::npos) {
                                        realmStart += 11 + charName.length() + 1;
                                        size_t realmEnd = url.find(L"/summary", realmStart);
                                        if (realmEnd != std::wstring::npos) {
                                            std::wstring realmStr = url.substr(realmStart, realmEnd - realmStart);
                                            std::wstring cacheFile = L"cache_" + charName + L"_" + realmStr + L".json";
                                            FILE* f;
                                            if (_wfopen_s(&f, (cachePath / cacheFile).c_str(), L"w, ccs=UTF-8") == 0) {
                                                fwprintf(f, L"%s", wjson.c_str());
                                                fclose(f);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        catch (winrt::hresult_error const& ex) {
                            char* appdata = nullptr;
                            size_t len = 0;
                            _dupenv_s(&appdata, &len, "APPDATA");
                            if (appdata) {
                                std::filesystem::path errPath = std::filesystem::path(appdata) / L"Wallmane" / L"error.txt";
                                free(appdata);
                                FILE* f;
                                if (_wfopen_s(&f, errPath.c_str(), L"w, ccs=UTF-8") == 0) {
                                    fwprintf(f, L"JSON Parse Error: %s\nJSON payload:\n%s\n", ex.message().c_str(), wjson.c_str());
                                    fclose(f);
                                }
                            }
                        }
                    }
                }
                });

            // Inject CSS to perfectly isolate the 3D model
            hstring jsIsolate = LR"(
                var style = document.createElement('style');
                style.type = 'text/css';
                style.innerHTML = `
                    ::-webkit-scrollbar { display: none !important; }
                    .wm-ui-header, .top-header, .footer, #footer, .navbar, .ad-container, .side-ad, .header-container,
                    #page-navigation, #inpage-navigation, .information, .character-stats, .information-right,
                    .item-left, .item-right, .item-bottom, #page-footer, noscript, .navigation-wrapper {
                        display: none !important;
                    }
                    body, html, #page-frame, #page-content-wrapper, #content-inner, .wm-ui-generic-frame, .item-model, #character-profile, #character-sheet, #content-wrapper {
                        background: transparent !important;
                        border: none !important;
                        box-shadow: none !important;
                        overflow: hidden !important;
                        margin: 0 !important;
                        padding: 0 !important;
                    }
                    .model, .model canvas {
                        background: transparent !important;
                        background-image: none !important;
                        position: fixed !important;
                        top: 50% !important;
                        left: 50% !important;
                        transform: translate(-50%, -50%) !important;
                        z-index: 999999 !important;
                    }
                    canvas { background: transparent !important; }
                `;
                document.head.appendChild(style);
            )";
            sender.ExecuteScriptAsync(jsIsolate);
        }
    }

    void MainWindow::ArmoryWebView_CoreWebView2Initialized(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& sender, winrt::Microsoft::UI::Xaml::Controls::CoreWebView2InitializedEventArgs const& args)
    {
        auto coreWebView2 = sender.CoreWebView2();
        if (coreWebView2)
        {
            auto settings = coreWebView2.Settings();
            settings.AreDefaultContextMenusEnabled(false);
            settings.AreDevToolsEnabled(false);
            settings.IsStatusBarEnabled(false);

            // Inject BEFORE any page script runs — intercepts ModelViewer constructor
            // to strip the background image before the 3D model renders it.
            hstring preScript = LR"(
                (function() {
                    // Wait for ModelViewer to be defined, then wrap it
                    var _origDefine = Object.defineProperty;
                    var patchModelViewer = function() {
                        if (typeof window.ModelViewer === 'function') {
                            var _Orig = window.ModelViewer;
                            window.ModelViewer = function(cfg) {
                                if (cfg) cfg.background = null;
                                return new _Orig(cfg);
                            };
                            // Copy static properties
                            for (var k in _Orig) {
                                if (_Orig.hasOwnProperty(k)) window.ModelViewer[k] = _Orig[k];
                            }
                            return true;
                        }
                        return false;
                    };
                    // Poll until ModelViewer is available
                    var attempts = 0;
                    var iv = setInterval(function() {
                        if (patchModelViewer() || ++attempts > 100) clearInterval(iv);
                    }, 20);

                    // Also clear canvas background via WebGL after load
                    window.addEventListener('load', function() {
                        setTimeout(function() {
                            var canvas = document.querySelector('.model canvas');
                            if (canvas) {
                                var gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
                                if (gl) {
                                    gl.clearColor(0.0, 0.0, 0.0, 0.0);
                                }
                            }
                        }, 500);
                    });
                })();
            )";
            coreWebView2.AddScriptToExecuteOnDocumentCreatedAsync(preScript);
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // Actions
    // ─────────────────────────────────────────────────────────────────
    void MainWindow::PlayButton_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring path = WowPathBox().Text().c_str();
        if (path.empty() || !Core::WowDetector::IsValidWowPath(path))
        {
            BrowseWowPath_Click(nullptr, nullptr);
            return;
        }

        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();

        bool launched = Core::WowDetector::LaunchWow(path, [this, lifetime, dispatcher](uint64_t duration) {
            dispatcher.TryEnqueue([this, lifetime]() {
                uint64_t playtimeSeconds = Core::WowDetector::GetTotalPlaytimeSeconds(WowPathBox().Text().c_str());
                PlaytimeLabel().Text(Core::WowDetector::FormatPlaytime(playtimeSeconds));

                // Restore window if it was minimized
                HWND hwnd = 0;
                this->m_inner.as<::IWindowNative>()->get_WindowHandle(&hwnd);
                if (hwnd) {
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }

                // Reset Discord RPC back to launcher
                Core::DiscordRPC::SetPresence("In Launcher", "Browsing");
            });
        });

        if (!launched)
        {
            ContentDialog dlg;
            dlg.Title(box_value(L"Launch Failed"));
            dlg.Content(box_value(L"Could not start Wow.exe. Please verify the path in Settings."));
            dlg.CloseButtonText(L"OK");
            dlg.XamlRoot(this->Content().XamlRoot());
            dlg.ShowAsync();
        }
        else
        {
            // Activate Discord RPC
            Core::DiscordRPC::SetPresence("Playing WotLK", "Icecrown");

            // Minimize to tray logic
            if (MinimizeOnPlayToggle().IsChecked().GetBoolean())
            {
                HWND hwnd = 0;
                this->m_inner.as<::IWindowNative>()->get_WindowHandle(&hwnd);
                if (hwnd) ShowWindow(hwnd, SW_MINIMIZE);
            }
        }
    }

    void MainWindow::BrowseWowPath_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto windowNative{ this->m_inner.as<::IWindowNative>() };
        HWND hwnd{ 0 };
        windowNative->get_WindowHandle(&hwnd);

        std::wstring path = Core::WowDetector::BrowseForWowExe(hwnd);
        if (!path.empty())
        {
            WowPathBox().Text(path);
            try {
                char* appdata = nullptr;
                size_t len = 0;
                _dupenv_s(&appdata, &len, "APPDATA");
                if (appdata) {
                    std::filesystem::path configPath = std::filesystem::path(appdata) / L"Wallmane";
                    free(appdata);
                    std::filesystem::create_directories(configPath);
                    FILE* f;
                    if (_wfopen_s(&f, (configPath / L"wowpath.txt").c_str(), L"w, ccs=UTF-8") == 0) {
                        fwprintf(f, L"%s", path.c_str());
                        fclose(f);
                    }
                }
            }
            catch (...) {}
        }
    }

    void MainWindow::ClearCache_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring path = WowPathBox().Text().c_str();
        if (path.empty()) return;

        std::filesystem::path cacheDir = std::filesystem::path(path).parent_path() / L"Cache";
        std::error_code ec;
        if (std::filesystem::exists(cacheDir))
            std::filesystem::remove_all(cacheDir, ec);

        ContentDialog dlg;
        dlg.Title(box_value(L"Cache Cleared"));
        dlg.Content(box_value(L"Cache folder removed successfully."));
        dlg.CloseButtonText(L"OK");
        dlg.XamlRoot(this->Content().XamlRoot());
        dlg.ShowAsync();
    }

    void MainWindow::CopyMagnet_Click(IInspectable const&, RoutedEventArgs const&)
    {
        auto dp = Windows::ApplicationModel::DataTransfer::DataPackage();
        dp.SetText(L"magnet:?xt=urn:btih:5b65d1928a3025a820b45e6db2451aaaabc5347c&dn=World%20of%20Warcraft%203.3.5a"
            L"&tr=udp%3A%2F%2Ftracker.openbittorrent.com%3A80%2Fannounce"
            L"&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce");
        Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(dp);
    }

    void MainWindow::RefreshRealmlistUI()
    {
        std::wstring path = WowPathBox().Text().c_str();
        if (path.empty()) return;

        std::wstring current = Core::WowDetector::ReadRealmlist(path);
        CurrentRealmlistLabel().Text(L"Active Realmlist: " + current);
        RealmlistBox().Text(current);
    }

    void MainWindow::SaveRealmlist_Click(IInspectable const&, RoutedEventArgs const&)
    {
        std::wstring path = WowPathBox().Text().c_str();
        if (path.empty())
        {
            RealmlistStatusText().Text(L"⚠ Set your WoW path in Settings first.");
            return;
        }

        if (Core::WowDetector::IsWowRunning())
        {
            RealmlistStatusText().Text(L"⚠ Cannot switch while WoW is running.");
            return;
        }

        std::wstring newRealm = RealmlistBox().Text().c_str();
        if (newRealm.empty())
        {
            RealmlistStatusText().Text(L"⚠ Please enter a realmlist address.");
            return;
        }

        bool ok = Core::WowDetector::SetRealmlist(path, newRealm);
        if (ok)
        {
            RealmlistStatusText().Text(L"✓ Realmlist saved. WTF profile swapped.");
            CurrentRealmlistLabel().Text(L"Active Realmlist: " + newRealm);
        }
        else
        {
            RealmlistStatusText().Text(L"✗ Failed to write realmlist.wtf");
        }
    }

    void MainWindow::SetWarmaneRealmlist_Click(IInspectable const&, RoutedEventArgs const&)
    {
        RealmlistBox().Text(L"logon.warmane.com");
        SaveRealmlist_Click(nullptr, nullptr);
    }


}