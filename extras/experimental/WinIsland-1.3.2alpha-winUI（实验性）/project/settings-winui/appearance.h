#pragma once
#include <winrt/Microsoft.UI.Composition.SystemBackdrops.h>

// Settings-only materials. The host/island renderer has no dependency on this file.
namespace appearance {
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Composition::SystemBackdrops;

inline Windows::UI::Color color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return {a, r, g, b};
}

inline Border card() {
    return Markup::XamlReader::Load(LR"(<Border
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        Background="{ThemeResource CardBackgroundFillColorDefaultBrush}"
        BorderBrush="{ThemeResource CardStrokeColorDefaultBrush}"
        BorderThickness="1" CornerRadius="8" Padding="20,16" MinHeight="80" />)")
        .as<Border>();
}

// Preserve the standard Fluent pointer/focus/selection brushes. Only remove
// the opaque content layer that would otherwise cover the desktop material.
inline void configureNavigation(NavigationView const& navigation) {
    auto resources = Markup::XamlReader::Load(LR"(<ResourceDictionary
        xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">
        <SolidColorBrush x:Key="NavigationViewContentBackground" Color="Transparent" />
        <SolidColorBrush x:Key="NavigationViewExpandedPaneBackground" Color="Transparent" />
        <SolidColorBrush x:Key="NavigationViewTopPaneBackground" Color="Transparent" />
        <!-- The settings client owns the indicator so it can continue across
             long jumps without being reset by NavigationView's item layout. -->
        <SolidColorBrush x:Key="NavigationViewSelectionIndicatorForeground" Color="Transparent" />
    </ResourceDictionary>)").as<ResourceDictionary>();
    navigation.Resources().MergedDictionaries().Append(resources);
    navigation.Background(Media::SolidColorBrush(color(0, 0, 0, 0)));
}

struct Material {
    DesktopAcrylicController controller{nullptr};
    SystemBackdropConfiguration configuration{nullptr};

    bool attach(Window const& window) {
        if (!DesktopAcrylicController::IsSupported()) return false;
        try {
            configuration = SystemBackdropConfiguration{};
            configuration.IsInputActive(true);
            controller = DesktopAcrylicController{};
            controller.Kind(DesktopAcrylicKind::Base);
            if (!controller.AddSystemBackdropTarget(
                    window.as<winrt::Microsoft::UI::Composition::ICompositionSupportsSystemBackdrop>())) {
                close();
                return false;
            }
            controller.SetSystemBackdropConfiguration(configuration);
            return true;
        } catch (hresult_error const&) {
            close();
            return false;
        }
    }

    void apply(Window const& window, Grid const& root) {
        bool dark = root.ActualTheme() == ElementTheme::Dark;
        auto base = dark ? color(32, 32, 32) : color(243, 243, 243);
        auto foreground = dark ? color(242, 242, 242) : color(26, 26, 26);
        if (controller) {
            // Set the theme first: it resets the controller's default properties.
            configuration.Theme(dark ? SystemBackdropTheme::Dark : SystemBackdropTheme::Light);
            controller.TintColor(base);
            controller.TintOpacity(0.60f);
            controller.LuminosityOpacity(0.85f);
            controller.FallbackColor(base);
            root.Background(Media::SolidColorBrush(color(0, 0, 0, 0)));
        } else {
            root.Background(Media::SolidColorBrush(base));
        }
        auto title = window.AppWindow().TitleBar();
        auto transparent = color(0, 0, 0, 0);
        title.ButtonBackgroundColor(transparent);
        title.ButtonInactiveBackgroundColor(transparent);
        title.ButtonForegroundColor(foreground);
        title.ButtonInactiveForegroundColor(dark ? color(170, 170, 170) : color(105, 105, 105));
        title.ButtonHoverBackgroundColor(dark ? color(62, 62, 62) : color(225, 225, 225));
        title.ButtonHoverForegroundColor(foreground);
        title.ButtonPressedBackgroundColor(dark ? color(54, 54, 54) : color(215, 215, 215));
        title.ButtonPressedForegroundColor(foreground);
    }

    void active(bool value) { if (configuration) configuration.IsInputActive(value); }
    void close() {
        if (controller) { controller.Close(); controller = nullptr; }
        configuration = nullptr;
    }
};
}
