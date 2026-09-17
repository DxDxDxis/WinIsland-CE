using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Forms = System.Windows.Forms;

namespace WinIsland
{
    // A child process owns temporary window masks. The input pipe closes if the
    // main process crashes, so the child restores native windows before exiting.
    internal sealed class NativeToastController : IDisposable
    {
        private Process helper;
        private readonly bool fixtures;
        internal volatile int ActiveMasks;
        internal int HelperId { get { return helper != null && !helper.HasExited ? helper.Id : 0; } }
        internal NativeToastController(bool fixtures) { this.fixtures = fixtures; }
        internal void SetEnabled(bool enabled)
        {
            if (!enabled) { Dispose(); return; }
            if (helper != null && !helper.HasExited) return;
            var owner = Process.GetCurrentProcess();
            helper = new Process { StartInfo = new ProcessStartInfo(owner.MainModule.FileName,
                "--native-helper " + owner.Id + " " + owner.StartTime.ToUniversalTime().Ticks + " " + (fixtures ? "fixture" : "normal"))
            { UseShellExecute = false, CreateNoWindow = true, WindowStyle = ProcessWindowStyle.Hidden,
                RedirectStandardInput = true, RedirectStandardOutput = true } };
            helper.OutputDataReceived += delegate(object sender, DataReceivedEventArgs e)
            {
                int count;
                if (e.Data != null && e.Data.StartsWith("MASKS ") && int.TryParse(e.Data.Substring(6), out count)) ActiveMasks = count;
            };
            helper.Start(); helper.BeginOutputReadLine();
        }
        public void Dispose()
        {
            if (helper == null) return;
            try
            {
                if (!helper.HasExited)
                {
                    helper.StandardInput.Close();
                    // No dependency on the owner's dispatcher: the helper uses its own message loop.
                    if (!helper.WaitForExit(2500)) Log.Error("Native helper shutdown timeout", new TimeoutException());
                }
            }
            catch (Exception ex) { Log.Error("Native helper shutdown", ex); }
            helper.Dispose(); helper = null; ActiveMasks = 0;
        }
    }

