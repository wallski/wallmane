#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif
#include "src/Core/WowDetector.h"
#include "src/Core/NewsFetcher.h"
#include "src/Core/DiscordRPC.h"
#include "src/Core/AddonManager.h"
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Dispatching.h>
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
        appWindow.Resize({ 1080, 660 });

        try
        {
            auto settings = ApplicationData::Current().LocalSettings().Values();
            if (settings.HasKey(L"WowPath"))
                WowPathBox().Text(unbox_value<hstring>(settings.Lookup(L"WowPath")));
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
            
            uint64_t playtimeSeconds = Core::WowDetector::GetTotalPlaytimeSeconds();
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
        SettingsPage().Visibility(tag == L"settings_page" ? Visibility::Visible : Visibility::Collapsed);

        if (tag == L"addons_page")
        {
            PerformAddonSearch(L"");
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
            Grid::SetColumn(textPanel, 0);

            Button tag;
            tag.Background(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(40, 200, 50, 50)));
            tag.CornerRadius({ 4,4,4,4 });
            tag.Padding({ 10,4,10,4 });
            tag.Content(box_value(L"Remove"));
            tag.Foreground(SolidColorBrush(Microsoft::UI::ColorHelper::FromArgb(255, 255, 100, 100)));
            tag.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(tag, 1);

            tag.Click([this, addon, tag](auto, auto) mutable {
                std::wstring p = WowPathBox().Text().c_str();
                if (p.empty()) return;

                tag.IsEnabled(false);
                tag.Content(box_value(L"Removing..."));

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

            grid.Children().Append(textPanel);
            grid.Children().Append(tag);
            card.Child(grid);
            InstalledPanel().Children().Append(card);
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
        if (!Core::WowDetector::LaunchWow(path))
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
                auto appWindow = this->AppWindow();
                // Minimize by invoking P/Invoke ShowWindow since AppWindow doesn't have a direct minimize yet
                HWND hwnd = 0;
                this->m_inner.as<::IWindowNative>()->get_WindowHandle(&hwnd);
                ShowWindow(hwnd, SW_MINIMIZE);
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
            try { ApplicationData::Current().LocalSettings().Values().Insert(L"WowPath", box_value(hstring(path))); }
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


}
