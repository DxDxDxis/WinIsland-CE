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
            const string sql = "SELECT [Id], [Order], [ArrivalTime], [DataVersion], [Payload] " +
                "FROM [Notification] WHERE [Type]='toast' ORDER BY [Order] DESC LIMIT 256";
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
                    string key = id + ":" + Sqlite.sqlite3_column_int64(stmt, 1) + ":" +
                        Sqlite.sqlite3_column_int64(stmt, 2) + ":" + Sqlite.sqlite3_column_int64(stmt, 3) + ":" + digest;
                    Notice notice = ToastParser.Parse(id, bytes) ?? new Notice { Id = id };
                    notice.Fingerprint = key;
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
        internal List<Notice> Accept(IEnumerable<Notice> rows)
        {
            var added = new List<Notice>();
            foreach (Notice row in rows)
            {
                if (!seen.Add(row.Fingerprint)) continue;
                order.Enqueue(row.Fingerprint);
                if (ready && row.Title != null) added.Add(row);
            }
            ready = true;
            while (order.Count > 8192) seen.Remove(order.Dequeue());
            return added;
        }
        internal void Reset() { seen.Clear(); order.Clear(); ready = false; }
    }

    internal sealed class NotificationPump : IDisposable
    {
        private readonly Action<Notice> received;
        private readonly Action<bool> health;
        private readonly NoticeTracker tracker = new NoticeTracker();
        private readonly object gate = new object();
        private Timer timer;
        private int busy, failures;
        private volatile bool disposed, paused;
        internal NotificationPump(Action<Notice> received, Action<bool> health)
        {
            this.received = received; this.health = health;
            timer = new Timer(Poll, null, 0, 650);
        }
        internal void Pause(bool value)
        {
            lock (gate) { paused = value; tracker.Reset(); }
        }
        private void Poll(object unused)
        {
            if (disposed || paused || Interlocked.Exchange(ref busy, 1) != 0) return;
            try
            {
                List<Notice> rows;
                using (var db = new NotificationDatabase(NotificationDatabase.DefaultPath)) rows = db.Read();
                lock (gate)
                {
                    if (disposed || paused) return;
                    foreach (Notice notice in tracker.Accept(rows)) received(notice);
                }
                if (failures != 0) health(true);
                failures = 0;
            }
            catch (Exception ex)
            {
                if (++failures == 1) { Log.Error("Notification reader", ex); health(false); }
            }
            finally { Interlocked.Exchange(ref busy, 0); }
        }
        public void Dispose() { disposed = true; if (timer != null) { timer.Dispose(); timer = null; } }
    }
}
