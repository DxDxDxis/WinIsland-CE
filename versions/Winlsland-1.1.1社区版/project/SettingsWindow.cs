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
        private readonly TextBlock validation;
        private static readonly Brush Muted = new SolidColorBrush(Color.FromRgb(105, 112, 126));

        internal SettingsWindow(IslandSettings settings)
        {
            this.settings = settings;
            Title = "WinIsland 设置"; Width = 552; Height = 410;
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
            var version = new TextBlock { Text = "1.1", Foreground = Muted, VerticalAlignment = VerticalAlignment.Center };
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
            validation = new TextBlock { Text = "", Foreground = new SolidColorBrush(Color.FromRgb(193, 55, 63)), FontSize = 12,
                Margin = new Thickness(2, 6, 0, 0) };
            root.Children.Add(validation); Content = root;
            ResidentSwitch.Checked += Changed; ResidentSwitch.Unchecked += Changed;
            NativeSwitch.Checked += Changed; NativeSwitch.Unchecked += Changed;
            SecondsBox.TextChanged += delegate { ApplyInput(); };
            SecondsBox.LostKeyboardFocus += delegate
            {
                double value;
                if (!IslandSettings.ParseSeconds(SecondsBox.Text, out value))
                    SecondsBox.Text = settings.DwellSeconds.ToString("0.###", CultureInfo.CurrentCulture);
            };
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
            text.Children.Add(new TextBlock { Text = hint, FontSize = 11, Foreground = Muted, Margin = new Thickness(0, 5, 0, 0) });
            grid.Children.Add(text); Grid.SetColumn(control, 1); grid.Children.Add(control);
            return new Border { Child = grid, Background = Brushes.White, CornerRadius = new CornerRadius(10),
                BorderBrush = new SolidColorBrush(Color.FromRgb(230, 233, 239)), BorderThickness = new Thickness(1), Margin = new Thickness(0, 0, 0, 8) };
        }
        private void Changed(object sender, RoutedEventArgs e)
        { settings.Update(ResidentSwitch.IsChecked == true, NativeSwitch.IsChecked == true, settings.DwellSeconds); }
        private void ApplyInput()
        {
            double value; bool valid = IslandSettings.ParseSeconds(SecondsBox.Text, out value);
            validation.Text = valid ? "" : "请输入 0.1–3600 之间的秒数；当前设置继续生效。";
            SecondsBox.BorderBrush = valid ? new SolidColorBrush(Color.FromRgb(211, 216, 225)) : validation.Foreground;
            if (valid) settings.Update(ResidentSwitch.IsChecked == true, NativeSwitch.IsChecked == true, value);
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
