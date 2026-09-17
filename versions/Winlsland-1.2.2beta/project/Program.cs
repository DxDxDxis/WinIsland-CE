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
[assembly: AssemblyVersion(WinIsland.AppVersion.Number)]
[assembly: AssemblyFileVersion(WinIsland.AppVersion.Number)]

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
        private MediaSessions media;
        private AudioLevels audio;
        private readonly MusicVisibility musicVisibility = new MusicVisibility();
        private DispatcherTimer inactivityTimer;
        private MusicState music;
        private LyricStore lyricStore;
        private Lyrics lyrics;
        private string lyricTrack;
        private bool readerHealthy, verificationMusicOverride;
        private IslandSettings settings;
        private SettingsWindow settingsWindow;
        private NativeToastController nativeToasts;
        private bool sessionPaused, powerPaused, wasPaused;
        private bool exiting;
        private readonly string previewDirectory, verifyDirectory;
        private DispatcherTimer previewTimer, verificationTimer;
        private int previewStage;
        private int syntheticDelivered;

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
            string perfDirectory=Environment.GetEnvironmentVariable("WINISLAND_PERF_DIR");
            if (!string.IsNullOrWhiteSpace(perfDirectory)) Performance.Start(perfDirectory,Dispatcher);
            // NotifyIcon must use WPF's dispatcher; installing the WinForms context
            // can deadlock mixed-framework teardown during a tray menu callback.
            Forms.WindowsFormsSynchronizationContext.AutoInstall = false;
            if (previewDirectory == null && verifyDirectory == null)
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
            lyricStore = new LyricStore(previewDirectory != null ? null : verifyDirectory != null ? Path.Combine(verifyDirectory, "lyrics") : Path.Combine(Path.GetDirectoryName(IslandSettings.DefaultPath), "lyrics"));
            island.MusicCommand = (command, key) => media == null ? System.Threading.Tasks.Task.FromResult(false) : media.Command(command, key);
            audio = new AudioLevels(verifyDirectory != null);
            island.Audio = () => audio.Snapshot;
            inactivityTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(200) };
            inactivityTimer.Tick += delegate {
                if (music != null && !music.Playing && musicVisibility.Project(music, DateTime.UtcNow) == null)
                    island.SetMusic(null, null);
            };
            inactivityTimer.Start();
            island.SessionPaused = delegate(bool value) { sessionPaused = value; UpdatePaused(); };
            island.PowerPaused = delegate(bool value) { powerPaused = value; UpdatePaused(); };
            island.ApplySettings(settings);
            island.Show();
            settings.Changed += ApplySettings;
            ApplySettings();
            if (previewDirectory == null)
            {
                pump = new NotificationPump(n => Post(delegate { if (!exiting && pump.IsCurrent(n)) island.Enqueue(n); }),
                    healthy => Post(delegate {
                        if (exiting) return;
                        readerHealthy = healthy;
                        tray.Text = healthy ? "WinIsland" : "WinIsland · 通知读取暂不可用，已恢复系统横幅";
                        ApplySettings();
                    }));
                pump.Start();
                media = new MediaSessions((value, epoch) => Post(delegate {
                    if (!exiting && media.IsCurrent(epoch) && !verificationMusicOverride) ApplyMusic(value);
                }), verifyDirectory != null);
                media.Start();
            }
            if (previewDirectory != null) StartPreview();
            if (verifyDirectory != null) StartVerification();
        }

        private void Post(Action action)
        { if (!exiting && !Dispatcher.HasShutdownStarted) { long queued=Performance.Begin(); Dispatcher.BeginInvoke(new Action(delegate { Performance.End(PerfPart.DispatchWait,queued); action(); })); } }
        private void OpenSettings()
        {
            if (settingsWindow == null)
            {
                settingsWindow = new SettingsWindow(settings);
                settingsWindow.ImportLyrics = ImportLyrics;
                settingsWindow.ClearLyrics = ClearLyrics;
                settingsWindow.UpdateMusic(music);
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
                try { nativeToasts.SetEnabled(settings.HideNative && readerHealthy && !wasPaused); }
                catch (Exception ex) { Log.Error("Native notification control", ex); tray.Text = "WinIsland · 系统通知隐藏暂不可用"; }
            }
        }
        private void UpdatePaused()
        {
            bool value = sessionPaused || powerPaused;
            if (value == wasPaused) return; wasPaused = value;
            if (pump != null) pump.Pause(value);
            if (media != null) media.Pause(value);
            music = null; lyricTrack = null; lyrics = null;
            musicVisibility.Project(null, DateTime.UtcNow); audio.Select(null);
            if (settingsWindow != null) settingsWindow.UpdateMusic(null);
            island.Suspend(value); ApplySettings();
        }
        private void ApplyMusic(MusicState value)
        {
            long started=Performance.Begin();try {
            music = value;
            audio.Select(value);
            string key = value == null || value.Loading ? null : value.TrackKey;
            if (key != lyricTrack) { lyricTrack = key; lyrics = key == null ? null : lyricStore.Load(key); }
            island.SetMusic(musicVisibility.Project(value, DateTime.UtcNow), lyrics);
            if (settingsWindow != null) settingsWindow.UpdateMusic(value);
            } finally { Performance.End(PerfPart.UiMusic,started); }
        }
        private void ImportLyrics()
        {
            if (music == null || music.Loading || !music.HasTimeline) return;
            string key = music.TrackKey;
            var dialog = new Microsoft.Win32.OpenFileDialog { Title = "为当前歌曲载入歌词", Filter = "LRC 歌词文件 (*.lrc)|*.lrc", CheckFileExists = true };
            if (dialog.ShowDialog(settingsWindow) != true) return;
            if (music == null || music.TrackKey != key)
            { MessageBox.Show(settingsWindow, "歌曲已切换，请为当前歌曲重新选择歌词文件。", "载入歌词"); return; }
            try { lyrics = lyricStore.Import(key, dialog.FileName); island.SetMusic(musicVisibility.Project(music, DateTime.UtcNow), lyrics); }
            catch (Exception ex) { MessageBox.Show(settingsWindow, "无法载入歌词：" + ex.Message, "载入歌词"); }
        }
        private void ClearLyrics()
        {
            if (music == null || music.Loading) return;
            try { lyricStore.Remove(music.TrackKey); lyrics = null; island.SetMusic(musicVisibility.Project(music, DateTime.UtcNow), null); }
            catch (Exception ex) { MessageBox.Show(settingsWindow, "无法清除歌词：" + ex.Message, "清除歌词"); }
        }
        protected override void OnSessionEnding(SessionEndingCancelEventArgs e)
        { Quit(false); base.OnSessionEnding(e); }
        internal void Quit(bool removeStartup)
        {
            if (exiting) return;
            if (inactivityTimer != null) inactivityTimer.Stop();
            if (audio != null) audio.Dispose();
            if (removeStartup && previewDirectory == null && verifyDirectory == null)
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
            if (media != null) media.Dispose();
            if (previewTimer != null) previewTimer.Stop();
            if (verificationTimer != null) verificationTimer.Stop();
            if (tray != null) { tray.Visible = false; tray.Dispose(); }
            if (menu != null) menu.Dispose();
            if (icon != null) icon.Dispose();
            if (settingsWindow != null) settingsWindow.Close();
            if (island != null) island.Close();
            if (verifyDirectory != null && removeStartup)
            {
                File.WriteAllText(Path.Combine(verifyDirectory, "exit-result.txt"), "PASS: diagnostic tray exit disposed UI without modifying user autostart");
            }
            Shutdown();
            Performance.Stop();
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
                if (n.Title != null && n.Title.StartsWith("合成会话 ", StringComparison.Ordinal)) syntheticDelivered++;
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
                    else if (command.StartsWith("frame=")) { OpenSettings(); settingsWindow.FrameRateBox.Text = command.Substring(6); }
                    else if (command.StartsWith("phase=")) Performance.Phase=command.Substring(6);
                    else if (command == "perf-lyrics" && music != null) { var text=new System.Text.StringBuilder();for(int i=0;i<4000;i++)text.AppendLine("[00:00.00]性能测试歌词用于测量排版耗时，不是真实歌曲歌词，第"+i+"行"); lyrics=Lyrics.Parse(text.ToString()); island.SetMusic(musicVisibility.Project(music,DateTime.UtcNow),lyrics); }
                    else if (command == "perf-poll" && music != null) island.SetMusic(musicVisibility.Project(music,DateTime.UtcNow),lyrics);
                    else if (command == "perf-flush") System.Threading.Tasks.Task.Run(new Action(Performance.Flush));
                    else if (command == "capture-settings" && settingsWindow != null) settingsWindow.SavePreview(Path.Combine(verifyDirectory, "settings.png"));
                    else if (command == "notice-short") island.Enqueue(new Notice { Id = -20, Title = "完成", Body = "已收到" });
                    else if (command == "notice-long") island.Enqueue(new Notice { Id = -21, Title = "WinIsland 集成测试", Body = "这是一条用于验证灵动岛的 Windows 系统通知。" });
                    else if (command == "notice-wide") island.Enqueue(new Notice { Id = -22, Title = "较长消息布局验证", Body = "这是一条较长的合成消息，用于检查同一个容器向下延伸、横向自适应和自动换行。音乐控制始终保留在上方，消息结束后恢复原来尺寸。" });
                    else if (command == "music-live") { verificationMusicOverride = false; }
                    else if (command == "media-fixture-only") { verificationMusicOverride = false; media.FixtureOnly = true; }
                    else if (command == "music-stop") { verificationMusicOverride = true; ApplyMusic(null); }
                    else if (command.StartsWith("music="))
                    {
                        verificationMusicOverride = true;
                        bool second = command == "music=next";
                        ApplyMusic(new MusicState { SourceId = "WinIsland.MediaFixture", Platform = "媒体集成测试", Title = second ? "合成歌曲 B" : "合成歌曲 A",
                            Artist = "测试歌手", Album = "测试专辑", Playing = command != "music=pause", Paused = command == "music=pause",
                            HasTimeline = true, Position = 15, Duration = 180 });
                        lyrics = second ? null : Lyrics.Parse("[00:00.00]合成歌词：用于同步验证\n[00:16.00]下一行合成歌词");
                        island.SetMusic(musicVisibility.Project(music, DateTime.UtcNow), lyrics);
                    }
                    else if (command == "music-expand") island.ExpandMusic(true);
                    else if (command == "music-collapse") island.ExpandMusic(false);
                    else if (command == "lyrics-fixture" && music != null && music.SourceId == "WinIsland.MediaFixture")
                    {
                        lyrics = lyricStore.Import(music.TrackKey, Path.Combine(verifyDirectory, "fixture.lrc"));
                        island.SetMusic(musicVisibility.Project(music, DateTime.UtcNow), lyrics);
                    }
                    else if (command == "capture") island.SavePreview(Path.Combine(verifyDirectory, "capture.png"));
                    else if (command == "burst")
                        for (int i = 0; i < 12; i++) island.Enqueue(new Notice { Id = -100 - i, HandlerId = 12, ArrivalTime = i, Title = "合成会话 " + (i + 1), Body = "连续消息与音乐共存测试" });
                }
                File.WriteAllLines(Path.Combine(verifyDirectory, "state.txt"), new[] {
                    "Resident=" + settings.Resident, "HideNative=" + settings.HideNative, "Seconds=" + settings.DwellSeconds,
                    "Idle=" + island.IsIdle, "Offscreen=" + island.IsOffscreen, "Holding=" + island.Holding,
                    "Width=" + island.VisibleWidth, "Height=" + island.VisibleHeight,
                    "HelperId=" + nativeToasts.HelperId, "Masks=" + nativeToasts.ActiveMasks,
                    "SettingsOpen=" + (settingsWindow != null), "HasMusic=" + island.HasMusic,
                    "MusicPlaying=" + island.MusicPlaying, "MusicHasCover=" + island.MusicHasCover,
                    "MusicTop=" + island.MusicTop, "Pending=" + island.PendingCount,
                    "NoticeTop=" + island.NoticeTop, "NoticeHeight=" + island.NoticeHeight, "NoticeOpacity=" + island.NoticeOpacity,
                    "MusicExpanded=" + island.MusicExpanded, "Animating=" + island.Animating, "Radius=" + island.ContainerRadius,
                    "SyntheticDelivered=" + syntheticDelivered,
                    "HasLyric=" + !string.IsNullOrEmpty(island.CurrentLyric), "ReaderHealthy=" + readerHealthy,
                    "MusicPlatform=" + (music == null ? "" : music.Platform),
                    "MusicWindow=" + island.WindowHandle,
                    "MusicHeight=" + island.MusicHeight, "MusicWidth=" + island.MusicWidth,
                    "MusicVisible=" + island.MusicVisible,
                    "FixtureTitle=" + (music != null && music.SourceId == "WinIsland.MediaFixture" ? island.MusicTitle : ""),
                    "FixtureLyric=" + (music != null && music.SourceId == "WinIsland.MediaFixture" ? island.CurrentLyric : ""),
                    "Bars=" + island.BarsLevel, "MusicControls=" + island.MusicControls,
                    "AudioAvailable=" + island.AudioAvailable,
                    "AudioPeak=" + (audio.Snapshot.Peaks.Length == 0 ? 0 : System.Linq.Enumerable.Max(audio.Snapshot.Peaks)),
                    "AudioMuted=" + audio.Snapshot.Muted,
                    "AudioDiagnostic=" + audio.Diagnostic,
                    "ModeVisible=" + island.ModeVisible, "Mode=" + (music == null ? "" : music.ModeName),
                    "HasTimeline=" + (music != null && music.HasTimeline),
                    "RenderTier=" + island.RenderTier, "LightweightRendering=" + island.LightweightRendering,
                    "FrameIntervalMs=" + Compatibility.FrameIntervalMs, "ConfiguredFrameRate=" + Compatibility.ConfiguredFrameRate,
                    "EffectiveFrameRate=" + Compatibility.EffectiveFrameRate,
                    "MusicRenderFrames=" + island.MusicRenderFrames, "AnimationRenderFrames=" + island.AnimationRenderFrames,
                    "Compatibility=" + island.CompatibilityDiagnostic });
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
            if (args.Length == 2 && args[0] == "--media-fixture") return MediaFixture.Run(Path.GetFullPath(args[1]));
            if (args.Length == 2 && args[0] == "--self-test") return SelfTests.Run(args[1]);
            string preview = args.Length == 2 && args[0] == "--preview" ? Path.GetFullPath(args[1]) : null;
            string verify = args.Length == 2 && args[0] == "--verify" ? Path.GetFullPath(args[1]) : null;
            bool created;
            string mutexName = preview != null || verify != null ? "Local\\WinIsland.Diagnostics." + Process.GetCurrentProcess().Id : "Local\\WinIsland.Desktop.Singleton";
            using (var mutex = new Mutex(true, mutexName, out created))
            {
                if (!created) return 0;
                try { new IslandApp(preview, verify).Run(); return 0; }
                catch (Exception ex) { Log.Error("Startup", ex); return 1; }
                finally { mutex.ReleaseMutex(); }
            }
        }
    }
}
