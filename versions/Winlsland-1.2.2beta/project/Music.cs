using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Media.Imaging;

namespace WinIsland
{
    internal sealed class MusicState
    {
        internal string SourceId, Platform, Title, Artist, Album;
        internal bool Playing, Paused, Loading, HasTimeline, CanToggle, CanPrevious, CanNext;
        internal bool CanRepeat, CanShuffle, CanMode;
        internal int RepeatMode = -1;
        internal bool? Shuffle;
        internal string ModeName { get { return Shuffle == true ? "随机播放" : RepeatMode == 1 ? "单曲循环" : RepeatMode == 2 ? "列表循环" : RepeatMode == 0 ? "顺序播放" : "播放模式未知"; } }
        internal double Position, Duration, Rate = 1;
        internal DateTimeOffset PositionAt = DateTimeOffset.UtcNow;
        internal BitmapSource Cover;
        internal string TrackKey { get { return SourceId + "\n" + Title + "\n" + Artist + "\n" + Album; } }
        internal double PositionNow(DateTimeOffset now)
        {
            if (!HasTimeline) return 0;
            double delta = Playing ? Math.Max(0, (now - PositionAt).TotalSeconds) * Rate : 0;
            return Math.Max(0, Math.Min(Duration, Position + delta));
        }
    }

    internal sealed class MusicVisibility
    {
        private DateTime? inactive;
        private string source;
        internal MusicState Project(MusicState value, DateTime now)
        {
            if (value == null || value.Playing || value.SourceId != source) inactive = null;
            source = value == null ? null : value.SourceId;
            if (value == null || value.Playing) return value;
            if (!inactive.HasValue) inactive = now;
            return now - inactive.Value >= TimeSpan.FromSeconds(10) ? null : value;
        }
    }

    internal static class MusicPlatforms
    {
        internal static string Identify(string source)
        {
            string id = (source ?? "").ToLowerInvariant();
            if (id.Contains("cloudmusic") || id.Contains("netease")) return "网易云音乐";
            if (id.Contains("qqmusic") || id.Contains("qq音乐")) return "QQ 音乐";
            if (id.Contains("sodamusic") || id.Contains("soda music") || id.Contains("soda.music") || id.Contains("com.luna.music") || id.Contains("汽水音乐")) return "汽水音乐";
            return null;
        }
        internal static bool IsRunning(string platform)
        {
            string name = platform == "网易云音乐" ? "cloudmusic" : platform == "QQ 音乐" ? "QQMusic" : "SodaMusic";
            Process[] processes = Process.GetProcessesByName(name);
            bool running = processes.Length > 0;
            foreach (var process in processes) process.Dispose();
            return running;
        }
    }

    internal sealed class MusicSelector
    {
        private string selected;
        private readonly HashSet<string> played = new HashSet<string>();
        internal string Selected { get { return selected; } }
        internal MusicState Choose(IList<MusicState> sources, string preferred)
        {
            var live = new HashSet<string>(sources.Select(s => s.SourceId));
            played.RemoveWhere(id => !live.Contains(id));
            foreach (var s in sources) if (s.Playing && !s.Loading && !string.IsNullOrWhiteSpace(s.Title)) played.Add(s.SourceId);
            MusicState current = sources.FirstOrDefault(s => s.SourceId == selected);
            MusicState result = current != null && current.Playing ? current :
                sources.FirstOrDefault(s => s.Playing && s.SourceId == preferred) ?? sources.FirstOrDefault(s => s.Playing);
            if (result == null && current != null && current.Paused && played.Contains(current.SourceId)) result = current;
            selected = result == null ? null : result.SourceId;
            return result;
        }
        internal void Reset() { selected = null; played.Clear(); }
    }

    internal sealed class LyricLine
    {
        internal double Seconds;
        internal string Text;
    }

    internal sealed class Lyrics
    {
        private readonly List<LyricLine> lines;
        private Lyrics(List<LyricLine> lines) { this.lines = lines; }
        internal int Count { get { return lines.Count; } }
        internal IEnumerable<string> Texts { get { return lines.Select(line => line.Text); } }
        internal static Lyrics Parse(string text)
        {
            if (text == null || text.Length > 1024 * 1024) throw new InvalidDataException("LRC size");
            double offset = 0;
            var offsetMatch = Regex.Match(text, @"\[offset:([+-]?\d+)\]", RegexOptions.IgnoreCase);
            if (offsetMatch.Success) double.TryParse(offsetMatch.Groups[1].Value, NumberStyles.Integer, CultureInfo.InvariantCulture, out offset);
            var result = new List<LyricLine>();
            foreach (string row in text.Split('\n'))
            {
                var stamps = Regex.Matches(row, @"\[(\d{1,3}):(\d{2})(?:[.:](\d{1,3}))?\]");
                if (stamps.Count == 0) continue;
                Match last = stamps[stamps.Count - 1];
                string value = row.Substring(last.Index + last.Length).Trim();
                if (value.Length > 500) value = value.Substring(0, 500);
                foreach (Match stamp in stamps)
                {
                    int seconds = int.Parse(stamp.Groups[2].Value, CultureInfo.InvariantCulture);
                    if (seconds >= 60) continue;
                    double fraction = stamp.Groups[3].Success ? double.Parse("0." + stamp.Groups[3].Value, CultureInfo.InvariantCulture) : 0;
                    result.Add(new LyricLine { Seconds = Math.Max(0, int.Parse(stamp.Groups[1].Value, CultureInfo.InvariantCulture) * 60 + seconds + fraction - offset / 1000), Text = value });
                }
            }
            return new Lyrics(result.OrderBy(line => line.Seconds).ToList());
        }
        internal string At(double position)
        {
            int low = 0, high = lines.Count - 1, found = -1;
            while (low <= high)
            {
                int mid = low + (high - low) / 2;
                if (lines[mid].Seconds <= position) { found = mid; low = mid + 1; } else high = mid - 1;
            }
            return found < 0 ? "" : lines[found].Text;
        }
    }

    internal sealed class LyricStore
    {
        private readonly string directory;
        internal LyricStore(string directory) { this.directory = directory; }
        private string PathFor(string track)
        {
            using (var sha = SHA256.Create())
                return Path.Combine(directory, BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(track))).Replace("-", "") + ".lrc");
        }
        internal Lyrics Load(string track)
        {
            long started=Performance.Begin();try {
            if (directory == null) return null;
            string path = PathFor(track);
            try { return File.Exists(path) && new FileInfo(path).Length <= 1024 * 1024 ? Lyrics.Parse(File.ReadAllText(path)) : null; }
            catch (Exception ex) { Log.Error("Lyric load", ex); return null; }
            } finally { Performance.End(PerfPart.LyricLoad,started); }
        }
        internal Lyrics Import(string track, string file)
        {
            if (new FileInfo(file).Length > 1024 * 1024) throw new InvalidDataException("歌词文件不能超过 1 MB。");
            string text = File.ReadAllText(file);
            Lyrics lyrics = Lyrics.Parse(text);
            if (lyrics.Count == 0) throw new InvalidDataException("文件中没有可识别的 LRC 时间标签。");
            Directory.CreateDirectory(directory);
            string path = PathFor(track), temp = path + ".tmp";
            File.WriteAllText(temp, text, Encoding.UTF8);
            if (File.Exists(path)) File.Replace(temp, path, null); else File.Move(temp, path);
            return lyrics;
        }
        internal void Remove(string track) { if (directory != null) File.Delete(PathFor(track)); }
    }
}