    internal sealed class NativeToastMasker : IDisposable
    {
        private readonly Dictionary<IntPtr, Mask> masks = new Dictionary<IntPtr, Mask>();
        private readonly WinEvent callback;
        private readonly bool fixtures;
        private IntPtr hook;
        private bool disposed;
        private sealed class Mask { internal uint Process; internal bool Cloak; internal IntPtr Region; }
        internal NativeToastMasker(bool fixtures)
        {
            this.fixtures = fixtures; callback = OnWindowEvent;
            hook = SetWinEventHook(0x8000, 0x800c, IntPtr.Zero, callback, 0, 0, 2);
            if (hook == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            Scan();
        }
        internal void Scan()
        {
            if (disposed) return;
            EnumWindows(delegate(IntPtr h, IntPtr unused) { TryMask(h); return true; }, IntPtr.Zero);
            var gone = new List<IntPtr>();
            foreach (var item in masks) if (!IsWindow(item.Key)) { if (item.Value.Region != IntPtr.Zero) DeleteObject(item.Value.Region); gone.Add(item.Key); }
            foreach (var key in gone) masks.Remove(key);
            Report();
        }
        private void OnWindowEvent(IntPtr hHook, uint ev, IntPtr hwnd, int obj, int child, uint thread, uint time)
        {
            if (disposed || hwnd == IntPtr.Zero || obj != 0 || child != 0 || ev == 0x8001 || ev == 0x8003) return;
            TryMask(hwnd);
        }
        private bool IsToast(IntPtr h, out uint pid)
        {
            pid = 0;
            if (!IsWindow(h) || GetAncestor(h, 2) != h) return false;
            var cls = new StringBuilder(256); var title = new StringBuilder(256);
            GetClassName(h, cls, cls.Capacity); GetWindowText(h, title, title.Capacity);
            string c = cls.ToString(), name = title.ToString();
            bool fixture = fixtures && name == "WinIsland Native Toast Fixture" && c.StartsWith("WindowsForms10.");
            bool native = (c == "Windows.UI.Core.CoreWindow" || c == "Windows.UI.Composition.DesktopWindowContentBridge") &&
                (name.Equals("New notification", StringComparison.OrdinalIgnoreCase) || name == "新通知" || name == "新的通知");
            native |= c == "ToastWnd" || c == "ToastWindowClass" || c == "Windows.UI.Notifications.ToastWindow";
            if (!fixture && !native) return false;
            GetWindowThreadProcessId(h, out pid);
            try
            {
                using (var process = Process.GetProcessById((int)pid))
                {
                    string exe = process.MainModule.FileName;
                    if (fixture) return string.Equals(exe, Process.GetCurrentProcess().MainModule.FileName, StringComparison.OrdinalIgnoreCase);
                    string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows) + Path.DirectorySeparatorChar;
                    if (!exe.StartsWith(windows, StringComparison.OrdinalIgnoreCase)) return false;
                    string n = process.ProcessName;
                    return n == "ShellExperienceHost" || n == "ShellHost" || n == "explorer";
                }
            }
            catch { return false; }
        }
        private void TryMask(IntPtr h)
        {
            if (disposed) return;
            try
            {
                uint pid;
                if (!IsToast(h, out pid)) return;
                Mask previous;
                if (masks.TryGetValue(h, out previous))
                {
                    if (previous.Process == pid) { EnsureMask(h, previous); return; }
                    if (previous.Region != IntPtr.Zero) DeleteObject(previous.Region);
                    masks.Remove(h);
                }
                int cloaked;
                if (DwmGetWindowAttribute(h, 14, out cloaked, 4) == 0 && (cloaked & 1) != 0) return;
                var mask = new Mask { Process = pid };
                int yes = 1;
                if (DwmSetWindowAttribute(h, 13, ref yes, 4) == 0) mask.Cloak = true;
                else
                {
                    IntPtr original = CreateRectRgn(0, 0, 0, 0);
                    int kind = GetWindowRgn(h, original);
                    if (kind == 1) { DeleteObject(original); return; }
                    if (kind == 0) { DeleteObject(original); original = IntPtr.Zero; }
                    mask.Region = original;
                    IntPtr empty = CreateRectRgn(0, 0, 0, 0);
                    if (SetWindowRgn(h, empty, true) == 0)
                    { DeleteObject(empty); if (original != IntPtr.Zero) DeleteObject(original); return; }
                }
                masks.Add(h, mask); Report();
            }
            catch (Exception ex) { Log.Error("Native toast mask", ex); }
        }
        private static void EnsureMask(IntPtr h, Mask mask)
        {
            if (mask.Cloak) { int yes = 1; DwmSetWindowAttribute(h, 13, ref yes, 4); return; }
            IntPtr probe = CreateRectRgn(0, 0, 0, 0);
            int kind = GetWindowRgn(h, probe); DeleteObject(probe);
            if (kind == 1) return;
            IntPtr empty = CreateRectRgn(0, 0, 0, 0);
            if (SetWindowRgn(h, empty, true) == 0) DeleteObject(empty);
        }
        private void Report() { try { Console.Out.WriteLine("MASKS " + masks.Count); Console.Out.Flush(); } catch { } }
        public void Dispose()
        {
            if (disposed) return; disposed = true;
            if (hook != IntPtr.Zero) { UnhookWinEvent(hook); hook = IntPtr.Zero; }
            foreach (var pair in masks)
            {
                var mask = pair.Value; uint pid; GetWindowThreadProcessId(pair.Key, out pid);
                if (IsWindow(pair.Key) && pid == mask.Process)
                {
                    if (mask.Cloak) { int no = 0; DwmSetWindowAttribute(pair.Key, 13, ref no, 4); }
                    else if (SetWindowRgn(pair.Key, mask.Region, true) != 0) mask.Region = IntPtr.Zero;
                }
                if (mask.Region != IntPtr.Zero) DeleteObject(mask.Region);
            }
            masks.Clear(); Report();
        }
        private delegate void WinEvent(IntPtr hook, uint ev, IntPtr h, int obj, int child, uint thread, uint time);
        private delegate bool EnumCallback(IntPtr h, IntPtr p);
        [DllImport("user32.dll")] private static extern IntPtr SetWinEventHook(uint min, uint max, IntPtr module, WinEvent proc, uint process, uint thread, uint flags);
        [DllImport("user32.dll")] private static extern bool UnhookWinEvent(IntPtr hook);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr p);
        [DllImport("user32.dll")] private static extern bool IsWindow(IntPtr h);
        [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr h, uint flags);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr h, out uint process);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr h, StringBuilder name, int count);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr h, StringBuilder name, int count);
        [DllImport("dwmapi.dll")] internal static extern int DwmSetWindowAttribute(IntPtr h, int attribute, ref int value, int size);
        [DllImport("dwmapi.dll")] internal static extern int DwmGetWindowAttribute(IntPtr h, int attribute, out int value, int size);
        [DllImport("gdi32.dll")] internal static extern IntPtr CreateRectRgn(int l, int t, int r, int b);
        [DllImport("gdi32.dll")] internal static extern bool DeleteObject(IntPtr h);
        [DllImport("user32.dll")] internal static extern int GetWindowRgn(IntPtr h, IntPtr region);
        [DllImport("user32.dll")] private static extern int SetWindowRgn(IntPtr h, IntPtr region, bool redraw);

        internal static int RunHelper(int parentId, long parentTicks, bool fixtures)
        {
            NativeToastMasker masker = null;
            try
            {
                using (var parent = Process.GetProcessById(parentId))
                {
                    if (parent.StartTime.ToUniversalTime().Ticks != parentTicks) return 0;
                    int ended = 0;
                    var reader = new Thread(delegate()
                    {
                        try { while (Console.In.ReadLine() != null) { } } catch { }
                        Interlocked.Exchange(ref ended, 1);
                    }) { IsBackground = true };
                    reader.Start();
                    masker = new NativeToastMasker(fixtures);
                    using (var timer = new Forms.Timer { Interval = 100 })
                    {
                        int ticks = 0;
                        timer.Tick += delegate
                        {
                            if (Volatile.Read(ref ended) != 0 || parent.HasExited) { masker.Dispose(); Forms.Application.ExitThread(); return; }
                            if (++ticks % 5 == 0) masker.Scan();
                        };
                        timer.Start(); Forms.Application.Run();
                    }
                }
                return 0;
            }
            catch (Exception ex) { Log.Error("Native helper", ex); return 1; }
            finally { if (masker != null) masker.Dispose(); }
        }
        internal static int RunFixture(string output)
        {
            using (var form = new Forms.Form { Text = "WinIsland Native Toast Fixture", Width = 330, Height = 110,
                StartPosition = Forms.FormStartPosition.Manual, Location = new System.Drawing.Point(32, 80), ShowInTaskbar = false,
                FormBorderStyle = Forms.FormBorderStyle.FixedToolWindow, BackColor = System.Drawing.Color.White })
            using (var timer = new Forms.Timer { Interval = 250 })
            {
                form.Controls.Add(new Forms.Label { Text = "WinIsland 原生通知遮蔽测试", Dock = Forms.DockStyle.Fill, TextAlign = System.Drawing.ContentAlignment.MiddleCenter });
                form.Shown += delegate { File.WriteAllText(output, form.Handle.ToInt64().ToString()); };
                var started = Stopwatch.StartNew();
                timer.Tick += delegate { if (File.Exists(output + ".close") || started.Elapsed.TotalSeconds > 180) form.Close(); };
                timer.Start(); Forms.Application.Run(form);
            }
            return 0;
        }
    }
}
