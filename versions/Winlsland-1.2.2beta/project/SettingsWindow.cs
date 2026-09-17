using System;
using System.Globalization;
using System.IO;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace WinIsland
{
    internal sealed class SettingsWindow : Window
    {
        private readonly IslandSettings settings;
        internal readonly CheckBox ResidentSwitch, NativeSwitch;
        internal readonly TextBox SecondsBox;
        internal readonly TextBox FrameRateBox;
        private readonly TextBlock validation;
        private readonly TextBlock frameValidation;
        private readonly TextBlock musicHint;
        private readonly Button importLyrics, clearLyrics;
        internal Action ImportLyrics, ClearLyrics;
        private static readonly Brush Muted = new SolidColorBrush(Color.FromRgb(105, 112, 126));

        internal SettingsWindow(IslandSettings settings)
        {
            this.settings = settings;
            Title = "WinIsland 设置"; Width = Math.Min(552, SystemParameters.WorkArea.Width); Height = Math.Min(600, SystemParameters.WorkArea.Height);
            ResizeMode = ResizeMode.NoResize; WindowStartupLocation = WindowStartupLocation.CenterScreen;
            Background = new SolidColorBrush(Color.FromRgb(246, 247, 250));
            FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"); FontSize = 14;
            UseLayoutRounding = true;
            using (var stream = System.Reflection.Assembly.GetExecutingAssembly().GetManifestResourceStream("WinIsland.ico"))
                Icon = BitmapFrame.Create(stream, BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
            var root = new StackPanel { Margin = new Thickness(26, 22, 26, 18) };
            var heading = new Grid(); heading.ColumnDefinitions.Add(new ColumnDefinition());
            heading.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            heading.Children.Add(new TextBlock { Text = "WinIsland", FontSize = 26, FontWeight = FontWeights.SemiBold });
            var version = new TextBlock { Text = AppVersion.Display, Foreground = Muted, VerticalAlignment = VerticalAlignment.Center };
            Grid.SetColumn(version, 1); heading.Children.Add(version); root.Children.Add(heading);
            root.Children.Add(new TextBlock { Text = "设置会自动保存并立即生效", Foreground = Muted, FontSize = 12,
                Margin = new Thickness(0, 6, 0, 20) });
            ResidentSwitch = Toggle("灵动岛常驻", settings.Resident);
            NativeSwitch = Toggle("隐藏Windows自带通知", settings.HideNative);
            root.Children.Add(Row("灵动岛常驻", "关闭后，仅在收到通知时滑入屏幕", ResidentSwitch));
            root.Children.Add(Row("隐藏Windows自带通知", "运行期间隐藏系统横幅，保留通知中心记录", NativeSwitch));
            var secondsPanel = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            SecondsBox = new TextBox { Text = settings.DwellSeconds.ToString("0.###", CultureInfo.CurrentCulture), Width = 62,
                Height = 32, VerticalContentAlignment = VerticalAlignment.Center, HorizontalContentAlignment = HorizontalAlignment.Center,
                BorderBrush = new SolidColorBrush(Color.FromRgb(211, 216, 225)), BorderThickness = new Thickness(1),
                Background = Brushes.White, MaxLength = 12, Padding = new Thickness(3) };
            AutomationProperties.SetName(SecondsBox, "通知停驻时间"); AutomationProperties.SetAutomationId(SecondsBox, "DwellSeconds");
            secondsPanel.Children.Add(SecondsBox);
            secondsPanel.Children.Add(new TextBlock { Text = "秒", Foreground = Muted, Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center });
            root.Children.Add(Row("通知停驻时间", "展开后保持显示的时间，支持小数", secondsPanel));
            var framePanel = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center };
            FrameRateBox = new TextBox { Text = settings.FrameRate.ToString(CultureInfo.CurrentCulture), Width = 62, Height = 32,
                VerticalContentAlignment = VerticalAlignment.Center, HorizontalContentAlignment = HorizontalAlignment.Center,
                BorderBrush = new SolidColorBrush(Color.FromRgb(211, 216, 225)), BorderThickness = new Thickness(1),
                Background = Brushes.White, MaxLength = 3, Padding = new Thickness(3) };
            AutomationProperties.SetName(FrameRateBox, "动画帧率"); AutomationProperties.SetAutomationId(FrameRateBox, "FrameRate");
            framePanel.Children.Add(FrameRateBox); framePanel.Children.Add(new TextBlock { Text = "FPS", Foreground = Muted, Margin = new Thickness(8, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center });
            root.Children.Add(Row("动画帧率", "0 跟随系统；可填 30–240，低性能默认目标 60 FPS", framePanel));
            FrameRateBox.ToolTip = "实际帧率受屏幕刷新率和电脑性能限制";
            frameValidation = new TextBlock { Foreground = new SolidColorBrush(Color.FromRgb(193, 55, 63)), FontSize = 12,
                TextWrapping = TextWrapping.Wrap, Visibility = Visibility.Collapsed, Margin = new Thickness(2, 0, 0, 6) };
            root.Children.Add(frameValidation);
            validation = new TextBlock { Text = "", Foreground = new SolidColorBrush(Color.FromRgb(193, 55, 63)), FontSize = 12,
                Margin = new Thickness(2, 6, 0, 0) };
            root.Children.Add(validation);
            musicHint = new TextBlock { FontSize = 12, Foreground = Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(2, 10, 0, 8) };
            root.Children.Add(musicHint);
            var lyricActions = new StackPanel { Orientation = Orientation.Horizontal };
            importLyrics = new Button { Content = "载入当前歌曲歌词…", Padding = new Thickness(12, 6, 12, 6), IsEnabled = false };
            clearLyrics = new Button { Content = "清除当前歌词", Padding = new Thickness(12, 6, 12, 6), Margin = new Thickness(10, 0, 0, 0), IsEnabled = false };
            AutomationProperties.SetName(importLyrics, "载入当前歌曲的 LRC 歌词文件"); AutomationProperties.SetName(clearLyrics, "清除当前歌曲歌词");
            importLyrics.Click += delegate { if (ImportLyrics != null) ImportLyrics(); };
            clearLyrics.Click += delegate { if (ClearLyrics != null) ClearLyrics(); };
            lyricActions.Children.Add(importLyrics); lyricActions.Children.Add(clearLyrics); root.Children.Add(lyricActions);
            UpdateMusic(null); Content = new ScrollViewer { Content = root, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
            ResidentSwitch.Checked += Changed; ResidentSwitch.Unchecked += Changed;
            NativeSwitch.Checked += Changed; NativeSwitch.Unchecked += Changed;
            SecondsBox.TextChanged += delegate { ApplyInput(); };
            FrameRateBox.TextChanged += delegate { ApplyFrameRate(); };
            FrameRateBox.LostKeyboardFocus += delegate {
                int value;
                if (!IslandSettings.ParseFrameRate(FrameRateBox.Text, out value)) FrameRateBox.Text = settings.FrameRate.ToString(CultureInfo.CurrentCulture);
            };
            SecondsBox.LostKeyboardFocus += delegate
            {
                double value;
                if (!IslandSettings.ParseSeconds(SecondsBox.Text, out value))
                    SecondsBox.Text = settings.DwellSeconds.ToString("0.###", CultureInfo.CurrentCulture);
            };
        }
        internal void UpdateMusic(MusicState music)
        {
            bool ready = music != null && !music.Loading;
            importLyrics.IsEnabled = ready && music.HasTimeline;
            clearLyrics.IsEnabled = ready;
            musicHint.Text = !ready ? "播放音乐并开启播放器的系统媒体控件后，灵动岛会显示歌曲。点击音乐区域可展开控制。" :
                !music.HasTimeline ? music.Platform + "当前未提供播放进度，封面与状态可显示，暂不显示同步歌词。" :
                "可为当前歌曲载入 LRC 歌词，按播放器进度同步。没有歌词时自动隐藏，切歌后不会沿用上一首。";
        }
        private static CheckBox Toggle(string label, bool value)
        {
            var control = new CheckBox { IsChecked = value, Width = 46, Height = 26, Cursor = System.Windows.Input.Cursors.Hand,
                VerticalAlignment = VerticalAlignment.Center, HorizontalAlignment = HorizontalAlignment.Right };
            AutomationProperties.SetName(control, label);
            AutomationProperties.SetAutomationId(control, label == "灵动岛常驻" ? "Resident" : "HideNative");
            control.Template = (ControlTemplate)XamlReader.Parse(
                "<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='CheckBox'>" +
                "<Border x:Name='Track' Background='#CDD2DD' CornerRadius='13' BorderBrush='Transparent' BorderThickness='2'>" +
                "<Ellipse x:Name='Thumb' Fill='White' Width='18' Height='18' HorizontalAlignment='Left' Margin='2,0,0,0'/></Border>" +
                "<ControlTemplate.Triggers><Trigger Property='IsChecked' Value='True'><Setter TargetName='Track' Property='Background' Value='#3D68F2'/>" +
                "<Setter TargetName='Thumb' Property='HorizontalAlignment' Value='Right'/><Setter TargetName='Thumb' Property='Margin' Value='0,0,2,0'/></Trigger>" +
                "<Trigger Property='IsKeyboardFocused' Value='True'><Setter TargetName='Track' Property='BorderBrush' Value='#203A93'/></Trigger>" +
                "<Trigger Property='IsEnabled' Value='False'><Setter Property='Opacity' Value='0.5'/></Trigger></ControlTemplate.Triggers></ControlTemplate>");
            return control;
        }
        private static Border Row(string label, string hint, UIElement control)
        {
            var grid = new Grid { Margin = new Thickness(16, 12, 16, 12), MinHeight = 46 };
            grid.ColumnDefinitions.Add(new ColumnDefinition());
            grid.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var text = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(0, 0, 14, 0) };
            text.Children.Add(new TextBlock { Text = label, FontSize = 14, FontWeight = FontWeights.Medium });
            text.Children.Add(new TextBlock { Text = hint, FontSize = 11, Foreground = Muted, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 5, 0, 0) });
            grid.Children.Add(text); Grid.SetColumn(control, 1); grid.Children.Add(control);
            return new Border { Child = grid, Background = Brushes.White, CornerRadius = new CornerRadius(10),
                BorderBrush = new SolidColorBrush(Color.FromRgb(230, 233, 239)), BorderThickness = new Thickness(1), Margin = new Thickness(0, 0, 0, 8) };
        }
        private void Changed(object sender, RoutedEventArgs e)
        { settings.Update(ResidentSwitch.IsChecked == true, NativeSwitch.IsChecked == true, settings.DwellSeconds, settings.FrameRate); }
        private void ApplyInput()
        {
            double value; bool valid = IslandSettings.ParseSeconds(SecondsBox.Text, out value);
            validation.Text = valid ? "" : "请输入 0.1–3600 之间的秒数；当前设置继续生效。";
            SecondsBox.BorderBrush = valid ? new SolidColorBrush(Color.FromRgb(211, 216, 225)) : validation.Foreground;
            if (valid) settings.Update(ResidentSwitch.IsChecked == true, NativeSwitch.IsChecked == true, value);
        }
        private void ApplyFrameRate()
        {
            int value; bool valid = IslandSettings.ParseFrameRate(FrameRateBox.Text, out value);
            frameValidation.Text = valid ? "" : "请输入 0（跟随系统）或 30–240 的整数；当前帧率保持不变。";
            frameValidation.Visibility = valid ? Visibility.Collapsed : Visibility.Visible;
            FrameRateBox.BorderBrush = valid ? new SolidColorBrush(Color.FromRgb(211, 216, 225)) : frameValidation.Foreground;
            if (valid) settings.Update(ResidentSwitch.IsChecked == true, NativeSwitch.IsChecked == true, settings.DwellSeconds, value);
        }
        internal void SavePreview(string path)
        {
            UpdateLayout(); var view = (FrameworkElement)Content;
            var bmp = new RenderTargetBitmap((int)ActualWidth, (int)ActualHeight, 96, 96, PixelFormats.Pbgra32);
            bmp.Render(this); var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bmp));
            using (var file = File.Create(path)) png.Save(file);
        }
    }
}
