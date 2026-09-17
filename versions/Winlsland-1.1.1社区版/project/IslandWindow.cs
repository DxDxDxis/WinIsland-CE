using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace WinIsland
{
    internal sealed class IslandWindow : Window
    {
        internal const double IdleWidth = 164, IdleHeight = 34, HiddenOffset = -54;
        private readonly Border island;
        private readonly StackPanel content;
        private readonly TextBlock title, body;
        private readonly RectangleGeometry clip = new RectangleGeometry();
        private readonly Queue<Notice> pending = new Queue<Notice>();
        private readonly DispatcherTimer dismiss;
        private readonly Stopwatch animation = new Stopwatch(), hold = new Stopwatch();
        private double maxWidth = 560, hostHeight = 540, offset, screenScale = 1;
        private double fromWidth, fromHeight, fromRadius, fromOpacity, fromOffset;
        private double toWidth, toHeight, toRadius, toOpacity, toOffset, duration;
        private int screenX, screenY, lastY = int.MinValue;
        private bool animating, suspended, resident = true, awaitingHold;
        private double dwellSeconds = 4;
        private Notice current;
        private IntPtr handle;
        internal Action<Notice> NoticeDisplayed;
        internal Action<bool> SessionPaused, PowerPaused;

        internal IslandWindow()
        {
            Title = "WinIsland"; WindowStyle = WindowStyle.None; ResizeMode = ResizeMode.NoResize;
            AllowsTransparency = true; Background = Brushes.Transparent; ShowInTaskbar = false;
            ShowActivated = false; Focusable = false; Topmost = true;
            Width = 600; Height = hostHeight; UseLayoutRounding = true; SnapsToDevicePixels = true;
            var root = new Grid { IsHitTestVisible = false };
            island = new Border { Background = Brushes.Black, Width = IdleWidth, Height = IdleHeight,
                CornerRadius = new CornerRadius(17), HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Top, Clip = clip };
            var canvas = new Canvas();
            content = new StackPanel { Width = maxWidth - 48, Opacity = 0 };
            Canvas.SetLeft(content, 24); Canvas.SetTop(content, 21);
            title = new TextBlock { Foreground = Brushes.White, FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"),
                FontSize = 16, FontWeight = FontWeights.Bold, TextWrapping = TextWrapping.Wrap,
                TextTrimming = TextTrimming.CharacterEllipsis, MaxHeight = 96, LineHeight = 24 };
            body = new TextBlock { Foreground = new SolidColorBrush(Color.FromRgb(223, 223, 228)),
                FontFamily = new FontFamily("Segoe UI, Microsoft YaHei UI"), FontSize = 14,
                TextWrapping = TextWrapping.Wrap, TextTrimming = TextTrimming.CharacterEllipsis,
                LineHeight = 22, Margin = new Thickness(0, 8, 0, 0) };
            content.Children.Add(title); content.Children.Add(body); canvas.Children.Add(content);
            island.Child = canvas; root.Children.Add(island); Content = root;
            dismiss = new DispatcherTimer(); dismiss.Tick += delegate { dismiss.Stop(); FinishNotice(); };
            SourceInitialized += delegate
            {
                handle = new WindowInteropHelper(this).Handle;
                Native.SetWindowLong(handle, Native.GwlExStyle, Native.GetWindowLong(handle, Native.GwlExStyle) |
                    Native.ToolWindow | Native.NoActivate | Native.Transparent);
                HwndSource.FromHwnd(handle).AddHook(WindowMessage);
                Native.WTSRegisterSessionNotification(handle, 0); PositionOnPrimary(); UpdateClip();
            };
            Closed += delegate { Native.WTSUnRegisterSessionNotification(handle); dismiss.Stop(); StopAnimation(); };
        }
        private IntPtr WindowMessage(IntPtr hwnd, int msg, IntPtr wp, IntPtr lp, ref bool handled)
        {
            if (msg == Native.WmMouseActivate) { handled = true; return new IntPtr(3); }
            if (msg == Native.WmNcHitTest) { handled = true; return new IntPtr(-1); }
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
            maxWidth = Math.Max(IdleWidth, Math.Min(560, available - 32)); Width = Math.Min(600, available);
            hostHeight = Math.Max(96, Math.Min(540, (info.Monitor.Bottom - info.Monitor.Top) / screenScale * 0.65)); Height = hostHeight;
            screenX = info.Monitor.Left + ((info.Monitor.Right - info.Monitor.Left) - (int)Math.Round(Width * screenScale)) / 2;
            screenY = info.Monitor.Top + (int)Math.Round(8 * screenScale);
            lastY = screenY + (int)Math.Round(offset * screenScale);
            Native.SetWindowPos(handle, new IntPtr(-1), screenX, lastY,
                (int)Math.Round(Width * screenScale), (int)Math.Ceiling(Height * screenScale), 0x0010);
            if (current != null) { Size size = MeasureNotice(current); Animate(size.Width, size.Height, 28, 1, 0, 0.35); }
        }
        internal void ApplySettings(IslandSettings settings)
        {
            bool move = resident != settings.Resident, retime = dwellSeconds != settings.DwellSeconds;
            resident = settings.Resident; dwellSeconds = settings.DwellSeconds;
            if (move && current == null)
            {
                if (handle == IntPtr.Zero || suspended) { offset = resident ? 0 : HiddenOffset; MoveVertically(); }
                else Animate(IdleWidth, IdleHeight, 17, 0, resident ? 0 : HiddenOffset, 0.46);
            }
            if (retime && current != null && !awaitingHold) ScheduleDismiss();
        }
        internal void Enqueue(Notice notice)
        {
            if (suspended) return;
            if (current == null || current.Id == notice.Id) { SetNotice(notice); return; }
            if (pending.Count >= 8) pending.Dequeue(); pending.Enqueue(notice);
        }
        internal Size MeasureNotice(Notice notice)
        {
            title.Text = notice.Title ?? ""; body.Text = notice.Body ?? "";
            body.Visibility = string.IsNullOrWhiteSpace(body.Text) ? Visibility.Collapsed : Visibility.Visible;
            title.TextWrapping = body.TextWrapping = TextWrapping.NoWrap;
            title.MaxHeight = body.MaxHeight = double.PositiveInfinity;
            title.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            body.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
            double width = Math.Max(IdleWidth, Math.Min(maxWidth, Math.Ceiling(Math.Max(title.DesiredSize.Width, body.DesiredSize.Width)) + 48));
            title.TextWrapping = body.TextWrapping = TextWrapping.Wrap; content.Width = width - 48;
            title.MaxHeight = Math.Min(96, hostHeight * 0.3); title.Measure(new Size(content.Width, double.PositiveInfinity));
            body.MaxHeight = Math.Max(22, hostHeight - 42 - title.DesiredSize.Height - 8);
            content.Measure(new Size(content.Width, double.PositiveInfinity));
            return new Size(width, Math.Min(hostHeight, Math.Max(66, Math.Ceiling(content.DesiredSize.Height) + 42)));
        }
        private void SetNotice(Notice notice)
        {
            current = notice; Size size = MeasureNotice(notice);
            dismiss.Stop(); hold.Reset(); awaitingHold = true; Animate(size.Width, size.Height, 28, 1, 0, 0.52);
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
            if (pending.Count > 0) { SetNotice(pending.Dequeue()); return; }
            current = null; Animate(IdleWidth, IdleHeight, 17, 0, resident ? 0 : HiddenOffset, 0.46);
        }
        internal void Suspend(bool value)
        {
            suspended = value; dismiss.Stop(); pending.Clear(); current = null;
            awaitingHold = false; hold.Reset(); StopAnimation();
            island.Width = IdleWidth; island.Height = IdleHeight; island.CornerRadius = new CornerRadius(17);
            content.Opacity = 0; title.Text = ""; body.Text = ""; offset = resident ? 0 : HiddenOffset; UpdateClip();
            if (value) Hide(); else { Show(); PositionOnPrimary(); }
        }
        internal static double Ease(double t)
        {
            if (t <= 0) return 0; if (t >= 1) return 1;
            return (1 - (1 + 9 * t) * Math.Exp(-9 * t)) / (1 - 10 * Math.Exp(-9));
        }
        private void Animate(double width, double height, double radius, double opacity, double targetOffset, double seconds)
        {
            fromWidth = island.Width; fromHeight = island.Height; fromRadius = island.CornerRadius.TopLeft;
            fromOpacity = content.Opacity; fromOffset = offset;
            toWidth = width; toHeight = height; toRadius = Math.Min(radius, height / 2); toOpacity = opacity;
            toOffset = targetOffset; duration = seconds; animation.Restart();
            if (!animating) { animating = true; CompositionTarget.Rendering += RenderFrame; }
        }
        private void RenderFrame(object sender, EventArgs args)
        {
            double t = Math.Min(1, animation.Elapsed.TotalSeconds / duration), p = Ease(t);
            island.Width = Lerp(fromWidth, toWidth, p); island.Height = Lerp(fromHeight, toHeight, p);
            island.CornerRadius = new CornerRadius(Lerp(fromRadius, toRadius, p));
            double textProgress = toOpacity > fromOpacity ? Ease(Math.Max(0, (t - 0.16) / 0.84)) : Ease(Math.Min(1, t * 2));
            content.Opacity = Lerp(fromOpacity, toOpacity, textProgress);
            offset = Lerp(fromOffset, toOffset, p); MoveVertically(); UpdateClip();
            if (t >= 1)
            {
                StopAnimation();
                if (current == null) { title.Text = ""; body.Text = ""; }
                else if (awaitingHold) { awaitingHold = false; hold.Restart(); ScheduleDismiss(); }
            }
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
        { if (animating) { CompositionTarget.Rendering -= RenderFrame; animating = false; } animation.Stop(); }
        internal void SavePreview(string path)
        {
            UpdateLayout();
            var bitmap = new RenderTargetBitmap((int)Math.Ceiling(Width), (int)Math.Ceiling(Height), 96, 96, PixelFormats.Pbgra32);
            bitmap.Render((Visual)Content); var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
            using (var stream = File.Create(path)) png.Save(stream);
        }
        internal bool IsIdle { get { return current == null && !animating && Math.Abs(island.Width - IdleWidth) < 0.1; } }
        internal bool IsOffscreen { get { return IsIdle && offset <= HiddenOffset + 0.1; } }
        internal double VisibleWidth { get { return island.Width; } }
        internal double VisibleHeight { get { return island.Height; } }
        internal bool Holding { get { return current != null && !awaitingHold; } }
    }
}
