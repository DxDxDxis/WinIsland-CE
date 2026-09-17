using System;
using System.Runtime.InteropServices;

namespace WinIsland
{
    internal static class Native
    {
        internal const int GwlExStyle = -20;
        internal const int Transparent = 0x20, ToolWindow = 0x80, NoActivate = 0x08000000;
        internal const int WmMouseActivate = 0x21, WmNcHitTest = 0x84, WmDpiChanged = 0x02E0;
        [DllImport("user32.dll")] internal static extern int GetWindowLong(IntPtr h, int index);
        [DllImport("user32.dll")] internal static extern int SetWindowLong(IntPtr h, int index, int value);
        [DllImport("user32.dll")] internal static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int hgt, uint flags);
        [DllImport("user32.dll")] internal static extern IntPtr MonitorFromPoint(Point point, uint flags);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] internal static extern bool GetMonitorInfo(IntPtr h, ref MonitorInfo info);
        [DllImport("shcore.dll")] internal static extern int GetDpiForMonitor(IntPtr h, int type, out uint x, out uint y);
        [DllImport("shell32.dll", CharSet = CharSet.Unicode)] internal static extern int SetCurrentProcessExplicitAppUserModelID(string id);
        [DllImport("wtsapi32.dll")] internal static extern bool WTSRegisterSessionNotification(IntPtr h, uint flags);
        [DllImport("wtsapi32.dll")] internal static extern bool WTSUnRegisterSessionNotification(IntPtr h);
        [StructLayout(LayoutKind.Sequential)] internal struct Point { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)] internal struct Rect { public int Left, Top, Right, Bottom; }
        [StructLayout(LayoutKind.Sequential)] internal struct MonitorInfo
        { public int Size; public Rect Monitor, Work; public uint Flags; }
    }
}
