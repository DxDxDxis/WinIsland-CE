using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;

namespace WinIsland
{
    internal sealed class AudioFrame
    {
        internal string SourceId;
        internal bool Available, Muted;
        // Six consecutive real session-peak measurements, oldest to newest (not frequency bins).
        internal float[] Peaks = new float[6];
    }

    internal sealed class AudioLevels : IDisposable
    {
        private sealed class Session
        {
            internal AudioSessionControl Control;
            internal SimpleAudioVolume Volume;
            internal EndpointVolume Endpoint;
            internal uint Pid;
        }
        private readonly object gate = new object();
        private readonly Thread thread;
        private readonly ManualResetEvent stop = new ManualResetEvent(false);
        private readonly bool fixtures;
        private AudioFrame frame = new AudioFrame();
        private string source, platform;
        private volatile bool disposed;
        internal string Diagnostic = "waiting";
        internal AudioLevels(bool fixtures)
        {
            this.fixtures = fixtures;
            thread = new Thread(Run) { IsBackground = true, Name = "WinIsland process audio capture" };
            thread.SetApartmentState(ApartmentState.MTA); thread.Start();
        }
        internal void Select(MusicState value)
        {
            lock (gate)
            {
                string id = value == null ? null : value.SourceId;
                if (id == source) return;
                source = id; platform = value == null ? null : value.Platform;
                frame = new AudioFrame { SourceId = id };
            }
        }
        internal AudioFrame Snapshot { get { lock (gate) return frame; } }
        private void Run()
        {
            var sessions = new List<Session>();
            var endpoints = new List<EndpointVolume>();
            var captures = new Dictionary<uint, ProcessAudioCapture>();
            var retry = new Dictionary<uint, DateTime>();
            string selected = null; DateTime nextScan = DateTime.MinValue;
            var samples = new float[6];
            try
            {
                while (!stop.WaitOne(60))
                {
                    string id, app; lock (gate) { id = source; app = platform; }
                    if (id != selected || DateTime.UtcNow >= nextScan)
                    {
                        if (id != selected) { Array.Clear(samples, 0, samples.Length); foreach (var c in captures.Values) c.Dispose(); captures.Clear(); retry.Clear(); }
                        Clear(sessions, endpoints); selected = id; nextScan = DateTime.UtcNow.AddSeconds(1);
                        if (id != null) { long started=Performance.Begin(); try { Scan(app, id, sessions, endpoints); } catch (Exception ex) { Diagnostic = "scan " + ex.GetType().Name + " " + ex.HResult.ToString("X8"); Clear(sessions, endpoints); } finally {Performance.End(PerfPart.AudioScan,started);} }
                        var live = new HashSet<uint>(sessions.Select(s => s.Pid));
                        foreach (uint gone in captures.Keys.Where(pid => !live.Contains(pid)).ToArray()) { captures[gone].Dispose(); captures.Remove(gone); }
                        foreach (uint gone in retry.Keys.Where(pid => !live.Contains(pid)).ToArray()) retry.Remove(gone);
                    }
                    float peak = 0; bool available = false, allMuted = sessions.Count > 0;
                    var measured = new Dictionary<uint, float>();
                    foreach (var session in sessions)
                    {
                        try
                        {
                            float value, volume, output; bool mute, endpointMute; int state;
                            Marshal.ThrowExceptionForHR(session.Control.GetState(out state));
                            if (state == 2) continue;
                            ProcessAudioCapture capture;
                            if (!captures.TryGetValue(session.Pid, out capture))
                            {
                                DateTime after;
                                if (retry.TryGetValue(session.Pid, out after) && DateTime.UtcNow < after) continue;
                                try { captures[session.Pid] = capture = new ProcessAudioCapture(session.Pid); retry.Remove(session.Pid); }
                                catch (Exception ex) { Diagnostic = "process loopback " + ex.Data["AudioStage"] + " " + ex.GetType().Name + " " + ex.HResult.ToString("X8"); retry[session.Pid] = DateTime.UtcNow.AddSeconds(10); continue; }
                            }
                            if (!measured.TryGetValue(session.Pid, out value)) measured[session.Pid] = value = capture.ReadPeak();
                            Marshal.ThrowExceptionForHR(session.Volume.GetMute(out mute));
                            Marshal.ThrowExceptionForHR(session.Volume.GetMasterVolume(out volume));
                            Marshal.ThrowExceptionForHR(session.Endpoint.GetMute(out endpointMute));
                            Marshal.ThrowExceptionForHR(session.Endpoint.GetMasterVolumeLevelScalar(out output));
                            available = true;
                            bool silent = mute || endpointMute || volume <= 0 || output <= 0;
                            if (!silent) allMuted = false;
                            // Process-loopback PCM includes the application's stream gain.
                            // Endpoint/session mute still gates the reading before presentation.
                            if (!silent && state == 1) peak = Math.Max(peak, Math.Max(0, Math.Min(1, value * output)));
                        }
                        catch (Exception ex) { Diagnostic = "read " + ex.GetType().Name + " " + ex.HResult.ToString("X8"); ProcessAudioCapture c; if (captures.TryGetValue(session.Pid, out c)) { c.Dispose(); captures.Remove(session.Pid); } nextScan = DateTime.MinValue; }
                    }
                    Array.Copy(samples, 1, samples, 0, samples.Length - 1); samples[samples.Length - 1] = peak;
                    lock (gate)
                        if (source == selected) frame = new AudioFrame { SourceId = selected, Available = available,
                            Muted = available && allMuted, Peaks = (float[])samples.Clone() };
                }
            }
            finally { foreach (var c in captures.Values) c.Dispose(); Clear(sessions, endpoints); stop.Dispose(); }
        }
        private void Scan(string app, string id, List<Session> sessions, List<EndpointVolume> endpoints)
        {
            string processName = app == "网易云音乐" ? "cloudmusic" : app == "QQ 音乐" ? "QQMusic" : app == "汽水音乐" ? "SodaMusic" : null;
            if (fixtures && id == "WinIsland.MediaFixture") processName = Process.GetCurrentProcess().ProcessName;
            if (processName == null) return;
            var pids = new HashSet<uint>();
            foreach (var process in Process.GetProcessesByName(processName))
                using (process) pids.Add((uint)process.Id);
            DeviceEnumerator enumerator = null; DeviceCollection devices = null;
            try
            {
                enumerator = (DeviceEnumerator)new MMDeviceEnumerator();
                Marshal.ThrowExceptionForHR(enumerator.EnumAudioEndpoints(0, 1, out devices));
                uint count; Marshal.ThrowExceptionForHR(devices.GetCount(out count));
                Diagnostic = "devices=" + count + " pids=" + pids.Count;
                for (uint i = 0; i < count; i++)
                {
                    Device device = null; SessionManager manager = null; SessionEnumerator list = null;
                    EndpointVolume endpoint = null;
                    try
                    {
                        Marshal.ThrowExceptionForHR(devices.Item(i, out device));
                        object result; Guid iid = typeof(SessionManager).GUID;
                        Marshal.ThrowExceptionForHR(device.Activate(ref iid, 23, IntPtr.Zero, out result)); manager = (SessionManager)result;
                        iid = typeof(EndpointVolume).GUID;
                        Marshal.ThrowExceptionForHR(device.Activate(ref iid, 23, IntPtr.Zero, out result)); endpoint = (EndpointVolume)result;
                        endpoints.Add(endpoint);
                        Marshal.ThrowExceptionForHR(manager.GetSessionEnumerator(out list));
                        int length; Marshal.ThrowExceptionForHR(list.GetCount(out length));
                        Diagnostic += " sessions=" + length;
                        for (int j = 0; j < length; j++)
                        {
                            object raw = null;
                            try
                            {
                                Marshal.ThrowExceptionForHR(list.GetSession(j, out raw));
                                var control = (AudioSessionControl)raw;
                                uint pid; Marshal.ThrowExceptionForHR(control.GetProcessId(out pid));
                                if (!pids.Contains(pid)) continue;
                                Diagnostic += " matched";
                                sessions.Add(new Session { Control = control,
                                    Volume = (SimpleAudioVolume)control, Endpoint = endpoint, Pid = pid });
                                raw = null;
                            }
                            finally { Release(raw); }
                        }
                    }
                    catch (Exception ex) { Diagnostic += " endpoint=" + ex.GetType().Name + ":" + ex.HResult.ToString("X8"); }
                    finally { Release(list); Release(manager); Release(device); }
                }
            }
            finally { Release(devices); Release(enumerator); }
        }
        private static void Clear(List<Session> sessions, List<EndpointVolume> endpoints)
        { foreach (var s in sessions) Release(s.Control); sessions.Clear(); foreach (var e in endpoints) Release(e); endpoints.Clear(); }
        private static void Release(object value) { if (value != null && Marshal.IsComObject(value)) Marshal.ReleaseComObject(value); }
        public void Dispose() { if (disposed) return; disposed = true; stop.Set(); /* Worker owns COM cleanup. */ }

