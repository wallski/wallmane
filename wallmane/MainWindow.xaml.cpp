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

    void MainWindow::AttachItemTooltip(Microsoft::UI::Xaml::FrameworkElement const& element, std::wstring const& itemName, std::wstring const& itemQuality)
    {
        if (itemName.empty()) return;

        // Determine quality color using ColorHelper - use auto to avoid type issues
        auto qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255); // default white

        if (itemQuality == L"poor")       qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 157, 157, 157);  // Gray
        else if (itemQuality == L"common")    qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 255, 255, 255);  // White
        else if (itemQuality == L"uncommon")  qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 30, 255, 0);    // Green
        else if (itemQuality == L"rare")      qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 0, 112, 221);  // Blue
        else if (itemQuality == L"epic")      qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 163, 53, 238);  // Purple
        else if (itemQuality == L"legendary") qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 255, 128, 0);    // Orange
        else if (itemQuality == L"artifact")  qualityColor = Microsoft::UI::ColorHelper::FromArgb(255, 229, 204, 128);  // Gold

        // Create tooltip content (reuse across calls)
        if (!m_itemTooltipFlyout)
        {
            m_itemTooltipFlyout = Microsoft::UI::Xaml::Controls::Flyout();
            m_itemTooltipFlyout.Placement(Microsoft::UI::Xaml::Controls::Primitives::FlyoutPlacementMode::Top);
        }

        auto tooltipBorder = Microsoft::UI::Xaml::Controls::Border();
        tooltipBorder.Background(Microsoft::UI::Xaml::Media::SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(240, 10, 10, 15)));
        tooltipBorder.BorderBrush(Microsoft::UI::Xaml::Media::SolidColorBrush(qualityColor));
        tooltipBorder.BorderThickness(Microsoft::UI::Xaml::Thickness{ 1,1,1,1 });
        tooltipBorder.CornerRadius(Microsoft::UI::Xaml::CornerRadius{ 4,4,4,4 });
        tooltipBorder.Padding(Microsoft::UI::Xaml::Thickness{ 10, 6, 10, 6 });

        auto tooltipText = Microsoft::UI::Xaml::Controls::TextBlock();
        tooltipText.Text(itemName);
        tooltipText.Foreground(Microsoft::UI::Xaml::Media::SolidColorBrush(qualityColor));
        tooltipText.FontSize(12);
        tooltipText.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());

        tooltipBorder.Child(tooltipText);
        m_itemTooltipFlyout.Content(tooltipBorder);

        // Show/hide flyout with delay on pointer enter/exit
        auto self = get_strong();
        element.PointerEntered([this, self, element](auto const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) mutable {
            // Cancel any pending hide
            if (m_tooltipDelayTimer)
                m_tooltipDelayTimer.Stop();

            // Show after a short delay to avoid flicker
            if (!m_tooltipDelayTimer)
            {
                m_tooltipDelayTimer = Microsoft::UI::Xaml::DispatcherTimer();
                m_tooltipDelayTimer.Interval(std::chrono::milliseconds(200));
                m_tooltipDelayTimer.Tick([this, self, element](auto const&, auto const&) mutable {
                    if (m_itemTooltipFlyout)
                        m_itemTooltipFlyout.ShowAt(element);
                    if (m_tooltipDelayTimer)
                        m_tooltipDelayTimer.Stop();
                });
            }
            m_tooltipDelayTimer.Start();
        });

        element.PointerExited([this, self](auto const&, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) mutable {
            // Hide tooltip on exit
            if (m_tooltipDelayTimer)
                m_tooltipDelayTimer.Stop();
            if (m_itemTooltipFlyout)
                m_itemTooltipFlyout.Hide();
        });
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

                            // Attach tooltip with item name and quality
                            AttachItemTooltip(slot, itemName, itemQuality);

                            panel.Children().Append(slot);
                        }
                        };

                    fillPanel(LeftGearPanel(), root.GetNamedArray(L"leftItems", winrt::Windows::Data::Json::JsonArray{}));
                    fillPanel(RightGearPanel(), root.GetNamedArray(L"rightItems", winrt::Windows::Data::Json::JsonArray{}));
                    fillPanel(BottomGearPanel(), root.GetNamedArray(L"bottomItems", winrt::Windows::Data::Json::JsonArray{}));

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
                            var itemData = { img: '', name: '', quality: '' };

                            if (img) itemData.img = img.src;

                            // Try to get item name from link title or data attribute
                            if (link) {
                                itemData.name = link.getAttribute('data-item-name') || 
                                                link.getAttribute('title') || 
                                                link.textContent.trim();
                                itemData.quality = link.getAttribute('data-quality') || '';
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