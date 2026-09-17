using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Xml;
using System.Xml.Linq;

namespace WinIsland
{
    internal sealed class Notice
    {
        internal long Id;
        internal string Title, Body, Fingerprint;
        internal long HandlerId, ArrivalTime;
        internal int Generation;
        internal string Key { get { return HandlerId + ":" + Id + ":" + ArrivalTime; } }
    }

    internal static class ToastParser
    {
        internal static Notice Parse(long id, byte[] payload)
        {
            if (payload == null || payload.Length == 0 || payload.Length > 1024 * 1024) return null;
            Encoding encoding = Encoding.UTF8;
            if (payload.Length > 1 && ((payload[0] == 0xff && payload[1] == 0xfe) || payload[1] == 0))
                encoding = Encoding.Unicode;
            else if (payload.Length > 1 && ((payload[0] == 0xfe && payload[1] == 0xff) || payload[0] == 0))
                encoding = Encoding.BigEndianUnicode;
            try
            {
                string xml = encoding.GetString(payload).Trim('\0', '\uFEFF');
                var settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null,
                    MaxCharactersInDocument = 1024 * 1024 };
                XDocument doc;
                using (var reader = XmlReader.Create(new StringReader(xml), settings)) doc = XDocument.Load(reader);
                if (doc.Root == null || doc.Root.Name.LocalName != "toast") return null;
                XElement visual = doc.Root.Elements().FirstOrDefault(e => e.Name.LocalName == "visual");
                if (visual == null) return null;
                var bindings = visual.Elements().Where(e => e.Name.LocalName == "binding").ToList();
                XElement binding = bindings.FirstOrDefault(e => (string)e.Attribute("template") == "ToastGeneric")
                    ?? bindings.FirstOrDefault();
                if (binding == null) return null;
                var values = binding.Descendants().Where(e => e.Name.LocalName == "text" &&
                    (string)e.Attribute("placement") != "attribution")
                    .Select(e => Clean(e.Value)).Where(s => s.Length > 0).ToList();
                if (values.Count == 0) return null;
                string title = values[0];
                string body = string.Join("\n", values.Skip(1).ToArray());
                return new Notice { Id = id, Title = title, Body = body };
            }
            catch (XmlException) { return null; }
            catch (ArgumentException) { return null; }
        }

