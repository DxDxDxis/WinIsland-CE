using System;
using System.Runtime.InteropServices;
using System.Windows.Media;

namespace WinIsland
{
    // Only Windows/.NET Framework/WPF are required. Optional capabilities are
    // detected locally; downloading binaries at runtime would be unsafe and is
    // unnecessary for this code-only application.
    internal static class Compatibility
    {
        internal static readonly int RenderTier;
        internal static readonly bool Lightweight;
        internal static int ConfiguredFrameRate;
        internal static int EffectiveFrameRate { get { return ConfiguredFrameRate == 0 && Lightweight ? 60 : ConfiguredFrameRate; } }
        internal static double FrameIntervalMs { get { return EffectiveFrameRate == 0 ? 0 : 1000.0 / EffectiveFrameRate; } }
        internal static string Diagnostic { get { return "os=" + Environment.OSVersion.Version + ",tier=" + RenderTier + ",cpu=" + Environment.ProcessorCount + ",mode=" + (Lightweight ? "light" : "full") + ",frame=" + FrameIntervalMs; } }
        static Compatibility()
        {
            try { RenderTier = RenderCapability.Tier >> 16; } catch { RenderTier = 0; }
            long ram = 0;
            try { var m = new MemoryStatus(); m.Length = (uint)Marshal.SizeOf(typeof(MemoryStatus)); if (GlobalMemoryStatusEx(ref m)) ram = (long)m.TotalPhysical; } catch { }
            int cpu = Math.Max(1, Environment.ProcessorCount);
            bool forced = string.Equals(Environment.GetEnvironmentVariable("WINISLAND_LIGHTWEIGHT"), "1", StringComparison.Ordinal);
            Lightweight = forced || RenderTier == 0 || (ram > 0 && ram < 4L * 1024 * 1024 * 1024) || cpu <= 2;
        }
        internal static void SetTargetFrameRate(int fps)
        {
            if (fps != 0 && (fps < 30 || fps > 240)) return;
            ConfiguredFrameRate = fps;
        }
        [StructLayout(LayoutKind.Sequential)] private struct MemoryStatus { internal uint Length, MemoryLoad; internal ulong TotalPhysical, AvailablePhysical, TotalPage, AvailablePage, TotalVirtual, AvailableVirtual, AvailableExtended; }
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);
    }

    // Each visual has its own clock. Use the compositor timestamp: WPF may raise
    // Rendering repeatedly for the same frame when layout changes during a callback.
    internal sealed class FramePacer
    {
        private long lastEvent = -1, lastAccepted = -1;
        private double nextDue;
        private int previousRate = -1;
        internal long Frames { get; private set; }
        internal double DeltaSeconds { get; private set; }
        internal bool ShouldRender(long ticks, int fps)
        {
            if (ticks == lastEvent) return false;
            if (fps != previousRate || ticks < lastEvent) { nextDue = ticks; previousRate = fps; }
            lastEvent = ticks;
            if (fps > 0)
            {
                if (ticks + 1 < nextDue) return false;
                double interval = (double)TimeSpan.TicksPerSecond / fps;
                // Retain the fractional deadline across 59.94/120/144/240 Hz events.
                // Skip missed slots after a stall; never queue catch-up frames.
                nextDue += (Math.Floor(Math.Max(0, ticks - nextDue) / interval) + 1) * interval;
            }
            DeltaSeconds = lastAccepted < 0 ? 1.0 / 60 : Math.Max(.001, Math.Min(.1, (ticks - lastAccepted) / (double)TimeSpan.TicksPerSecond));
            lastAccepted = ticks; Frames++; return true;
        }
    }
}
