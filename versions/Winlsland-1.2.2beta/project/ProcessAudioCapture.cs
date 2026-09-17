using System;
using System.Runtime.InteropServices;
using System.Threading;

namespace WinIsland
{
    // Windows 10 build 20348+/Windows 11 process-loopback. PCM is only inspected
    // in memory and is never recorded. Other applications aren't part of the stream.
    public sealed class ProcessAudioCapture : IDisposable
    {
        private AudioClient client;
        private CaptureClient capture;
        internal ProcessAudioCapture(uint pid)
        {
            IntPtr blob = Marshal.AllocHGlobal(12);
            ActivateOperation operation = null;
            var handler = new Completion();
            string stage = "activate";
            try
            {
                Marshal.WriteInt32(blob, 0, 1); // AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK
                Marshal.WriteInt32(blob, 4, unchecked((int)pid)); Marshal.WriteInt32(blob, 8, 0);
                var parameters = new PropVariant { Type = 65, Size = 12, Data = blob };
                Guid iid = typeof(AudioClient).GUID;
                Marshal.ThrowExceptionForHR(ActivateAudioInterfaceAsync("VAD\\Process_Loopback", ref iid, ref parameters, handler, out operation));
                if (!handler.Done.WaitOne(2500)) { handler.Abandon(); throw new TimeoutException("Process loopback activation"); }
                stage = "activation result";
                Marshal.ThrowExceptionForHR(handler.Result);
                client = (AudioClient)handler.Audio; handler.Audio = null;
                var format = new WaveFormat { Tag = 1, Channels = 2, Samples = 44100, Bytes = 176400, Align = 4, Bits = 16 };
                stage = "initialize";
                Marshal.ThrowExceptionForHR(client.Initialize(0, 0x20000, 2000000, 0, ref format, IntPtr.Zero));
                object result; iid = typeof(CaptureClient).GUID;
                Marshal.ThrowExceptionForHR(client.GetService(ref iid, out result)); capture = (CaptureClient)result;
                Marshal.ThrowExceptionForHR(client.Start());
            }
            catch (Exception ex) { handler.Abandon(); ex.Data["AudioStage"] = stage; Dispose(); throw; }
            finally { handler.ReleaseWait(operation != null); Marshal.FreeHGlobal(blob); if (operation != null) Marshal.ReleaseComObject(operation); }
        }
        internal float ReadPeak()
        {
            long started=Performance.Begin();try {
            float peak = 0; uint frames;
            for (int batch = 0; batch < 128; batch++)
            {
                Marshal.ThrowExceptionForHR(capture.GetNextPacketSize(out frames));
                if (frames == 0) break;
                IntPtr bytes; uint flags; ulong device, time;
                Marshal.ThrowExceptionForHR(capture.GetBuffer(out bytes, out frames, out flags, out device, out time));
                try
                {
                    if ((flags & 2) == 0)
                        for (int i = 0; i < frames * 2; i++) peak = Math.Max(peak, Math.Abs((int)Marshal.ReadInt16(bytes, i * 2)) / 32768f);
                }
                finally { Marshal.ThrowExceptionForHR(capture.ReleaseBuffer(frames)); }
            }
            // The PCM converter can emit one-LSB dither at digital silence (-90 dBFS).
            // A -80 dBFS meter floor prevents that noise from reporting audible activity.
            return peak < .0001f ? 0 : peak;
            } finally {Performance.End(PerfPart.AudioRead,started);}
        }
        public void Dispose()
        {
            if (client != null) try { client.Stop(); } catch { }
            if (capture != null) { Marshal.ReleaseComObject(capture); capture = null; }
            if (client != null) { Marshal.ReleaseComObject(client); client = null; }
        }
        [StructLayout(LayoutKind.Explicit, Size = 24)] private struct PropVariant {
            [FieldOffset(0)] internal ushort Type; [FieldOffset(8)] internal uint Size; [FieldOffset(16)] internal IntPtr Data;
        }
        [StructLayout(LayoutKind.Sequential, Pack = 2)] private struct WaveFormat {
            internal ushort Tag, Channels; internal uint Samples, Bytes; internal ushort Align, Bits, Extra;
        }
        [DllImport("Mmdevapi.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
        private static extern int ActivateAudioInterfaceAsync(string device, ref Guid iid, ref PropVariant parameters, CompletionHandler completion, out ActivateOperation operation);
        [ComImport, Guid("72A22D78-CDE4-431D-B8CC-843A71199B6D"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface ActivateOperation { [PreserveSig] int GetActivateResult(out int result, [MarshalAs(UnmanagedType.IUnknown)] out object audio); }
        [ComVisible(true), Guid("41D949AB-9862-444A-80F6-C261334DA5EB"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        public interface CompletionHandler { [PreserveSig] int ActivateCompleted([MarshalAs(UnmanagedType.IUnknown)] object operation); }
        [ComVisible(true), Guid("94EA2B94-E9CC-49E0-C0FF-EE64CA8F5B90"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)] public interface Agile { }
        [ComVisible(true), ClassInterface(ClassInterfaceType.None)] public sealed class Completion : CompletionHandler, Agile {
            internal readonly ManualResetEvent Done = new ManualResetEvent(false);
            internal int Result; internal object Audio;
            private bool abandoned, finished, waitReleased;
            internal void Abandon() { lock (this) { abandoned = true; if (Audio != null) { Marshal.ReleaseComObject(Audio); Audio = null; } } }
            internal void ReleaseWait(bool started) { lock (this) { waitReleased = true; if (finished || !started) Done.Dispose(); } }
            public int ActivateCompleted(object value) {
                lock (this) {
                    try { int hr = ((ActivateOperation)value).GetActivateResult(out Result, out Audio); if (hr < 0) Result = hr; }
                    catch (Exception ex) { Result = ex.HResult; }
                    if (abandoned && Audio != null) { Marshal.ReleaseComObject(Audio); Audio = null; }
                    Done.Set();
                    finished = true; if (waitReleased) Done.Dispose();
                }
                return 0;
            }
        }
        [ComImport, Guid("1CB9AD4C-DBFA-4C32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface AudioClient {
            [PreserveSig] int Initialize(int mode, uint flags, long duration, long period, ref WaveFormat format, IntPtr session);
            [PreserveSig] int GetBufferSize(out uint frames); [PreserveSig] int GetStreamLatency(out long latency);
            [PreserveSig] int GetCurrentPadding(out uint frames); [PreserveSig] int IsFormatSupported(int mode, IntPtr format, out IntPtr nearest);
            [PreserveSig] int GetMixFormat(out IntPtr format); [PreserveSig] int GetDevicePeriod(out long normal, out long minimum);
            [PreserveSig] int Start(); [PreserveSig] int Stop(); [PreserveSig] int Reset(); [PreserveSig] int SetEventHandle(IntPtr handle);
            [PreserveSig] int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
        }
        [ComImport, Guid("C8ADBD64-E71E-48A0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface CaptureClient {
            [PreserveSig] int GetBuffer(out IntPtr data, out uint frames, out uint flags, out ulong devicePosition, out ulong qpcPosition);
            [PreserveSig] int ReleaseBuffer(uint frames); [PreserveSig] int GetNextPacketSize(out uint frames);
        }
    }
}