        private static string Clean(string value)
        {
            var b = new StringBuilder();
            foreach (char c in value.Trim())
                if (!char.IsControl(c) || c == '\n' || c == '\t') b.Append(c);
            return b.Length > 12000 ? b.ToString(0, 12000) : b.ToString();
        }
    }

    // Windows owns this database. Production access is always SQLITE_OPEN_READONLY;
    // no PRAGMA changes, checkpoints, copies, notification removal or content logging.
    internal sealed class NotificationDatabase : IDisposable
    {
        private IntPtr db;
        private const int Row = 100, Done = 101;
        internal NotificationDatabase(string path)
        {
            int rc = Sqlite.sqlite3_open_v2(Utf8(path), out db, 1, IntPtr.Zero);
            if (rc != 0) { Dispose(); throw new IOException("Notification database open failed: " + rc); }
            Sqlite.sqlite3_busy_timeout(db, 150);
        }
        internal List<Notice> Read()
        {
            const string sql = "SELECT [Id], [HandlerId], [ArrivalTime], [DataVersion], [Payload] " +
                "FROM [Notification] WHERE [Type]='toast' ORDER BY [Order] DESC";
            IntPtr stmt;
            int rc = Sqlite.sqlite3_prepare_v2(db, Utf8(sql), -1, out stmt, IntPtr.Zero);
            if (rc != 0) throw new IOException("Notification database query failed: " + rc);
            var rows = new List<Notice>();
            try
            {
                while ((rc = Sqlite.sqlite3_step(stmt)) == Row)
                {
                    long id = Sqlite.sqlite3_column_int64(stmt, 0);
                    int length = Sqlite.sqlite3_column_bytes(stmt, 4);
                    if (length <= 0 || length > 1024 * 1024) continue;
                    var bytes = new byte[length];
                    Marshal.Copy(Sqlite.sqlite3_column_blob(stmt, 4), bytes, 0, length);
                    string digest;
                    using (var sha = SHA256.Create()) digest = Convert.ToBase64String(sha.ComputeHash(bytes));
                    Notice notice = ToastParser.Parse(id, bytes) ?? new Notice { Id = id };
                    notice.HandlerId = Sqlite.sqlite3_column_int64(stmt, 1);
                    notice.ArrivalTime = Sqlite.sqlite3_column_int64(stmt, 2);
                    // Arrival distinguishes reuse of an Id. Order/DataVersion alone are not new content.
                    notice.Fingerprint = notice.Key + ":" + digest;
                    rows.Add(notice);
                }
                if (rc != Done) throw new IOException("Notification database read failed: " + rc);
            }
            finally { Sqlite.sqlite3_finalize(stmt); }
            rows.Reverse();
            return rows;
        }
        public void Dispose() { if (db != IntPtr.Zero) { Sqlite.sqlite3_close(db); db = IntPtr.Zero; } }
        internal static byte[] Utf8(string s) { return Encoding.UTF8.GetBytes(s + "\0"); }
        internal static string DefaultPath { get { return Path.Combine(Environment.GetFolderPath(
            Environment.SpecialFolder.LocalApplicationData), @"Microsoft\Windows\Notifications\wpndatabase.db"); } }

        internal static class Sqlite
        {
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_open_v2(byte[] filename, out IntPtr db, int flags, IntPtr vfs);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_close(IntPtr db);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_busy_timeout(IntPtr db, int ms);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_prepare_v2(IntPtr db, byte[] sql, int bytes, out IntPtr stmt, IntPtr tail);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_step(IntPtr stmt);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern long sqlite3_column_int64(IntPtr stmt, int column);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_column_bytes(IntPtr stmt, int column);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern IntPtr sqlite3_column_blob(IntPtr stmt, int column);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_finalize(IntPtr stmt);
            [DllImport("winsqlite3.dll", CallingConvention = CallingConvention.Cdecl)] internal static extern int sqlite3_db_readonly(IntPtr db, byte[] name);
            internal static bool IsReadOnly(NotificationDatabase owner) { return sqlite3_db_readonly(owner.db, Utf8("main")) == 1; }
        }
    }

    internal sealed class NoticeTracker
    {
        private readonly HashSet<string> seen = new HashSet<string>();
        private readonly Queue<string> order = new Queue<string>();
        private bool ready;
        private long baselineSince = long.MaxValue;
        internal NoticeTracker() { }
        internal NoticeTracker(long since) { baselineSince = since; }
        internal List<Notice> Accept(IEnumerable<Notice> rows)
        {
            var added = new List<Notice>();
            var retained = new HashSet<string>();
            foreach (Notice row in rows)
            {
                retained.Add(row.Fingerprint);
                if (!seen.Add(row.Fingerprint)) continue;
                order.Enqueue(row.Fingerprint);
                if ((ready || row.ArrivalTime >= baselineSince) && row.Title != null) added.Add(row);
            }
            ready = true;
            int remaining = order.Count;
            while (order.Count > 8192 && remaining-- > 0)
            {
                string key = order.Dequeue();
                if (retained.Contains(key)) order.Enqueue(key);
                else seen.Remove(key);
            }
            return added;
        }
        internal void Reset() { Reset(long.MaxValue); }
        internal void Reset(long since) { seen.Clear(); order.Clear(); ready = false; baselineSince = since; }
    }

    internal sealed class NotificationPump : IDisposable
    {
        private readonly Action<Notice> received;
        private readonly Action<bool> health;
        private readonly NoticeTracker tracker = new NoticeTracker(DateTime.UtcNow.ToFileTimeUtc());
        private readonly object gate = new object();
        private readonly string path;
        private FileSystemWatcher watcher;
        private Timer timer;
        private int busy, failures, generation, requested;
        private bool reportedHealthy;
        private volatile bool disposed, paused;
        internal NotificationPump(Action<Notice> received, Action<bool> health)
            : this(received, health, NotificationDatabase.DefaultPath) { }
        internal NotificationPump(Action<Notice> received, Action<bool> health, string path)
        {
            this.received = received; this.health = health; this.path = path;
            timer = new Timer(Poll, null, Timeout.Infinite, 650);
            try
            {
                watcher = new FileSystemWatcher(Path.GetDirectoryName(path), Path.GetFileName(path) + "*");
                watcher.NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName;
                watcher.Changed += delegate { Wake(); }; watcher.Created += delegate { Wake(); };
                watcher.Renamed += delegate { Wake(); };
                watcher.EnableRaisingEvents = true;
            }
            catch (Exception ex) { Log.Error("Notification change watcher", ex); }
        }
        internal void Start() { Wake(); }
        private void Wake()
        {
            if (disposed || paused) return;
            Interlocked.Exchange(ref requested, 1);
            try { timer.Change(0, 650); } catch (ObjectDisposedException) { }
        }
        internal bool IsCurrent(Notice notice)
        { lock (gate) return !disposed && !paused && notice.Generation == generation; }
        internal void Pause(bool value)
        {
            lock (gate) { paused = value; generation++; tracker.Reset(DateTime.UtcNow.ToFileTimeUtc()); }
            if (!value) Wake();
        }
        private void Poll(object unused)
        {
            if (disposed || paused || Interlocked.Exchange(ref busy, 1) != 0) return;
            long started=Performance.Begin();
            try
            {
                Interlocked.Exchange(ref requested, 0);
                int epoch;
                lock (gate) { epoch = generation; }
                List<Notice> rows;
                using (var db = new NotificationDatabase(path)) rows = db.Read();
                lock (gate)
                {
                    if (disposed || paused || epoch != generation) return;
                    foreach (Notice notice in tracker.Accept(rows)) { notice.Generation = epoch; received(notice); }
                }
                if (!reportedHealthy) { reportedHealthy = true; health(true); }
                failures = 0;
            }
            catch (Exception ex)
            {
                if (++failures == 1) { reportedHealthy = false; Log.Error("Notification reader", ex); health(false); }
            }
            finally
            {
                Performance.End(PerfPart.NotificationPoll,started);
                Interlocked.Exchange(ref busy, 0);
                if (Interlocked.Exchange(ref requested, 0) != 0) Wake();
            }
        }
        public void Dispose()
        {
            lock (gate) { disposed = true; generation++; }
            if (watcher != null) { watcher.Dispose(); watcher = null; }
            if (timer != null) timer.Dispose();
        }
    }
}
