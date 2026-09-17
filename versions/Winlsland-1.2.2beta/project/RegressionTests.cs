using System;
using System.Collections.Generic;
using System.Reflection;

namespace WinIsland
{
    internal static class RegressionTests
    {
        internal static void Run(Action<bool, string> check)
        {
            var window = new IslandWindow();
            try
            {
                var delivered = new List<long>();
                window.NoticeDisplayed = n => delivered.Add(n.Id);
                for (int i = 1; i <= 12; i++)
                    window.Enqueue(new Notice { Id = i, Title = "会话" + i, Body = "合成消息" + i });
                var finish = typeof(IslandWindow).GetMethod("FinishNotice", BindingFlags.Instance | BindingFlags.NonPublic);
                for (int i = 0; i < 12; i++) finish.Invoke(window, null);
                check(delivered.Count == 12 && delivered[1] == 2,
                    "Burst arriving during expansion displays all 12 notifications in order (actual=" + delivered.Count + ")");
            }
            finally { window.Close(); }
            var queue = new NoticeQueue();
            queue.Enqueue(new Notice { Id = 7, HandlerId = 1, ArrivalTime = 10, Title = "会话A", Body = "旧正文" });
            queue.Enqueue(new Notice { Id = 7, HandlerId = 1, ArrivalTime = 10, Title = "会话A", Body = "更新正文" });
            queue.Enqueue(new Notice { Id = 7, HandlerId = 2, ArrivalTime = 10, Title = "会话B" });
            queue.Enqueue(new Notice { Id = 7, HandlerId = 1, ArrivalTime = 11, Title = "会话A的新消息" });
            check(queue.Count == 3 && queue.Dequeue().Body == "更新正文" && queue.Dequeue().HandlerId == 2 && queue.Dequeue().ArrivalTime == 11,
                "Pending updates merge in place; different sources and reused notification IDs remain distinct");
            var tracker = new NoticeTracker(100);
            check(tracker.Accept(new[] {
                new Notice { Id = 1, ArrivalTime = 99, Title = "历史", Fingerprint = "history" },
                new Notice { Id = 2, ArrivalTime = 101, Title = "启动中收到", Fingerprint = "during-start" }
            }).Count == 1, "First successful read delivers notifications arriving after startup, including after a reader outage");
            tracker.Reset(200);
            check(tracker.Accept(new[] {
                new Notice { Id = 3, ArrivalTime = 199, Title = "锁屏中", Fingerprint = "locked" },
                new Notice { Id = 4, ArrivalTime = 201, Title = "解锁后", Fingerprint = "unlocked" }
            }).Count == 1, "Resume baseline excludes lock-screen history without dropping a new post-resume message");
            var many = new List<Notice>();
            for (int i = 0; i < 8300; i++) many.Add(new Notice { Id = i, Title = "合成", Fingerprint = "retained-" + i });
            tracker.Reset(); tracker.Accept(many);
            check(tracker.Accept(many).Count == 0, "Long-lived notification rows do not replay when the dedup history exceeds 8192 entries");
            var selector = new MusicSelector();
            var a = new MusicState { SourceId = "cloudmusic.exe", Platform = "网易云音乐", Title = "合成 A", Artist = "甲", Paused = true };
            var b = new MusicState { SourceId = "QQMusic.exe", Platform = "QQ 音乐", Title = "合成 B", Artist = "乙", Playing = true };
            check(selector.Choose(new[] { a }, a.SourceId) == null, "An opened or initially paused player is not presented as playing");
            a.Playing = true; a.Paused = false;
            check(selector.Choose(new[] { a }, a.SourceId) == a && selector.Choose(new[] { a, b }, b.SourceId) == a,
                "The playing source stays selected when another player starts or Windows changes its preferred session");
            a.Playing = false; a.Paused = true;
            check(selector.Choose(new[] { a, b }, a.SourceId) == b, "Pausing the selected source switches once to an actually playing source");
            b.Playing = false; b.Paused = true;
            check(selector.Choose(new[] { a, b }, a.SourceId) == b && selector.Choose(new[] { a }, a.SourceId) == null,
                "Pause retains the selected track; source exit clears it without falling back to an inactive app");
            check(MusicPlatforms.Identify("cloudmusic.exe") == "网易云音乐" && MusicPlatforms.Identify("QQMusic.exe") == "QQ 音乐" &&
                MusicPlatforms.Identify("com.luna.music") == "汽水音乐" && MusicPlatforms.Identify("QQ.exe") == null,
                "All three music platform identifiers are supported; QQ chat is excluded");
            var lrc = Lyrics.Parse("[ar:测试歌手]\n[offset:500]\n[00:01.00][00:03.000]第一行\n[00:02.50]第二行\n[00:04.00]\n[00:65.00]无效");
            check(lrc.Count == 4 && lrc.At(0) == "" && lrc.At(.5) == "第一行" && lrc.At(2.1) == "第二行" && lrc.At(2.5) == "第一行" && lrc.At(3.6) == "",
                "LRC timestamps, offsets, repeated tags, blank intervals and seek lookup are accurate");
            a.HasTimeline = true; a.Position = 10; a.Duration = 20; a.PositionAt = DateTimeOffset.UtcNow; a.Playing = true;
            check(a.PositionNow(a.PositionAt.AddSeconds(4)) == 14 && a.PositionNow(a.PositionAt.AddSeconds(30)) == 20,
                "Media timeline advances while playing and clamps at duration");
            a.Playing = false;
            check(a.PositionNow(a.PositionAt.AddSeconds(4)) == 10, "Pause freezes lyric and timeline position");
            var visibility = new MusicVisibility(); var moment = DateTime.UtcNow;
            check(visibility.Project(a, moment) == a && visibility.Project(a, moment.AddSeconds(9.9)) == a && visibility.Project(a, moment.AddSeconds(10)) == null,
                "Inactive music remains for ten seconds, then retracts without resetting on repeated polls");
            a.Playing = true;
            check(visibility.Project(a, moment.AddSeconds(11)) == a, "Resuming music resets inactivity and restores its presentation");
            window = new IslandWindow();
            try
            {
                var events = new List<long>(); window.NoticeDisplayed = n => events.Add(n.Id);
                window.SetMusic(a, lrc);
                window.Enqueue(new Notice { Id = 1000, Title = "合成消息", Body = "音乐期间到达" });
                for (int i = 0; i < 20; i++) { a.Position = i; window.SetMusic(a, lrc); }
                window.SetMusic(b, null);
                check(window.HasMusic && window.MusicTitle == b.Title && events.Count == 1 && window.CurrentLyric == "",
                    "Music updates and track changes leave the current message intact and clear unavailable lyrics");
                window.SetMusic(null, null);
                check(!window.HasMusic && events.Count == 1, "Closing the music source does not dismiss or replay the message");
            }
            finally { window.Close(); }
        }
    }
}
