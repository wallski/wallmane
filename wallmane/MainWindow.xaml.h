#pragma once
#include "MainWindow.g.h"
#include <winrt/Microsoft.UI.Composition.h>

namespace winrt::wallmane::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        winrt::fire_and_forget LoadDataAsync();

        void NavView_SelectionChanged(Microsoft::UI::Xaml::Controls::NavigationView const& sender,
            Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args);

        void PlayButton_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void BrowseWowPath_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void ClearCache_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);
        void CopyMagnet_Click(winrt::Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& e);

        void SearchAddonsBtn_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void AddonSearchBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
        winrt::fire_and_forget PerformAddonSearch(std::wstring query);
        void TabDiscover_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void TabInstalled_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void LoadInstalledAddons();

        void LoadCharacters();
        void UpdateArmoryUI(winrt::hstring const& jsonStr);
        void ArmoryWebView_NavigationCompleted(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& sender, winrt::Microsoft::Web::WebView2::Core::CoreWebView2NavigationCompletedEventArgs const& args);
        void ArmoryWebView_CoreWebView2Initialized(winrt::Microsoft::UI::Xaml::Controls::WebView2 const& sender, winrt::Microsoft::UI::Xaml::Controls::CoreWebView2InitializedEventArgs const& args);

    private:
        Microsoft::UI::Xaml::DispatcherTimer m_lightningTimer{ nullptr };
        int m_lightningCountdown = 0;

        void SetupCustomTitleBar();
        void StartAnimations();
        void SetupCompositionRain();
        void OnLightningTick(winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&);
        void TriggerLightningFlash();
    };
}

namespace winrt::wallmane::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
