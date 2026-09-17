using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Windows.Threading;

namespace WinIsland
{
    internal enum PerfPart { UiMusic, NoticeMeasure, ShellFrame, MusicFrame, LyricLoad, LyricWidth, MediaPoll, AudioScan, AudioRead, NotificationPoll, DispatchWait, HeartbeatLate, ShellGap, MusicGap }
    internal static class Performance
    {
        private sealed class Counter
        {
            internal readonly long[] buckets = new long[2002];
            internal long count, ticks, max;
            internal void Add(long value)
            {
                Interlocked.Increment(ref count); Interlocked.Add(ref ticks, value);
                long previous; do { previous = Interlocked.Read(ref max); if (previous >= value) break; } while (Interlocked.CompareExchange(ref max, value, previous) != previous);
                int bucket = (int)Math.Min(2001, Math.Ceiling(value * 1000.0 / Stopwatch.Frequency));
                Interlocked.Increment(ref buckets[Math.Max(0, bucket)]);
            }
            internal string Read()
            {
                long n=Interlocked.Read(ref count), sum=Interlocked.Read(ref ticks), worst=Interlocked.Read(ref max), seen=0; int p95=0,p99=0;
                for (int i=0; i<buckets.Length; i++) { seen+=Interlocked.Read(ref buckets[i]); if (seen < n*.95) p95=i+1; if (seen < n*.99) p99=i+1; }
                return n+","+F(n==0?0:sum*1000.0/Stopwatch.Frequency/n)+","+p95+","+p99+","+F(worst*1000.0/Stopwatch.Frequency);
            }
        }
        private static readonly Counter[] counters = MakeCounters();
        private static readonly Stopwatch lifetime=Stopwatch.StartNew();
        private static Timer writer;
        private static DispatcherTimer heartbeat;
        private static long beat;
        private static string directory;
        private static int writing;
        internal static volatile bool Enabled;
        internal static string Phase="startup";
        private static Counter[] MakeCounters() { var c=new Counter[Enum.GetValues(typeof(PerfPart)).Length]; for(int i=0;i<c.Length;i++) c[i]=new Counter(); return c; }
        internal static long Begin() { return Enabled ? Stopwatch.GetTimestamp() : 0; }
        internal static void End(PerfPart part,long start) { if(start!=0) counters[(int)part].Add(Math.Max(0,Stopwatch.GetTimestamp()-start)); }
        internal static void Gap(PerfPart part,ref long previous) { if(!Enabled)return; long now=Stopwatch.GetTimestamp(); if(previous!=0)counters[(int)part].Add(now-previous);previous=now; }
        internal static void Start(string path,Dispatcher dispatcher)
        {
            if(Enabled)return;
            directory=path; Directory.CreateDirectory(path); Enabled=true;
            File.WriteAllText(Path.Combine(path,"environment.txt"),"Version="+AppVersion.Display+"\nOS="+Environment.OSVersion.Version+"\nCLR="+Environment.Version+"\nCPU="+Environment.ProcessorCount+"\nRender="+Compatibility.Diagnostic+"\nMetrics are callback/dispatcher timings, not GPU presentation timings. No content is recorded.\n");
            heartbeat=new DispatcherTimer(DispatcherPriority.Background,dispatcher) { Interval=TimeSpan.FromMilliseconds(100) };
            beat=Stopwatch.GetTimestamp(); heartbeat.Tick+=delegate {long now=Stopwatch.GetTimestamp();counters[(int)PerfPart.HeartbeatLate].Add(Math.Max(0,now-beat-Stopwatch.Frequency/10));beat=now;}; heartbeat.Start();
            writer=new Timer(delegate { Flush(); },null,1000,5000);
        }
        private static string F(double n) {return n.ToString("0.###",CultureInfo.InvariantCulture);}
        internal static void Flush()
        {
            if(!Enabled || Interlocked.Exchange(ref writing,1)!=0)return;
            try {
                using(var p=Process.GetCurrentProcess()) {
                    var b=new StringBuilder();b.AppendLine("phase,elapsed_s,cpu_s,working_mb,private_mb,threads,handles,gc0,gc1,gc2,target_fps");
                    b.AppendLine(Phase+","+F(lifetime.Elapsed.TotalSeconds)+","+F(p.TotalProcessorTime.TotalSeconds)+","+F(p.WorkingSet64/1048576.0)+","+F(p.PrivateMemorySize64/1048576.0)+","+p.Threads.Count+","+p.HandleCount+","+GC.CollectionCount(0)+","+GC.CollectionCount(1)+","+GC.CollectionCount(2)+","+Compatibility.EffectiveFrameRate);
                    b.AppendLine("part,count,mean_ms,p95_ms_bucket,p99_ms_bucket,max_ms");
                    foreach(PerfPart part in Enum.GetValues(typeof(PerfPart))) b.AppendLine(part+","+counters[(int)part].Read());
                    string text=b.ToString();File.WriteAllText(Path.Combine(directory,"latest.csv"),text);
                    string log=Path.Combine(directory,"performance.log");if(File.Exists(log)&&new FileInfo(log).Length>2*1024*1024)File.Copy(log,Path.Combine(directory,"performance.previous.log"),true);
                    if(File.Exists(log)&&new FileInfo(log).Length>2*1024*1024)File.WriteAllText(log,text);else File.AppendAllText(log,text+"\n");
                }
            } catch(IOException){} catch(UnauthorizedAccessException){} finally {Interlocked.Exchange(ref writing,0);}
        }
        internal static void Stop() { if(heartbeat!=null)heartbeat.Stop(); if(writer!=null)writer.Dispose(); Flush(); Enabled=false; }
    }
}
