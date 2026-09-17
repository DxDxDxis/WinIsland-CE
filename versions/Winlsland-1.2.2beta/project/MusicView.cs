using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace WinIsland
{
    internal sealed class MusicView : Border
    {
        private readonly TextBlock title, artist, state, lyric, timing;
        private readonly Image cover;
        private readonly FrameworkElement placeholder;
        private readonly StackPanel details, layout, activity;
        private readonly Button toggle, previous, next, header, mode;
        private readonly Rectangle progress;
        private readonly DispatcherTimer clock;
        private readonly ScaleTransform[] bars = new ScaleTransform[6];
        private readonly RectangleGeometry silhouette = new RectangleGeometry();
        private bool rendering, leaving;
        private readonly FramePacer framePacer = new FramePacer();
        private long perfFrame;
        internal long RenderFrames { get { return framePacer.Frames; } }
        private double preferredWidth = BaseWidth, availableWidth = 616;
        private DateTime feedbackUntil;
        private string currentLine = "";
        private MusicState music;
        private Lyrics lyrics;
        private bool expanded, commandPending;
        internal Action LayoutChanged;
        internal Func<string, string, System.Threading.Tasks.Task<bool>> Command;
        internal Func<AudioFrame> Audio;
        internal bool Expanded { get { return expanded; } }
        internal string VisibleLyric { get { return currentLine; } }
        internal string DisplayedTitle { get { return title.Text; } }
        internal bool HasCover { get { return cover.Source != null; } }
        internal const double BaseWidth = 460.46, CompactHeight = 48.384, ExpandedHeight = 121.8, ContentScale = 1.05;
        internal double PanelHeight { get { return expanded ? (music != null && music.HasTimeline ? ExpandedHeight : 105) : CompactHeight; } }
        internal double PreferredWidth { get { return preferredWidth; } }
        internal int VisibleControls { get { return (previous.Visibility == Visibility.Visible ? 1 : 0) + (toggle.Visibility == Visibility.Visible ? 1 : 0) + (next.Visibility == Visibility.Visible ? 1 : 0); } }
        internal double BarsLevel { get { return bars[0].ScaleY; } }
        internal bool AudioAvailable { get; private set; }
        internal bool ModeVisible { get { return mode.Visibility == Visibility.Visible; } }
        internal MusicView()
        {
            Width = BaseWidth; Height = 0; Background = Brushes.Transparent; Clip = silhouette;
            HorizontalAlignment = HorizontalAlignment.Center; VerticalAlignment = VerticalAlignment.Top;
            layout = new StackPanel { Width = preferredWidth / ContentScale - 28, Margin = new Thickness(14, 7.04, 14, 7.04), HorizontalAlignment = HorizontalAlignment.Center,
                LayoutTransform = new ScaleTransform(ContentScale, ContentScale) };
            var row = new Grid { Height = 32 };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(32) });
            row.ColumnDefinitions.Add(new ColumnDefinition());
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            var artwork = new Grid { Width = 28, Height = 28, Clip = new RectangleGeometry(new Rect(0, 0, 28, 28), 6, 6) };
            artwork.Children.Add(new Border { Background = new SolidColorBrush(Color.FromRgb(37, 39, 43)) });
            placeholder = Icon("M 9,3 L 9,16 C 7,14 2,16 3,19 C 4,22 11,20 11,17 L 11,7 L 18,5 L 18,14 C 16,12 11,14 12,17 C 13,20 20,18 20,15 L 20,0 Z", 18, 19);
            artwork.Children.Add(placeholder);
            cover = new Image { Stretch = Stretch.UniformToFill }; artwork.Children.Add(cover); row.Children.Add(artwork);
            var names = new StackPanel { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(9, 0, 10, 0) };
            title = Label(12.5, Brushes.White); title.FontWeight = FontWeights.SemiBold;
            artist = Label(10.5, new SolidColorBrush(Color.FromRgb(190, 192, 199))); artist.Margin = new Thickness(0, 1, 0, 0);
            names.Children.Add(title); names.Children.Add(artist); Grid.SetColumn(names, 1); row.Children.Add(names);
            state = Label(10, new SolidColorBrush(Color.FromRgb(197, 208, 227)));
            activity = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(3, 0, 3, 0), IsHitTestVisible = false };
            for (int i = 0; i < bars.Length; i++)
            {
                bars[i] = new ScaleTransform(1, .08);
                activity.Children.Add(new Rectangle { Width = 2, Height = 14, RadiusX = 1, RadiusY = 1,
                    Fill = new SolidColorBrush(Color.FromRgb(195, 214, 248)), Margin = new Thickness(1.5, 0, 1.5, 0),
                    RenderTransform = bars[i], RenderTransformOrigin = new Point(.5, 1) });
            }
            Grid.SetColumn(activity, 2); row.Children.Add(activity);
            header = ButtonFor(row, "展开音乐", double.NaN, 32); header.HorizontalContentAlignment = HorizontalAlignment.Stretch;
            AutomationProperties.SetAutomationId(header, "MusicHeader");
            header.Click += delegate { expanded = !expanded; ApplyLayout(); if (LayoutChanged != null) LayoutChanged(); };
            layout.Children.Add(header);
            details = new StackPanel { Visibility = Visibility.Collapsed, Margin = new Thickness(4, 3, 4, 0) };
            lyric = Label(12, new SolidColorBrush(Color.FromRgb(229, 233, 244))); lyric.TextAlignment = TextAlignment.Center; lyric.Height = 16;
            AutomationProperties.SetAutomationId(lyric, "CurrentLyric");
            details.Children.Add(lyric);
            var buttons = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 0, 0, 1) };
            previous = ButtonFor(Icon("M 3,2 L 5,2 L 5,20 L 3,20 Z M 18,2 L 6,11 L 18,20 Z", 12, 14), "上一首", 40, 30);
            toggle = ButtonFor(Icon("M 5,2 L 20,11 L 5,20 Z", 14, 14), "播放", 44, 30);
            next = ButtonFor(Icon("M 17,2 L 19,2 L 19,20 L 17,20 Z M 4,2 L 16,11 L 4,20 Z", 12, 14), "下一首", 40, 30);
            mode = ButtonFor("", "播放模式", 36, 30); mode.Margin = new Thickness(12, 0, 0, 0);
            AutomationProperties.SetAutomationId(mode, "MusicMode"); mode.Click += async delegate { await Send("mode"); };
            AutomationProperties.SetAutomationId(previous, "MusicPrevious"); AutomationProperties.SetAutomationId(toggle, "MusicToggle"); AutomationProperties.SetAutomationId(next, "MusicNext");
            previous.Click += async delegate { await Send("previous"); };
            toggle.Click += async delegate { await Send("toggle"); };
            next.Click += async delegate { await Send("next"); };
            // SMTC has no favourite/account state or write API. Do not add a simulated heart.
            buttons.Children.Add(previous); buttons.Children.Add(toggle); buttons.Children.Add(next); buttons.Children.Add(mode); details.Children.Add(buttons);
            var track = new Grid { Height = 2, Margin = new Thickness(1, 3, 1, 0) };
            track.Children.Add(new Border { Background = new SolidColorBrush(Color.FromRgb(52, 55, 62)) });
            progress = new Rectangle { Height = 2, Width = 0, Fill = new SolidColorBrush(Color.FromRgb(192, 210, 245)), HorizontalAlignment = HorizontalAlignment.Left };
            track.Children.Add(progress); details.Children.Add(track);
            timing = Label(10, new SolidColorBrush(Color.FromRgb(160, 164, 174))); timing.TextAlignment = TextAlignment.Right;
            timing.Margin = new Thickness(0, 1, 0, 0); details.Children.Add(timing);
            layout.Children.Add(details); Child = layout;
            clock = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(150) };
            clock.Tick += delegate { UpdateClock(); };
            Loaded += delegate { UpdateSilhouette(); if (music != null) StartRendering(); };
            Unloaded += delegate { clock.Stop(); StopRendering(); };
        }
        private static TextBlock Label(double size, Brush color)
        { return new TextBlock { FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"), FontSize = size, Foreground = color, TextTrimming = TextTrimming.CharacterEllipsis, TextWrapping = TextWrapping.NoWrap }; }
        private static FrameworkElement Icon(string data, double width, double height)
        { return new Path { Data = Geometry.Parse(data), Fill = Brushes.White, Stretch = Stretch.Uniform, Width = width, Height = height, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false }; }
        private static Button ButtonFor(object content, string name, double width, double height)
        {
            var button = new Button { Content = content, Width = width, Height = height, Background = Brushes.Transparent,
                Foreground = Brushes.White, BorderThickness = new Thickness(1), BorderBrush = Brushes.Transparent,
                Padding = new Thickness(0), Cursor = Cursors.Hand };
            var border = new FrameworkElementFactory(typeof(Border)); border.Name = "Frame";
            border.SetValue(Border.CornerRadiusProperty, new CornerRadius(10));
            border.SetValue(Border.BorderThicknessProperty, new Thickness(1));
            // A non-null background gives the entire template a mouse hit area, including
            // icon-only buttons whose Path deliberately doesn't handle input.
            border.SetValue(Border.BackgroundProperty, Brushes.Transparent);
            var presenter = new FrameworkElementFactory(typeof(ContentPresenter));
            presenter.SetValue(ContentPresenter.HorizontalAlignmentProperty, HorizontalAlignment.Stretch);
            presenter.SetValue(ContentPresenter.VerticalAlignmentProperty, VerticalAlignment.Center);
            border.AppendChild(presenter);
            var template = new ControlTemplate(typeof(Button)) { VisualTree = border };
            var hover = new Trigger { Property = UIElement.IsMouseOverProperty, Value = true };
            hover.Setters.Add(new Setter(Border.BackgroundProperty, new SolidColorBrush(Color.FromRgb(32, 34, 39)), "Frame")); template.Triggers.Add(hover);
            var outline = new FrameworkElementFactory(typeof(Border));
            outline.SetValue(Border.BorderBrushProperty, Brushes.White); outline.SetValue(Border.BorderThicknessProperty, new Thickness(1));
            outline.SetValue(Border.CornerRadiusProperty, new CornerRadius(8)); outline.SetValue(FrameworkElement.MarginProperty, new Thickness(1));
            var focusStyle = new Style(typeof(Control));
            focusStyle.Setters.Add(new Setter(Control.TemplateProperty, new ControlTemplate(typeof(Control)) { VisualTree = outline }));
            button.FocusVisualStyle = focusStyle; // WPF shows this for keyboard navigation, not mouse focus.
            var disabled = new Trigger { Property = UIElement.IsEnabledProperty, Value = false };
            disabled.Setters.Add(new Setter(UIElement.OpacityProperty, 0.3)); template.Triggers.Add(disabled);
            button.Template = template; AutomationProperties.SetName(button, name); button.ToolTip = name;
            return button;
        }
        internal void SetState(MusicState value, Lyrics currentLyrics)
        {
            bool wasPlaying = music != null && music.Playing;
            bool newTrack = music == null || value == null || music.TrackKey != value.TrackKey;
            bool lyricsChanged = !object.ReferenceEquals(lyrics, currentLyrics);
            bool timelineChanged = music == null || value == null || music.HasTimeline != value.HasTimeline;
            music = value; lyrics = currentLyrics;
            if (value == null)
            {
                title.Text = artist.Text = state.Text = lyric.Text = timing.Text = currentLine = ""; cover.Source = null;
                clock.Stop(); expanded = false; UpdateControls(); return;
            }
            title.Text = value.Loading ? "正在切换歌曲" : value.Title;
            artist.Text = string.IsNullOrWhiteSpace(value.Artist) ? value.Platform : value.Artist;
            state.Text = value.Loading ? "" : value.Playing ? "播放中" : "已暂停";
            cover.Source = value.Loading ? null : value.Cover;
            placeholder.Visibility = cover.Source == null ? Visibility.Visible : Visibility.Collapsed;
            AutomationProperties.SetName(header, (expanded ? "收起音乐，" : "展开音乐，") + title.Text + "，" + artist.Text + "，" + state.Text);
            if (newTrack || wasPlaying != value.Playing)
                toggle.Content = Icon(value.Playing ? "M 4,2 L 9,2 L 9,20 L 4,20 Z M 14,2 L 19,2 L 19,20 L 14,20 Z" : "M 5,2 L 20,11 L 5,20 Z", 15, 15);
            AutomationProperties.SetName(toggle, value.Playing ? "暂停" : "播放"); toggle.ToolTip = value.Playing ? "暂停" : "播放";
            UpdateControls(); UpdateClock();
            if (!value.Loading && (newTrack || lyricsChanged || timelineChanged)) UpdatePreferredWidth();
            if (value.Playing) clock.Start(); else clock.Stop();
            if (IsLoaded) StartRendering();
        }
        private void ApplyLayout()
        {
            // Keep collapsing controls in the clipped section until its height has receded.
            if (expanded) details.Visibility = Visibility.Visible;
            UpdatePreferredWidth(); UpdateClock();
            AutomationProperties.SetName(header, expanded ? "收起音乐" : "展开音乐");
            header.ToolTip = expanded ? "收起音乐" : "展开音乐";
        }
        private void UpdateControls()
        {
            previous.Visibility = music != null && music.CanPrevious ? Visibility.Visible : Visibility.Collapsed;
            toggle.Visibility = music != null && music.CanToggle ? Visibility.Visible : Visibility.Collapsed;
            next.Visibility = music != null && music.CanNext ? Visibility.Visible : Visibility.Collapsed;
            mode.Visibility = music != null && music.CanMode ? Visibility.Visible : Visibility.Collapsed;
            if (music != null)
            {
                mode.Content = ModeIcon(music); mode.ToolTip = music.ModeName + "，点击切换";
                AutomationProperties.SetName(mode, music.ModeName + "，切换播放模式");
            }
            previous.IsEnabled = music != null && music.CanPrevious && !commandPending;
            toggle.IsEnabled = music != null && music.CanToggle && !commandPending;
            next.IsEnabled = music != null && music.CanNext && !commandPending;
            mode.IsEnabled = music != null && music.CanMode && !commandPending;
        }
        private async System.Threading.Tasks.Task Send(string action)
        {
            if (music == null || commandPending || Command == null) return;
            commandPending = true; UpdateControls();
            try { if (!await Command(action, music.TrackKey)) { feedbackUntil = DateTime.UtcNow.AddSeconds(2); UpdateClock(); } }
            finally { commandPending = false; UpdateControls(); }
        }
        private void UpdateClock()
        {
            if (music == null || music.Loading) { lyric.Text = timing.Text = currentLine = ""; progress.Width = 0; return; }
            double position = music.PositionNow(DateTimeOffset.UtcNow);
            currentLine = music.HasTimeline && lyrics != null ? lyrics.At(position) : "";
            lyric.Text = DateTime.UtcNow < feedbackUntil ? "播放器未响应" : !string.IsNullOrWhiteSpace(currentLine) ? currentLine : music.HasTimeline ? "暂无歌词" : "歌词进度不可用";
            lyric.Visibility = Visibility.Visible;
            artist.Text = !expanded && !string.IsNullOrWhiteSpace(currentLine) ? currentLine : string.IsNullOrWhiteSpace(music.Artist) ? music.Platform : music.Artist;
            timing.Text = music.HasTimeline ? Time(position) + " / " + Time(music.Duration) : "";
            UpdateProgress(position);
            progress.Parent.SetValue(UIElement.VisibilityProperty, music.HasTimeline ? Visibility.Visible : Visibility.Collapsed);
            timing.Visibility = music.HasTimeline ? Visibility.Visible : Visibility.Collapsed;
        }
        private void UpdateProgress(double position)
        { progress.Width = music != null && music.HasTimeline && music.Duration > 0 ? Math.Max(0, layout.Width - 10) * Math.Min(1, position / music.Duration) : 0; }
        private static string Time(double value) { var t = TimeSpan.FromSeconds(Math.Max(0, value)); return ((int)t.TotalMinutes).ToString("00") + ":" + t.Seconds.ToString("00"); }
        internal void SetExpanded(bool value) { expanded = value; ApplyLayout(); if (LayoutChanged != null) LayoutChanged(); }
        internal void SetAvailableWidth(double width)
        {
            availableWidth = Math.Min(616, width); UpdatePreferredWidth();
        }
        private void UpdatePreferredWidth()
        {
            long started=Performance.Begin();try {
            double need = BaseWidth;
            if (lyrics != null && music != null && music.HasTimeline)
                foreach (string text in lyrics.Texts)
                {
                    var size = new FormattedText(text, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight,
                        new Typeface(title.FontFamily, FontStyles.Normal, FontWeights.Normal, FontStretches.Normal), expanded ? 12 : 10.5, Brushes.White, VisualTreeHelper.GetDpi(this).PixelsPerDip);
                    need = Math.Max(need, (size.WidthIncludingTrailingWhitespace + (expanded ? 40 : 118)) * 1.1);
                }
            // Reserve one width per song/state using its longest lyric. No per-line breathing.
            preferredWidth = Math.Min(availableWidth, need <= BaseWidth ? BaseWidth : Math.Ceiling(need / 26.4) * 26.4);
            } finally { Performance.End(PerfPart.LyricWidth,started); }
        }
        private static FrameworkElement ModeIcon(MusicState value)
        {
            string data = value.Shuffle == true ? "M 1,3 L 5,3 L 15,17 L 20,17 M 16,13 L 20,17 L 16,21 M 1,17 L 5,17 L 15,3 L 20,3 M 16,0 L 20,3 L 16,7" :
                "M 3,6 L 18,6 L 15,3 M 18,6 L 15,9 M 18,15 L 3,15 L 6,12 M 3,15 L 6,18";
            var icon = new Path { Data = Geometry.Parse(data), Stroke = Brushes.White, StrokeThickness = 1.5, Stretch = Stretch.Uniform,
                Width = 15, Height = 15, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, IsHitTestVisible = false };
            if (value.Shuffle == true || value.RepeatMode != 1) return icon;
            var grid = new Grid { Width = 18, Height = 18, IsHitTestVisible = false }; grid.Children.Add(icon);
            grid.Children.Add(new TextBlock { Text = "1", FontSize = 8, Foreground = Brushes.White, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center });
            return grid;
        }
        internal void Enter()
        { leaving = false; Visibility = Visibility.Visible; IsHitTestVisible = true; if (expanded) details.Visibility = Visibility.Visible; }
        internal void BeginExit()
        { leaving = true; IsHitTestVisible = false; clock.Stop(); }
        // The parent owns the only shell and the entire geometry animation. This view
        // only clips its music section, so controls never paint into the message below.
        internal void SetBounds(double width, double height, double alpha)
        {
            Width = width; Height = Math.Max(0, height); layout.Width = Math.Max(0, width / ContentScale - 28); Opacity = alpha;
            if (!expanded && height <= CompactHeight + .01) details.Visibility = Visibility.Collapsed;
            UpdateProgress(music == null ? 0 : music.PositionNow(DateTimeOffset.UtcNow)); UpdateSilhouette();
        }
        internal void Reset()
        {
            leaving = false; StopRendering(); clock.Stop(); Height = 0; Opacity = 0; Visibility = Visibility.Hidden; SetState(null, null);
        }
        private void StartRendering() { if (!rendering) { rendering = true; CompositionTarget.Rendering += Render; } }
        private void StopRendering() { if (rendering) { rendering = false; CompositionTarget.Rendering -= Render; } perfFrame=0; }
        private void UpdateSilhouette()
        { silhouette.Rect = new Rect(0, 0, Math.Max(0, Width), Math.Max(0, Height)); }
        private void Render(object sender, EventArgs e)
        {
            if (!framePacer.ShouldRender(((RenderingEventArgs)e).RenderingTime.Ticks, Compatibility.EffectiveFrameRate)) return;
            Performance.Gap(PerfPart.MusicGap,ref perfFrame);long started=Performance.Begin();try {
            bool playing = music != null && music.Playing && !music.Loading && !leaving;
            AudioFrame frame = Audio == null ? null : Audio();
            AudioAvailable = frame != null && music != null && frame.SourceId == music.SourceId && frame.Available;
            activity.Opacity = AudioAvailable ? 1 : .35;
            header.ToolTip = (expanded ? "收起音乐" : "展开音乐") + (AudioAvailable ? " · 当前播放器音量历史" : " · 真实音频电平暂不可用");
            bool settling = false;
            for (int i = 0; i < bars.Length; i++)
            {
                double peak = playing && AudioAvailable && !frame.Muted ? frame.Peaks[i] : 0;
                double target = .08 + .92 * Math.Max(0, (20 * Math.Log10(Math.Max(.001, peak)) + 60) / 60);
                double smoothing = 1 - Math.Pow(target > bars[i].ScaleY ? .5 : .8, framePacer.DeltaSeconds * 60);
                bars[i].ScaleY += (target - bars[i].ScaleY) * smoothing;
                if (Math.Abs(target - bars[i].ScaleY) > .005) settling = true;
            }
            if (!playing && !settling) StopRendering();
            } finally { Performance.End(PerfPart.MusicFrame,started); }
        }
    }
}