        [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] private class MMDeviceEnumerator { }
        [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface DeviceEnumerator {
            [PreserveSig] int EnumAudioEndpoints(int flow, uint state, out DeviceCollection devices);
        }
        [ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface DeviceCollection {
            [PreserveSig] int GetCount(out uint count); [PreserveSig] int Item(uint index, out Device device);
        }
        [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface Device {
            [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr parameters, [MarshalAs(UnmanagedType.IUnknown)] out object result);
        }
        [ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface SessionManager {
            [PreserveSig] int GetAudioSessionControl(ref Guid session, uint flags, out IntPtr control);
            [PreserveSig] int GetSimpleAudioVolume(ref Guid session, uint flags, out IntPtr volume);
            [PreserveSig] int GetSessionEnumerator(out SessionEnumerator sessions);
        }
        [ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface SessionEnumerator {
            [PreserveSig] int GetCount(out int count); [PreserveSig] int GetSession(int index, [MarshalAs(UnmanagedType.IUnknown)] out object session);
        }
        [ComImport, Guid("BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface AudioSessionControl {
            [PreserveSig] int GetState(out int state);
            [PreserveSig] int GetDisplayName([MarshalAs(UnmanagedType.LPWStr)] out string name);
            [PreserveSig] int SetDisplayName([MarshalAs(UnmanagedType.LPWStr)] string name, ref Guid context);
            [PreserveSig] int GetIconPath([MarshalAs(UnmanagedType.LPWStr)] out string path);
            [PreserveSig] int SetIconPath([MarshalAs(UnmanagedType.LPWStr)] string path, ref Guid context);
            [PreserveSig] int GetGroupingParam(out Guid grouping); [PreserveSig] int SetGroupingParam(ref Guid grouping, ref Guid context);
            [PreserveSig] int RegisterAudioSessionNotification(IntPtr events); [PreserveSig] int UnregisterAudioSessionNotification(IntPtr events);
            [PreserveSig] int GetSessionIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
            [PreserveSig] int GetSessionInstanceIdentifier([MarshalAs(UnmanagedType.LPWStr)] out string id);
            [PreserveSig] int GetProcessId(out uint pid);
        }
        [ComImport, Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface SimpleAudioVolume {
            [PreserveSig] int SetMasterVolume(float level, ref Guid context); [PreserveSig] int GetMasterVolume(out float level);
            [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool mute, ref Guid context); [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool mute);
        }
        [ComImport, Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface EndpointVolume {
            [PreserveSig] int RegisterControlChangeNotify(IntPtr callback); [PreserveSig] int UnregisterControlChangeNotify(IntPtr callback);
            [PreserveSig] int GetChannelCount(out uint count); [PreserveSig] int SetMasterVolumeLevel(float value, ref Guid context);
            [PreserveSig] int SetMasterVolumeLevelScalar(float value, ref Guid context); [PreserveSig] int GetMasterVolumeLevel(out float value);
            [PreserveSig] int GetMasterVolumeLevelScalar(out float value); [PreserveSig] int SetChannelVolumeLevel(uint channel, float value, ref Guid context);
            [PreserveSig] int SetChannelVolumeLevelScalar(uint channel, float value, ref Guid context); [PreserveSig] int GetChannelVolumeLevel(uint channel, out float value);
            [PreserveSig] int GetChannelVolumeLevelScalar(uint channel, out float value); [PreserveSig] int SetMute([MarshalAs(UnmanagedType.Bool)] bool value, ref Guid context);
            [PreserveSig] int GetMute([MarshalAs(UnmanagedType.Bool)] out bool value);
        }
    }
}
