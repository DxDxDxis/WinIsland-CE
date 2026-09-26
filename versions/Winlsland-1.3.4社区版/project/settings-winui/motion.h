#pragma once
// Settings process only.  The first implementation walked every XAML visual
// state and mutated template Storyboards at runtime.  That is unsafe with
// WinUI's shared control templates: a toggle can invalidate a storyboard while
// LayoutUpdated is traversing the same tree, which can terminate the settings
// process.  Keep the policy as a small, side-effect-free state object instead.
// Application-owned transitions consult `reduced` before starting. Built-in
// template transitions remain managed by WinUI; do not mutate shared timelines.
struct SettingsMotion {
 bool enabled=false;
 void scan(winrt::Microsoft::UI::Xaml::DependencyObject const&) noexcept {}
 void prune() noexcept {}
 void set(bool value) noexcept { enabled=value; }
};
