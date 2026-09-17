using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace WinIsland
{
    internal sealed class IslandWindow : Window
    {
        internal const double IdleWidth = 180.4, IdleHeight = 29.92, HiddenOffset = -45.92;
        private readonly Border island;
        private readonly MusicView musicView;
        private readonly Canvas noticeViewport;
        private readonly Border divider;
        private MusicState music;
        private readonly StackPanel content;
        private readonly TextBlock title, body;
        private readonly RectangleGeometry clip = new RectangleGeometry();
        private readonly NoticeQueue pending = new NoticeQueue();
        private readonly DispatcherTimer dismiss;
        private readonly Stopwatch animation = new Stopwatch(), hold = new Stopwatch();
        private double maxWidth = 616, hostHeight = 432, offset, screenScale = 1;
        private double fromWidth, fromHeight, fromRadius, fromOpacity, fromOffset;
        private double toWidth, toHeight, toRadius, toOpacity, toOffset, duration;
        private double musicHeight, musicOpacity, fromMusicHeight, toMusicHeight, fromMusicOpacity, toMusicOpacity;
        private int screenX, screenY, lastY = int.MinValue;
        private bool animating, suspended, resident = true, awaitingHold, changingNotice;
        private readonly FramePacer framePacer = new FramePacer();
        private long perfFrame;
        private double dwellSeconds = 4;
        private Notice current;
        private IntPtr handle;
        internal Action<Notice> NoticeDisplayed;
        internal Action<bool> SessionPaused, PowerPaused;
        internal Func<string, string, System.Threading.Tasks.Task<bool>> MusicCommand;
        internal Func<AudioFrame> Audio;

        internal IslandWindow()
        {
            Title = "WinIsland"; WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize;
            AllowsTransparency = true; Background = Brushes.Transparent; ShowInTaskbar = false;
            ShowActivated = false; Focusable = false; Topmost = true;
            Width = 656; Height = hostHeight; UseLayoutRounding = true; SnapsToDevicePixels = true;
            var root = new Grid();
            island = new Border { Background = Brushes.Black, Width = IdleWidth, Height = IdleHeight,
                CornerRadius = new CornerRadius(IdleHeight / 2), HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Top, Clip = clip };
            var canvas = new Canvas();
            noticeViewport = new Canvas { ClipToBounds = true, IsHitTestVisible = false };
            divider = new Border { Background = new SolidColorBrush(Color.FromRgb(48, 49, 53)), Height = 1, Opacity = 0 };
            Canvas.SetLeft(divider, 24); noticeViewport.Children.Add(divider);
            content = new StackPanel { Width = maxWidth - 48, Opacity = 0 };
            Canvas.SetLeft(content, 24); Canvas.SetTop(content, 16.8);
            title = new TextBlock { Foreground = Brushes.White, FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"),
                FontSize = 14, FontWeight = FontWeights.Bold, TextWrapping = TextWrapping.Wrap,
                TextTrimming = TextTrimming.CharacterEllipsis, MaxHeight = 76.8, LineHeight = 19.2 };
            body = new TextBlock { Foreground = new SolidColorBrush(Color.FromRgb(223, 223, 228)),
                FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"), FontSize = 12,
                TextWrapping = TextWrapping.Wrap, TextTrimming = TextTrimming.CharacterEllipsis,
                LineHeight = 17.6, Margin = new Thickness(0, 6.4, 0, 0) };
            content.Children.Add(title); content.Children.Add(body); noticeViewport.Children.Add(content); canvas.Children.Add(noticeViewport);
            island.Child = canvas; root.Children.Add(island); Content = new AdornerDecorator { Child = root };
            musicView = new MusicView { Visibility = Visibility.Hidden };
            musicView.Command = (action, key) => MusicCommand == null ? System.Threading.Tasks.Task.FromResult(false) : MusicCommand(action, key);
            musicView.Audio = () => Audio == null ? null : Audio();
            musicView.LayoutChanged = delegate { UpdateLayoutTarget(.42); };
            canvas.Children.Add(musicView);
            dismiss = new DispatcherTimer(); dismiss.Tick += delegate { dismiss.Stop(); FinishNotice(); };
            SourceInitialized += delegate
            {
                handle = new WindowInteropHelper(this).Handle;
                Native.SetWindowLong(handle, Native.GwlExStyle, Native.GetWindowLong(handle, Native.GwlExStyle) |
                    Native.ToolWindow | Native.NoActivate | Native.Transparent);
                HwndSource.FromHwnd(handle).AddHook(WindowMessage);
                Native.WTSRegisterSessionNotification(handle, 0); PositionOnPrimary(); UpdateClip();
            };
            Closed += delegate { Native.WTSUnRegisterSessionNotification(handle); dismiss.Stop(); StopAnimation(); musicView.Reset(); };
        }
        private IntPtr WindowMessage(IntPtr hwnd, int msg, IntPtr wp, IntPtr lp, ref bool handled)
        {
            if (msg == Native.WmMouseActivate) { handled = true; return new IntPtr(music != null ? 1 : 3); }
            if (msg == Native.WmNcHitTest)
            {
                long point = lp.ToInt64();
                Point local = PointFromScreen(new Point((short)(point & 0xffff), (short)((point >> 16) & 0xffff)));
                bool hitMusic = music != null && !suspended && local.X >= (Width - musicView.Width) / 2 &&
                    local.X <= (Width + musicView.Width) / 2 && local.Y >= 0 && local.Y <= musicHeight;
                handled = true; return new IntPtr(hitMusic ? 1 : -1);
            }
            if (msg == Native.WmDpiChanged || msg == 0x007E || msg == 0x001A)
                Dispatcher.BeginInvoke(new Action(PositionOnPrimary));
            if (msg == 0x02B1)
            {
                int reason = wp.ToInt32();
                if ((reason == 2 || reason == 4 || reason == 6 || reason == 7) && SessionPaused != null) SessionPaused(true);
                if ((reason == 1 || reason == 3 || reason == 8) && SessionPaused != null) SessionPaused(false);
            }
            if (msg == 0x0218 && PowerPaused != null)
            {
                int reason = wp.ToInt32();
                if (reason == 4) PowerPaused(true);
                if (reason == 7 || reason == 18) PowerPaused(false);
            }
            return IntPtr.Zero;
        }
        internal void PositionOnPrimary()
        {
            if (handle == IntPtr.Zero) return;
            var monitor = Native.MonitorFromPoint(new Native.Point(), 1);
            var info = new Native.MonitorInfo { Size = Marshal.SizeOf(typeof(Native.MonitorInfo)) };
            if (!Native.GetMonitorInfo(monitor, ref info)) return;
            uint dx = 96, dy = 96;
            try { Native.GetDpiForMonitor(monitor, 0, out dx, out dy); } catch (DllNotFoundException) { }
            screenScale = dx / 96.0;
            double available = (info.Monitor.Right - info.Monitor.Left) / screenScale;
            maxWidth = Math.Max(IdleWidth, Math.Min(616, available - 32)); Width = Math.Min(656, available);
            musicView.SetAvailableWidth(maxWidth);
            hostHeight = Math.Max(96, Math.Min(432, (info.Monitor.Bottom - info.Monitor.Top) / screenScale * 0.65)); Height = hostHeight;
            screenX = info.Monitor.Left + ((info.Monitor.Right - info.Monitor.Left) - (int)Math.Round(Width * screenScale)) / 2;
            screenY = info.Monitor.Top + (int)Math.Round(8 * screenScale);
            lastY = screenY + (int)Math.Round(offset * screenScale);
            Native.SetWindowPos(handle, new IntPtr(-1), screenX, lastY,
                (int)Math.Round(Width * screenScale), (int)Math.Ceiling(Height * screenScale), 0x0010);
            UpdateLayoutTarget(.3);
        }
        internal void ApplySettings(IslandSettings settings)
        {
            Compatibility.SetTargetFrameRate(settings.FrameRate);
            bool move = resident != settings.Resident, retime = dwellSeconds != settings.DwellSeconds;
            resident = settings.Resident; dwellSeconds = settings.DwellSeconds;
            if (move && current == null && music == null)
            {
                if (handle == IntPtr.Zero || suspended) { offset = resident ? 0 : HiddenOffset; MoveVertically(); }
                else UpdateLayoutTarget(.46);
            }
            if (retime && current != null && !awaitingHold) ScheduleDismiss();
        }
        internal void SetMusic(MusicState value, Lyrics lyrics)
        {
            if (suspended) return;
            bool presenceChanged = (music == null) != (value == null);
            music = value;
            if (value != null)
            {
                musicView.SetState(value, lyrics);
                if (presenceChanged) musicView.Enter();
            }
            else if (presenceChanged) musicView.BeginExit();
            UpdateLayoutTarget(presenceChanged ? .56 : .4);
            if (handle != IntPtr.Zero)
            {
                int style = Native.GetWindowLong(handle, Native.GwlExStyle);
                Native.SetWindowLong(handle, Native.GwlExStyle, value == null ? style | Native.Transparent | Native.NoActivate : style & ~Native.Transparent & ~Native.NoActivate);
                Focusable = value != null;
            }
        }
        private void UpdateLayoutTarget(double seconds)
        {
            double section = music == null ? 0 : musicView.PanelHeight;
            double width = music == null ? IdleWidth : musicView.PreferredWidth;
            double height = section > 0 ? section : IdleHeight;
            if (current != null)
            {
                Size size = MeasureNotice(current);
                width = size.Width; height = section + size.Height;
            }
            double radius = current != null ? 28 : music == null ? IdleHeight / 2 : musicView.Expanded ? 23.1 : 21;
            double alpha = current != null && !changingNotice ? 1 : 0;
            double targetOffset = current != null || music != null || resident ? 0 : HiddenOffset;
            // SMTC polls and timeline ticks must not restart an in-flight transition or
            // extend a notification's hold. Retarget only when the layout actually changes.
            if (Math.Abs(toWidth - width) < .01 && Math.Abs(toHeight - height) < .01 &&
                Math.Abs(toMusicHeight - section) < .01 && Math.Abs(toRadius - radius) < .01 &&
                toOpacity == alpha && toOffset == targetOffset && toMusicOpacity == (music == null ? 0 : 1)) return;
            Animate(width, height, radius, alpha, targetOffset, section, music == null ? 0 : 1, seconds);
        }
        internal void Enqueue(Notice notice)
        {
            if (suspended) return;
            if (current == null) { SetNotice(notice); return; }
            if (current.Key == notice.Key)
            {
                if (current.Title == notice.Title && current.Body == notice.Body) return;
                current = notice; UpdateLayoutTarget(.3);
                return;
            }
            pending.Enqueue(notice);
        }
        internal Size MeasureNotice(Notice notice)
        {
            long started=Performance.Begin(); try {
            title.Text = notice.Title ?? ""; body.Text = notice.Body ?? "";
            body.Visibility = string.IsNullOrWhiteSpace(body.Text) ? Visibility.Collapsed : Visibility.Visible;
            title.TextWrapping = body.TextWrapping = TextWrapping.NoWrap;
            title.MaxHeight = body.MaxHeight = double.PositiveInfinity;
            title.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            body.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            // Preserve the previous horizontal footprint while reducing the vertical typography scale.
            double width = Math.Max(IdleWidth, Math.Min(maxWidth, Math.Ceiling(Math.Max(title.DesiredSize.Width * 16 / 14, body.DesiredSize.Width * 14 / 12)) + 48));
            if (music != null) width = Math.Max(width, musicView.PreferredWidth);
            title.TextWrapping = body.TextWrapping = TextWrapping.Wrap; content.Width = width - 48;
            double noticeLimit = Math.Max(80, hostHeight - (music != null ? musicView.PanelHeight : 0));
            title.MaxHeight = Math.Min(76.8, noticeLimit * 0.3); title.Measure(new Size(content.Width, double.PositiveInfinity));
            body.MaxHeight = Math.Max(17.6, noticeLimit - 33.6 - title.DesiredSize.Height - 6.4);
            content.Measure(new Size(content.Width, double.PositiveInfinity));
            return new Size(width, Math.Min(noticeLimit, Math.Max(52.8, content.DesiredSize.Height + 33.6)));
            } finally { Performance.End(PerfPart.NoticeMeasure,started); }
        }
        private void SetNotice(Notice notice)
        {
            current = notice; changingNotice = false;
            dismiss.Stop(); hold.Reset(); awaitingHold = true; UpdateLayoutTarget(.52);
            if (NoticeDisplayed != null) NoticeDisplayed(notice);
        }
        private void ScheduleDismiss()
        {
            dismiss.Stop(); if (current == null || awaitingHold) return;
            double remaining = dwellSeconds - hold.Elapsed.TotalSeconds;
            if (remaining <= 0) FinishNotice();
            else { dismiss.Interval = TimeSpan.FromSeconds(remaining); dismiss.Start(); }
        }
        private void FinishNotice()
        {
            hold.Reset(); awaitingHold = false;
            if (pending.Count > 0)
            {
                if (!IsLoaded) { SetNotice(pending.Dequeue()); return; }
                changingNotice = true; UpdateLayoutTarget(.14); return;
            }
            current = null; UpdateLayoutTarget(.46);
        }
        internal void Suspend(bool value)
        {
            suspended = value; dismiss.Stop(); pending.Clear(); current = null;
            awaitingHold = changingNotice = false; hold.Reset(); StopAnimation();
            island.Width = IdleWidth; island.Height = IdleHeight; island.CornerRadius = new CornerRadius(IdleHeight / 2);
            content.Opacity = 0; title.Text = ""; body.Text = ""; offset = resident ? 0 : HiddenOffset; UpdateClip();
            music = null; musicHeight = musicOpacity = 0; musicView.Reset(); toWidth = 0; UpdateSections();
            if (value) Hide(); else { Show(); PositionOnPrimary(); }
        }
        internal static double Ease(double t)
        {
            if (t <= 0) return 0; if (t >= 1) return 1;
            return (1 - (1 + 9 * t) * Math.Exp(-9 * t)) / (1 - 10 * Math.Exp(-9));
        }
        private void Animate(double width, double height, double radius, double opacity, double targetOffset, double section, double musicAlpha, double seconds)
        {
            fromWidth = island.Width; fromHeight = island.Height; fromRadius = island.CornerRadius.TopLeft;
            fromOpacity = content.Opacity; fromOffset = offset;
            fromMusicHeight = musicHeight; fromMusicOpacity = musicOpacity; toMusicHeight = section; toMusicOpacity = musicAlpha;
            toWidth = width; toHeight = height; toRadius = Math.Min(radius, height / 2); toOpacity = opacity;
            toOffset = targetOffset; duration = SystemParameters.ClientAreaAnimation ? seconds : 0.001; animation.Restart();
            if (!animating) { animating = true; CompositionTarget.Rendering += RenderFrame; }
        }
        private void RenderFrame(object sender, EventArgs args)
        {
            if (!framePacer.ShouldRender(((RenderingEventArgs)args).RenderingTime.Ticks, Compatibility.EffectiveFrameRate)) return;
            Performance.Gap(PerfPart.ShellGap,ref perfFrame); long started=Performance.Begin(); try {
            double t = Math.Min(1, animation.Elapsed.TotalSeconds / duration), p = Ease(t);
            island.Width = Lerp(fromWidth, toWidth, p); island.Height = Lerp(fromHeight, toHeight, p);
            island.CornerRadius = new CornerRadius(Lerp(fromRadius, toRadius, p));
            double textProgress = toOpacity > fromOpacity ? Ease(Math.Max(0, (t - 0.16) / 0.84)) : Ease(Math.Min(1, t * 2));
            content.Opacity = Lerp(fromOpacity, toOpacity, textProgress);
            musicHeight = Lerp(fromMusicHeight, toMusicHeight, p);
            double musicProgress = toMusicOpacity > fromMusicOpacity ? Ease(Math.Max(0, (t - .16) / .84)) : Ease(Math.Min(1, t * 1.6));
            musicOpacity = Lerp(fromMusicOpacity, toMusicOpacity, musicProgress);
            offset = Lerp(fromOffset, toOffset, p); MoveVertically(); UpdateClip();
            UpdateSections();
            if (t >= 1)
            {
                StopAnimation();
                if (music == null) musicView.Reset();
                if (changingNotice) { changingNotice = false; SetNotice(pending.Dequeue()); return; }
                if (current == null) { title.Text = ""; body.Text = ""; }
                else if (awaitingHold) { awaitingHold = false; hold.Restart(); ScheduleDismiss(); }
            }
            } finally { Performance.End(PerfPart.ShellFrame,started); }
        }
        private void UpdateSections()
        {
            musicView.SetBounds(island.Width, musicHeight, musicOpacity);
            Canvas.SetTop(noticeViewport, musicHeight);
            noticeViewport.Width = island.Width; noticeViewport.Height = Math.Max(0, island.Height - musicHeight);
            divider.Width = Math.Max(0, island.Width - 48); divider.Opacity = content.Opacity * musicOpacity;
            Canvas.SetTop(content, 16.8 + 4 * (1 - content.Opacity));
        }
        private void MoveVertically()
        {
            if (handle == IntPtr.Zero) return;
            int y = screenY + (int)Math.Round(offset * screenScale);
            if (lastY == y) return; lastY = y;
            Native.SetWindowPos(handle, IntPtr.Zero, screenX, y, 0, 0, 0x0001 | 0x0004 | 0x0010);
        }
        private static double Lerp(double a, double b, double p) { return a + (b - a) * p; }
        private void UpdateClip()
        { clip.Rect = new Rect(0, 0, island.Width, island.Height); clip.RadiusX = clip.RadiusY = island.CornerRadius.TopLeft; }
        private void StopAnimation()
        { if (animating) { CompositionTarget.Rendering -= RenderFrame; animating = false; } animation.Stop(); perfFrame=0; }
        internal void SavePreview(string path)
        {
            UpdateLayout();
            var bitmap = new RenderTargetBitmap((int)Math.Ceiling(Width), (int)Math.Ceiling(Height), 96, 96, PixelFormats.Pbgra32);
            bitmap.Render((Visual)Content); var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
            using (var stream = File.Create(path)) png.Save(stream);
        }
        internal bool IsIdle { get { return current == null && !animating; } }
        internal bool IsOffscreen { get { return IsIdle && offset <= HiddenOffset + 0.1; } }
        internal double VisibleWidth { get { return island.Width; } }
        internal double VisibleHeight { get { return island.Height; } }
        internal bool Holding { get { return current != null && !awaitingHold && !changingNotice; } }
        internal bool HasMusic { get { return music != null; } }
        internal string MusicTitle { get { return musicView.DisplayedTitle; } }
        internal bool MusicPlaying { get { return music != null && music.Playing; } }
        internal bool MusicHasCover { get { return musicView.HasCover; } }
        internal string CurrentLyric { get { return musicView.VisibleLyric; } }
        internal double MusicTop { get { return 0; } }
        internal double NoticeTop { get { return musicHeight; } }
        internal double NoticeHeight { get { return noticeViewport.Height; } }
        internal double NoticeOpacity { get { return content.Opacity; } }
        internal bool MusicExpanded { get { return musicView.Expanded; } }
        internal bool Animating { get { return animating; } }
        internal double ContainerRadius { get { return island.CornerRadius.TopLeft; } }
        internal int RenderTier { get { return Compatibility.RenderTier; } }
        internal bool LightweightRendering { get { return Compatibility.Lightweight; } }
        internal string CompatibilityDiagnostic { get { return Compatibility.Diagnostic; } }
        internal long AnimationRenderFrames { get { return framePacer.Frames; } }
        internal long MusicRenderFrames { get { return musicView.RenderFrames; } }
        internal int PendingCount { get { return pending.Count; } }
        internal long WindowHandle { get { return handle.ToInt64(); } }
        internal double MusicHeight { get { return musicView.Height; } }
        internal double MusicWidth { get { return musicView.Width; } }
        internal double BarsLevel { get { return musicView.BarsLevel; } }
        internal int MusicControls { get { return musicView.VisibleControls; } }
        internal bool MusicVisible { get { return musicView.Visibility == Visibility.Visible; } }
        internal bool AudioAvailable { get { return musicView.AudioAvailable; } }
        internal bool ModeVisible { get { return musicView.ModeVisible; } }
        internal void ExpandMusic(bool value) { musicView.SetExpanded(value); }
    }
}
