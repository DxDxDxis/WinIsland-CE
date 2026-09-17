using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Threading;
using System.Windows;
using System.Windows.Threading;
using Microsoft.Win32;
using Forms = System.Windows.Forms;

[assembly: AssemblyTitle("WinIsland")]
[assembly: AssemblyDescription("Windows 通知灵动岛")]
[assembly: AssemblyCompany("WinIsland")]
[assembly: AssemblyProduct("WinIsland")]
[assembly: AssemblyVersion("1.1.0.0")]
[assembly: AssemblyFileVersion("1.1.0.0")]

namespace WinIsland
{
    internal static class Log
    {
        private static readonly object Gate = new object();
        internal static void Error(string context, Exception ex)
        {
            try
            {
                lock (Gate)
                {
                    string dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "WinIsland");
                    Directory.CreateDirectory(dir);
                    string path = Path.Combine(dir, "diagnostics.log");
                    if (File.Exists(path) && new FileInfo(path).Length > 128 * 1024) File.Delete(path);
                    File.AppendAllText(path, DateTime.Now.ToString("s") + " " + context + " " + ex.GetType().Name +
                        " 0x" + ex.HResult.ToString("X8") + Environment.NewLine);
                }
            }
            catch { }
        }
    }

    internal static class AutoStart
    {
        internal const string Key = @"Software\Microsoft\Windows\CurrentVersion\Run";
        internal static void Register()
        {
            string exe = Process.GetCurrentProcess().MainModule.FileName;
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(Key))
                key.SetValue("WinIsland", "\"" + exe + "\"", RegistryValueKind.String);
        }
        internal static void Remove()
        {
            using (RegistryKey key = Registry.CurrentUser.OpenSubKey(Key, true))
                if (key != null) key.DeleteValue("WinIsland", false);
        }
    }

    internal sealed class IslandApp : Application
    {
        private IslandWindow island;
        private Forms.NotifyIcon tray;
        private Forms.ContextMenuStrip menu;
        private Icon icon;
        private NotificationPump pump;
        private IslandSettings settings;
        private SettingsWindow settingsWindow;
        private NativeToastController nativeToasts;
        private bool sessionPaused, powerPaused, wasPaused;
        private bool exiting;
        private readonly string previewDirectory, verifyDirectory;
        private DispatcherTimer previewTimer, verificationTimer;
        private int previewStage;

        internal IslandApp(string previewDirectory, string verifyDirectory)
        {
            this.previewDirectory = previewDirectory; this.verifyDirectory = verifyDirectory;
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            DispatcherUnhandledException += delegate(object sender, DispatcherUnhandledExceptionEventArgs e)
            { Log.Error("UI", e.Exception); e.Handled = true; Quit(false); };
        }
        protected override void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            // NotifyIcon must use WPF's dispatcher; installing the WinForms context
            // can deadlock mixed-framework teardown during a tray menu callback.
            Forms.WindowsFormsSynchronizationContext.AutoInstall = false;
            if (previewDirectory == null)
            {
                try { AutoStart.Register(); } catch (Exception ex) { Log.Error("Startup registration", ex); }
            }
            Native.SetCurrentProcessExplicitAppUserModelID("WinIsland.Desktop");
            using (Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream("WinIsland.ico"))
                icon = new Icon(resource);
            settings = new IslandSettings(previewDirectory != null ? null : verifyDirectory != null ? Path.Combine(verifyDirectory, "settings.xml") : IslandSettings.DefaultPath);
            nativeToasts = new NativeToastController(verifyDirectory != null);
            menu = new Forms.ContextMenuStrip { ShowImageMargin = false };
            menu.Items.Add("设置", null, delegate { Post(OpenSettings); });
            menu.Items.Add("退出", null, delegate { Post(delegate { Quit(true); }); });
            tray = new Forms.NotifyIcon { Text = "WinIsland", Icon = icon, ContextMenuStrip = menu, Visible = true };
            island = new IslandWindow(); MainWindow = island;
            island.SessionPaused = delegate(bool value) { sessionPaused = value; UpdatePaused(); };
            island.PowerPaused = delegate(bool value) { powerPaused = value; UpdatePaused(); };
            island.ApplySettings(settings);
            island.Show();
            settings.Changed += ApplySettings;
            ApplySettings();
            if (previewDirectory == null)
                pump = new NotificationPump(n => Post(delegate { if (!exiting) island.Enqueue(n); }),
                    healthy => Post(delegate { if (!exiting) tray.Text = healthy ? "WinIsland" : "WinIsland · 正在等待 Windows 通知服务"; }));
            if (previewDirectory != null) StartPreview();
            if (verifyDirectory != null) StartVerification();
        }

        private void Post(Action action)
        { if (!exiting && !Dispatcher.HasShutdownStarted) Dispatcher.BeginInvoke(action); }
        private void OpenSettings()
        {
            if (settingsWindow == null)
            {
                settingsWindow = new SettingsWindow(settings);
                settingsWindow.Closed += delegate { settingsWindow = null; };
                settingsWindow.Show();
            }
            else { if (settingsWindow.WindowState == WindowState.Minimized) settingsWindow.WindowState = WindowState.Normal; settingsWindow.Activate(); }
        }
        private void ApplySettings()
        {
            island.ApplySettings(settings);
            if (previewDirectory == null)
            {
                try { nativeToasts.SetEnabled(settings.HideNative); }
                catch (Exception ex) { Log.Error("Native notification control", ex); tray.Text = "WinIsland · 系统通知隐藏暂不可用"; }
            }
        }
        private void UpdatePaused()
        {
            bool value = sessionPaused || powerPaused;
            if (value == wasPaused) return; wasPaused = value;
            if (pump != null) pump.Pause(value); island.Suspend(value);
        }
        protected override void OnSessionEnding(SessionEndingCancelEventArgs e)
        { Quit(false); base.OnSessionEnding(e); }
        internal void Quit(bool removeStartup)
        {
            if (exiting) return;
            if (removeStartup && previewDirectory == null)
            {
                try { AutoStart.Remove(); }
                catch (Exception ex)
                {
                    Log.Error("Startup removal", ex);
                    tray.ShowBalloonTip(6000, "WinIsland", "无法移除开机启动项，请在任务管理器的启动应用中禁用 WinIsland。", Forms.ToolTipIcon.Warning);
                }
            }
            exiting = true;
            if (nativeToasts != null) nativeToasts.Dispose();
            if (pump != null) pump.Dispose();
            if (previewTimer != null) previewTimer.Stop();
            if (verificationTimer != null) verificationTimer.Stop();
            if (tray != null) { tray.Visible = false; tray.Dispose(); }
            if (menu != null) menu.Dispose();
            if (icon != null) icon.Dispose();
            if (settingsWindow != null) settingsWindow.Close();
            if (island != null) island.Close();
            if (verifyDirectory != null && removeStartup)
            {
                using (var key = Registry.CurrentUser.OpenSubKey(AutoStart.Key))
                    File.WriteAllText(Path.Combine(verifyDirectory, "exit-result.txt"), key == null || key.GetValue("WinIsland") == null ? "PASS: tray exit removed startup and disposed UI" : "FAIL: startup remains");
            }
            Shutdown();
        }
        private void StartPreview()
        {
            Directory.CreateDirectory(previewDirectory);
            previewTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
            previewTimer.Tick += delegate
            {
                previewStage++;
                if (previewStage == 1)
                {
                    island.SavePreview(Path.Combine(previewDirectory, "idle.png"));
                    island.Enqueue(new Notice { Id = -1, Title = "WinIsland 已准备就绪", Body = "收到新的通知时，灵动岛会平滑展开。\n标题与内容在这里呈现，片刻后自动收回。" });
                }
                if (previewStage == 3) island.SavePreview(Path.Combine(previewDirectory, "expanded.png"));
                if (previewStage == 7)
                {
                    PreviewCheck(island.IsIdle, "default four-second hold and collapse");
                    settings.Update(false, true, 4);
                }
                if (previewStage == 8)
                {
                    PreviewCheck(island.IsOffscreen, "resident switch off slides idle pill above the display");
                    island.Enqueue(new Notice { Id = -2, Title = "完成", Body = "下载已完成。" });
                }
                if (previewStage == 9)
                {
                    island.SavePreview(Path.Combine(previewDirectory, "short.png"));
                    PreviewCheck(island.Holding && island.VisibleWidth < 250, "hidden island slides in and short content uses compact width");
                    settings.Update(false, true, 0.2);
                }
                if (previewStage == 10)
                {
                    PreviewCheck(island.IsOffscreen, "changing dwell time immediately shortens the current notification and slides out");
                    settings.Update(true, true, 1);
                    island.Enqueue(new Notice { Id = -3, Title = "WinIsland 1.1 · 内容自适应通知", Body = "通知浮窗的宽度和高度会根据标题与正文自动调整。\n较长的内容会自动换行，保持清晰、舒适的阅读间距。\n常驻开关、系统通知隐藏和停驻时间都可以在设置中即时调整。" });
                }
                if (previewStage == 11)
                {
                    island.SavePreview(Path.Combine(previewDirectory, "long.png"));
                    PreviewCheck(island.VisibleWidth > 400 && island.VisibleHeight > 120, "long content grows in both dimensions");
                }
                if (previewStage == 13)
                {
                    PreviewCheck(island.IsIdle && !island.IsOffscreen, "resident switch on restores the idle pill");
                    settings.Update(true, true, 4); OpenSettings();
                }
                if (previewStage == 14)
                {
                    settingsWindow.SavePreview(Path.Combine(previewDirectory, "settings.png"));
                    PreviewCheck(settingsWindow.SecondsBox.Text == "4" && settingsWindow.ResidentSwitch.IsChecked == true && settingsWindow.NativeSwitch.IsChecked == true, "settings window defaults and control layout");
                    settingsWindow.SecondsBox.Text = "-5";
                    PreviewCheck(settings.DwellSeconds == 4, "invalid dwell input preserves the last valid value");
                    Quit(false);
                }
            };
            previewTimer.Start();
        }
        private void PreviewCheck(bool passed, string text)
        { File.AppendAllText(Path.Combine(previewDirectory, "preview-result.txt"), (passed ? "PASS: " : "FAIL: ") + text + Environment.NewLine); }
        private void StartVerification()
        {
            Directory.CreateDirectory(verifyDirectory);
            island.NoticeDisplayed = delegate(Notice n)
            {
                // Only synthetic diagnostics are recorded; never save the user's notification text.
                if (n.Title == "WinIsland 集成测试")
                {
                    File.WriteAllText(Path.Combine(verifyDirectory, "notification-received.txt"), "PASS: Windows toast title/body received; body=" + (n.Body == "这是一条用于验证灵动岛的 Windows 系统通知。"));
                    var capture = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
                    capture.Tick += delegate { capture.Stop(); island.SavePreview(Path.Combine(verifyDirectory, "system-toast.png")); };
                    capture.Start();
                }
            };
            verificationTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(100) };
            verificationTimer.Tick += delegate
            {
                try
                {
                if (File.Exists(Path.Combine(verifyDirectory, "exit.request")))
                {
                    menu.Items[1].PerformClick();
                }
                string commandPath = Path.Combine(verifyDirectory, "command.txt");
                if (File.Exists(commandPath))
                {
                    string command = File.ReadAllText(commandPath).Trim(); File.Delete(commandPath);
                    if (command == "settings") menu.Items[0].PerformClick();
                    else if (command == "close-settings" && settingsWindow != null) settingsWindow.Close();
                    else if (command.StartsWith("resident=")) { OpenSettings(); settingsWindow.ResidentSwitch.IsChecked = command == "resident=on"; }
                    else if (command.StartsWith("native=")) { OpenSettings(); settingsWindow.NativeSwitch.IsChecked = command == "native=on"; }
                    else if (command.StartsWith("seconds=")) { OpenSettings(); settingsWindow.SecondsBox.Text = command.Substring(8); }
                    else if (command == "notice-short") island.Enqueue(new Notice { Id = -20, Title = "完成", Body = "已收到" });
                    else if (command == "notice-long") island.Enqueue(new Notice { Id = -21, Title = "WinIsland 集成测试", Body = "这是一条用于验证灵动岛的 Windows 系统通知。" });
                }
                File.WriteAllLines(Path.Combine(verifyDirectory, "state.txt"), new[] {
                    "Resident=" + settings.Resident, "HideNative=" + settings.HideNative, "Seconds=" + settings.DwellSeconds,
                    "Idle=" + island.IsIdle, "Offscreen=" + island.IsOffscreen, "Holding=" + island.Holding,
                    "Width=" + island.VisibleWidth, "Height=" + island.VisibleHeight,
                    "HelperId=" + nativeToasts.HelperId, "Masks=" + nativeToasts.ActiveMasks,
                    "SettingsOpen=" + (settingsWindow != null) });
                }
                catch (IOException) { /* A diagnostic client may have a snapshot open; retry next tick. */ }
            };
            verificationTimer.Start();
            File.WriteAllText(Path.Combine(verifyDirectory, "ready.txt"), Process.GetCurrentProcess().Id.ToString());
        }
    }

    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            if (args.Length == 4 && args[0] == "--native-helper")
            {
                int parent; long ticks;
                if (int.TryParse(args[1], out parent) && long.TryParse(args[2], out ticks))
                    return NativeToastMasker.RunHelper(parent, ticks, args[3] == "fixture");
                return 1;
            }
            if (args.Length == 2 && args[0] == "--native-fixture") return NativeToastMasker.RunFixture(args[1]);
            if (args.Length == 2 && args[0] == "--self-test") return SelfTests.Run(args[1]);
            string preview = args.Length == 2 && args[0] == "--preview" ? Path.GetFullPath(args[1]) : null;
            string verify = args.Length == 2 && args[0] == "--verify" ? Path.GetFullPath(args[1]) : null;
            bool created;
            using (var mutex = new Mutex(true, "Local\\WinIsland.Desktop.Singleton", out created))
            {
                if (!created) return 0;
                try { new IslandApp(preview, verify).Run(); return 0; }
                catch (Exception ex) { Log.Error("Startup", ex); return 1; }
                finally { mutex.ReleaseMutex(); }
            }
        }
    }
}
