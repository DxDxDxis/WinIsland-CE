using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace WinIsland
{
    internal static class SelfTests
    {
        private static readonly List<string> results = new List<string>();
        private static void Check(bool value, string description)
        { if (!value) throw new InvalidOperationException(description); results.Add("PASS: " + description); }
        private static Notice Parse(string xml)
        { return ToastParser.Parse(1, Encoding.UTF8.GetBytes(xml)); }
        internal static int Run(string output)
        {
            try
            {
                Notice n = Parse("<toast><visual><binding template='ToastGeneric'><text>测试 &amp; title</text><text>第一行</text><text>第二行 😀</text><text placement='attribution'>应用名</text></binding></visual><actions><action content='按钮'/></actions></toast>");
                Check(n != null && n.Title == "测试 & title" && n.Body == "第一行\n第二行 😀", "Chinese, emoji, XML entities and body lines; attribution/actions excluded");
                n = ToastParser.Parse(2, Encoding.Unicode.GetBytes("<toast><visual><binding template='ToastText01'><text>Unicode 通知</text></binding></visual></toast>"));
                Check(n != null && n.Title == "Unicode 通知" && n.Body == "", "UTF-16 legacy toast and title-only layout");
                n = ToastParser.Parse(3, Encoding.BigEndianUnicode.GetBytes("<toast><visual><binding><text>大端</text></binding></visual></toast>"));
                Check(n != null && n.Title == "大端", "UTF-16 big-endian payload");
                Check(Parse("<toast><visual><binding><text>  </text></binding></visual></toast>") == null, "Empty toast ignored");
                Check(Parse("<invalid") == null, "Malformed payload ignored");
                Check(Parse("<!DOCTYPE toast [<!ENTITY x SYSTEM 'file:///C:/Windows/win.ini'>]><toast><visual><binding><text>&x;</text></binding></visual></toast>") == null, "External XML entities rejected");
                Check(Parse("<tile><visual><binding><text>not toast</text></binding></visual></tile>") == null, "Non-toast payload ignored");
                n = Parse("<toast xmlns='urn:test'><visual><binding><text>命名空间</text><text>内容</text></binding></visual></toast>");
                Check(n != null && n.Body == "内容", "Namespaced payload supported");
                n = Parse("<toast><visual><binding><text>" + new string('长', 15000) + "</text></binding></visual></toast>");
                Check(n != null && n.Title.Length == 12000, "Oversized visible text bounded");
                var tracker = new NoticeTracker();
                var old = new Notice { Id = 1, Fingerprint = "1:old", Title = "old" };
                var fresh = new Notice { Id = 2, Fingerprint = "2:new", Title = "new" };
                Check(tracker.Accept(new[] { old }).Count == 0, "Startup history establishes baseline without replay");
                Check(tracker.Accept(new[] { old, fresh }).Count == 1, "New notification delivered once");
                Check(tracker.Accept(new[] { fresh, old }).Count == 0, "Repeat polls and changed ordering do not repeat notifications");
                Check(tracker.Accept(new[] { new Notice { Id = 2, Fingerprint = "2:updated", Title = "updated" } }).Count == 1, "Updated notification detected");
                tracker.Reset();
                Check(tracker.Accept(new[] { old, fresh }).Count == 0, "Unlock/resume baseline prevents replay");
                double previous = 0;
                for (int i = 0; i <= 1000; i++)
                {
                    double p = IslandWindow.Ease(i / 1000.0);
                    if (p < previous || p < 0 || p > 1) throw new InvalidOperationException("Animation is not bounded and monotonic");
                    previous = p;
                }
                Check(IslandWindow.Ease(0) == 0 && IslandWindow.Ease(1) == 1 && IslandWindow.Ease(0.5) > 0.8,
                    "Nonlinear spring easing is monotonic, bounded and reaches both endpoints");
                var defaults = new IslandSettings(null);
                Check(defaults.Resident && defaults.HideNative && defaults.DwellSeconds == 4, "1.1 defaults are resident on, native hidden, and four seconds");
                Check(defaults.FrameRate == 0, "New and legacy settings default to system render cadence");
                int fps;
                Check(IslandSettings.ParseFrameRate("0", out fps) && fps == 0 && IslandSettings.ParseFrameRate("144", out fps) && fps == 144 &&
                    !IslandSettings.ParseFrameRate("29", out fps) && !IslandSettings.ParseFrameRate("241", out fps) &&
                    !IslandSettings.ParseFrameRate("60.5", out fps) && !IslandSettings.ParseFrameRate("", out fps), "Frame-rate setting accepts system/30-240 and rejects invalid entries");
                foreach (int hz in new[] { 60, 120, 144, 240 })
                {
                    foreach (int target in new[] { 0, 30, 60, 120, 144, 240 })
                    {
                        var pacer = new FramePacer();
                        for (int i = 0; i < hz * 10; i++)
                        {
                            long tick = (long)Math.Round(i * (double)TimeSpan.TicksPerSecond / hz);
                            pacer.ShouldRender(tick, target);
                            if (pacer.ShouldRender(tick, target)) throw new InvalidOperationException("Repeated compositor timestamp rendered twice");
                        }
                        int expected = Math.Min(hz, target == 0 ? hz : target) * 10;
                        if (Math.Abs(pacer.Frames - expected) > 1) throw new InvalidOperationException("Frame pacing drift at " + hz + " Hz / " + target + " target: " + pacer.Frames);
                    }
                }
                Check(true, "System and fixed pacing remain accurate at simulated 60/120/144/240 Hz without duplicate work");
                var resumedPacer = new FramePacer(); resumedPacer.ShouldRender(0, 30);
                Check(resumedPacer.ShouldRender(TimeSpan.TicksPerSecond * 10, 30) && !resumedPacer.ShouldRender(TimeSpan.TicksPerSecond * 10 + 100, 30) &&
                    resumedPacer.ShouldRender(TimeSpan.TicksPerSecond * 10 + 200, 144), "A long stall skips missed frames and live FPS changes take effect immediately");
                double seconds;
                Check(IslandSettings.ParseSeconds("0.5", out seconds) && seconds == 0.5 && IslandSettings.ParseSeconds("3600", out seconds), "Decimal and bounded positive duration inputs accepted");
                Check(!IslandSettings.ParseSeconds("-1", out seconds) && !IslandSettings.ParseSeconds("NaN", out seconds) &&
                    !IslandSettings.ParseSeconds("", out seconds) && !IslandSettings.ParseSeconds("3601", out seconds), "Invalid duration cannot change the active timer");
                string settingsPath = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(output)), "test-settings-" + Guid.NewGuid().ToString("N") + ".xml");
                try
                {
                    var saved = new IslandSettings(settingsPath); int changes = 0;
                    saved.Changed += delegate { changes++; };
                    saved.Update(false, false, 2.5); saved.Update(false, false, 2.5);
                    var loaded = new IslandSettings(settingsPath);
                    Check(!loaded.Resident && !loaded.HideNative && loaded.DwellSeconds == 2.5 && changes == 1, "Settings persist across launch and only real changes fire callbacks");
                    saved.Update(true, false, 5); loaded = new IslandSettings(settingsPath);
                    Check(loaded.Resident && !loaded.HideNative && loaded.DwellSeconds == 5, "Existing settings file is atomically replaced");
                    saved.Update(true, false, 5, 144); saved.Update(false, true, 2);
                    loaded = new IslandSettings(settingsPath);
                    Check(loaded.FrameRate == 144 && !loaded.Resident && loaded.HideNative && loaded.DwellSeconds == 2,
                        "Manual FPS persists across restart and other settings updates");
                    saved.Update(true, false, 5, 999);
                    Check(saved.FrameRate == 144 && saved.DwellSeconds == 2, "Invalid FPS does not mutate settings");
                    File.WriteAllText(settingsPath, "<WinIsland version='1.2.0'><Resident>false</Resident><HideNative>true</HideNative><DwellSeconds>3</DwellSeconds></WinIsland>");
                    loaded = new IslandSettings(settingsPath);
                    Check(loaded.FrameRate == 0 && !loaded.Resident && loaded.DwellSeconds == 3, "Legacy configuration retains existing preferences and defaults FPS to system");
                }
                finally { if (File.Exists(settingsPath)) File.Delete(settingsPath); }
                var window = new IslandWindow();
                var small = window.MeasureNotice(new Notice { Title = "完成", Body = "已收到" });
                var wide = window.MeasureNotice(new Notice { Title = "WinIsland", Body = "这是一段比简短通知更长的内容，用于验证宽度自适应。" });
                var large = window.MeasureNotice(new Notice { Title = new string('题', 100), Body = new string('文', 2000) });
                Check(small.Width < wide.Width && small.Height <= wide.Height && large.Height > wide.Height && large.Width <= 616 && large.Height <= 540,
                    "Content sizing grows width and height, wraps long text and stays within the display limit");
                window.Close();
                RegressionTests.Run(Check);
                using (var db = new NotificationDatabase(NotificationDatabase.DefaultPath))
                {
                    Check(NotificationDatabase.Sqlite.IsReadOnly(db), "Live Windows notification database opened strictly read-only");
                    db.Read();
                    Check(true, "Live Windows 11 schema and payload query supported");
                }
                File.WriteAllLines(output, results.ToArray(), Encoding.UTF8);
                return 0;
            }
            catch (Exception ex)
            {
                results.Add("FAIL: " + ex.ToString());
                File.WriteAllLines(output, results.ToArray(), Encoding.UTF8);
                return 1;
            }
        }
    }
}
