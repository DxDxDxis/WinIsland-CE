using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Media.Imaging;
using Windows.Foundation;
using Windows.Media;
using Windows.Media.Control;
using Windows.Storage.Streams;

namespace WinIsland
{
    internal static class WinRtAsync
    {
        // System WinMetadata is split by namespace; use its native completion callback.
        // This avoids a Windows SDK or any redistributed runtime assembly.
        internal static async Task<T> Complete<T>(IAsyncOperation<T> operation)
        {
            var task = new TaskCompletionSource<T>();
            operation.Completed = (op, status) => {
                try { task.TrySetResult(op.GetResults()); }
                catch (Exception ex) { task.TrySetException(ex); }
            };
            if (await Task.WhenAny(task.Task, Task.Delay(2500)).ConfigureAwait(false) != task.Task)
            {
                try { operation.Cancel(); } catch { }
                CloseWhenCompleted(task.Task, operation);
                ObserveFault(task.Task);
                throw new TimeoutException("Windows media operation timed out");
            }
            try { return await task.Task.ConfigureAwait(false); }
            finally { operation.Close(); }
        }
        private static void ObserveFault(Task task)
        { task.ContinueWith(t => { var ignored = t.Exception; }, TaskContinuationOptions.OnlyOnFaulted); }
        private static void CloseWhenCompleted<T>(Task task, IAsyncOperation<T> operation)
        { task.ContinueWith(t => { try { operation.Close(); } catch { } }, TaskScheduler.Default); }
    }

    internal sealed class MediaSessions : IDisposable
    {
        private sealed class Watch
        {
            internal GlobalSystemMediaTransportControlsSession Session;
            internal int Revision, ReadRevision = -1;
            internal string Title, Artist, Album;
            internal BitmapSource Cover;
            internal DateTime LastRead = DateTime.MinValue;
            internal TypedEventHandler<GlobalSystemMediaTransportControlsSession, MediaPropertiesChangedEventArgs> Changed;
        }
        private readonly Dictionary<string, Watch> watches = new Dictionary<string, Watch>();
        private readonly MusicSelector selector = new MusicSelector();
        private readonly Action<MusicState, int> changed;
        private readonly Action wake;
        private GlobalSystemMediaTransportControlsSessionManager manager;
        private readonly bool fixtures;
        private readonly object gate = new object();
        private readonly Timer timer;
        private int busy, generation;
        private volatile bool disposed, paused;
        private MusicState current;
        private readonly Dictionary<string, string> failedCommands = new Dictionary<string, string>();
        internal volatile bool FixtureOnly;
        internal MediaSessions(Action<MusicState, int> changed, bool fixtures)
        {
            this.changed = changed; this.fixtures = fixtures;
            timer = new Timer(Poll, null, Timeout.Infinite, 400);
            wake = delegate { try { if (!disposed && !paused) timer.Change(0, 400); } catch (ObjectDisposedException) { } };
        }
        internal void Start() { wake(); }
        internal bool IsCurrent(int epoch) { lock (gate) return !disposed && !paused && generation == epoch; }
        internal void Pause(bool value)
        {
            lock (gate) { paused = value; generation++; current = null; }
            if (!value) wake();
        }
        private void Publish(MusicState state, int epoch)
        {
            lock (gate)
            {
                if (!IsCurrent(epoch)) return;
                current = state; changed(state, epoch);
            }
        }
        private async void Poll(object unused)
        {
            if (disposed || paused || Interlocked.Exchange(ref busy, 1) != 0) return;
            long started=Performance.Begin();
            int epoch; lock (gate) epoch = generation;
            try
            {
                if (manager == null) manager = await WinRtAsync.Complete(GlobalSystemMediaTransportControlsSessionManager.RequestAsync()).ConfigureAwait(false);
                var sessions = manager.GetSessions();
                var live = new HashSet<string>();
                var candidates = new List<MusicState>();
                foreach (var session in sessions)
                {
                    if (!IsCurrent(epoch)) return;
                    string source = session.SourceAppUserModelId;
                    string platform = MusicPlatforms.Identify(source);
                    if (fixtures && FixtureOnly && source != "WinIsland.MediaFixture") continue;
                    if (platform != null && !MusicPlatforms.IsRunning(platform)) continue;
                    if (platform == null && fixtures && source.IndexOf("WinIsland", StringComparison.OrdinalIgnoreCase) >= 0) platform = "媒体集成测试";
                    if (platform == null) continue;
                    live.Add(source);
                    try
                    {
                        Watch watch;
                        if (watches.TryGetValue(source, out watch) && !object.ReferenceEquals(watch.Session, session))
                        { Unwatch(watch); watches.Remove(source); }
                        if (!watches.TryGetValue(source, out watch))
                        {
                            watch = new Watch { Session = session };
                            Watch captured = watch;
                            watch.Changed = (s, args) => { Interlocked.Increment(ref captured.Revision); wake(); };
                            session.MediaPropertiesChanged += watch.Changed;
                            watches.Add(source, watch);
                        }
                        var info = session.GetPlaybackInfo();
                        bool playing = info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
                        bool isPaused = info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused || info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Stopped;
                        int revision = Volatile.Read(ref watch.Revision);
                        if (watch.ReadRevision != revision || (DateTime.UtcNow - watch.LastRead).TotalSeconds > 5)
                        {
                            if (watch.ReadRevision != revision && selector.Selected == source)
                                Publish(new MusicState { SourceId = source, Platform = platform, Loading = true, Playing = playing, Paused = isPaused }, epoch);
                            var properties = await WinRtAsync.Complete(session.TryGetMediaPropertiesAsync()).ConfigureAwait(false);
                            string key = properties.Title + "\n" + properties.Artist + "\n" + properties.AlbumTitle;
                            bool sameTrack = key == watch.Title + "\n" + watch.Artist + "\n" + watch.Album;
                            BitmapSource cover = sameTrack ? watch.Cover : null;
                            // Never retain the previous track's image when the next thumbnail is absent or invalid.
                            if (!sameTrack || watch.ReadRevision != revision || cover == null)
                                cover = await ReadCover(properties.Thumbnail).ConfigureAwait(false);
                            if (revision != Volatile.Read(ref watch.Revision) || !IsCurrent(epoch)) continue;
                            watch.Title = properties.Title; watch.Artist = properties.Artist; watch.Album = properties.AlbumTitle;
                            watch.Cover = cover; watch.ReadRevision = revision; watch.LastRead = DateTime.UtcNow;
                        }
                        if (string.IsNullOrWhiteSpace(watch.Title) || (!playing && !isPaused)) continue;
                        info = session.GetPlaybackInfo();
                        playing = info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
                        isPaused = info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused || info.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Stopped;
                        if (!playing && !isPaused) continue;
                        var timeline = session.GetTimelineProperties();
                        string trackKey = source + "\n" + watch.Title + "\n" + watch.Artist + "\n" + watch.Album;
                        candidates.Add(new MusicState {
                            SourceId = source, Platform = platform, Title = watch.Title, Artist = watch.Artist, Album = watch.Album,
                            Playing = playing, Paused = isPaused, Cover = watch.Cover,
                            HasTimeline = timeline.EndTime > timeline.StartTime && timeline.LastUpdatedTime.Year >= 2000,
                            Position = (timeline.Position - timeline.StartTime).TotalSeconds,
                            Duration = (timeline.EndTime - timeline.StartTime).TotalSeconds,
                            PositionAt = timeline.LastUpdatedTime > DateTimeOffset.UtcNow ? DateTimeOffset.UtcNow : timeline.LastUpdatedTime,
                            Rate = info.PlaybackRate.HasValue ? info.PlaybackRate.Value : 1,
                            CanToggle = (playing ? info.Controls.IsPauseEnabled : info.Controls.IsPlayEnabled) && CommandAvailable(source, trackKey, "toggle"),
                            CanPrevious = info.Controls.IsPreviousEnabled && CommandAvailable(source, trackKey, "previous"),
                            CanNext = info.Controls.IsNextEnabled && CommandAvailable(source, trackKey, "next"),
                            RepeatMode = info.AutoRepeatMode.HasValue ? (int)info.AutoRepeatMode.Value : -1, Shuffle = info.IsShuffleActive,
                            CanRepeat = info.Controls.IsRepeatEnabled, CanShuffle = info.Controls.IsShuffleEnabled,
                            CanMode = info.AutoRepeatMode.HasValue && info.IsShuffleActive.HasValue &&
                                (info.Controls.IsRepeatEnabled || info.Controls.IsShuffleEnabled) && CommandAvailable(source, trackKey, "mode")
                        });
                    }
                    catch (Exception ex) { Log.Error("Media source", ex); }
                }
                foreach (string removed in watches.Keys.Where(id => !live.Contains(id)).ToArray())
                {
                    Unwatch(watches[removed]); watches.Remove(removed);
                }
                var preferred = manager.GetCurrentSession();
                Publish(selector.Choose(candidates, preferred == null ? null : preferred.SourceAppUserModelId), epoch);
            }
            catch (Exception ex) { Log.Error("Media reader", ex); Publish(null, epoch); }
            finally { Performance.End(PerfPart.MediaPoll,started); Interlocked.Exchange(ref busy, 0); }
        }
        private static async Task<BitmapSource> ReadCover(IRandomAccessStreamReference reference)
        {
            if (reference == null) return null;
            try
            {
                using (var stream = await WinRtAsync.Complete(reference.OpenReadAsync()).ConfigureAwait(false))
                {
                    if (stream.Size == 0 || stream.Size > 4 * 1024 * 1024) return null;
                    using (var reader = new DataReader(stream.GetInputStreamAt(0)))
                    {
                        uint size = (uint)stream.Size;
                        uint read = await WinRtAsync.Complete(reader.LoadAsync(size)).ConfigureAwait(false);
                        if (read != size) return null;
                        byte[] bytes = new byte[size]; reader.ReadBytes(bytes);
                        using (var memory = new MemoryStream(bytes))
                        {
                            var bitmap = new BitmapImage(); bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad;
                            bitmap.DecodePixelWidth = 160; bitmap.StreamSource = memory; bitmap.EndInit(); bitmap.Freeze();
                            return bitmap;
                        }
                    }
                }
            }
            catch { return null; }
        }
        internal async Task<bool> Command(string command, string expectedTrack)
        {
            string source;
            lock (gate)
            {
                if (disposed || paused || current == null || current.TrackKey != expectedTrack) return false;
                source = current.SourceId;
            }
            try
            {
                var session = manager.GetSessions().FirstOrDefault(s => s.SourceAppUserModelId == source);
                if (session == null) return false;
                var properties = await WinRtAsync.Complete(session.TryGetMediaPropertiesAsync());
                if (source + "\n" + properties.Title + "\n" + properties.Artist + "\n" + properties.AlbumTitle != expectedTrack) return false;
                var before = session.GetPlaybackInfo().PlaybackStatus;
                double position = session.GetTimelineProperties().Position.TotalSeconds;
                if (command == "mode")
                {
                    bool applied = await ChangeMode(session);
                    if (!applied) DisableCommand(source, expectedTrack, command);
                    wake(); return applied;
                }
                bool result = command == "previous" ? await WinRtAsync.Complete(session.TrySkipPreviousAsync()) :
                    command == "next" ? await WinRtAsync.Complete(session.TrySkipNextAsync()) :
                    before == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing ?
                        await WinRtAsync.Complete(session.TryPauseAsync()) : await WinRtAsync.Complete(session.TryPlayAsync());
                // Some players accept a command without acting on it. Only retain the button
                // when playback/metadata (or a previous-track restart) confirms the action.
                if (result) result = await ConfirmCommand(session, command, expectedTrack, before, position);
                if (!result) DisableCommand(source, expectedTrack, command);
                wake(); return result;
            }
            catch (Exception ex) { DisableCommand(source, expectedTrack, command); wake(); Log.Error("Media command", ex); return false; }
        }
        private static async Task<bool> ChangeMode(GlobalSystemMediaTransportControlsSession session)
        {
            var info = session.GetPlaybackInfo();
            if (!info.AutoRepeatMode.HasValue || !info.IsShuffleActive.HasValue) return false;
            bool shuffle = info.IsShuffleActive.Value; var repeat = info.AutoRepeatMode.Value;
            if (shuffle)
            {
                if (!info.Controls.IsShuffleEnabled) return false;
                shuffle = false;
                if (info.Controls.IsRepeatEnabled) repeat = MediaPlaybackAutoRepeatMode.None;
            }
            else if (info.Controls.IsRepeatEnabled)
            {
                if (repeat == MediaPlaybackAutoRepeatMode.None) repeat = MediaPlaybackAutoRepeatMode.List;
                else if (repeat == MediaPlaybackAutoRepeatMode.List) repeat = MediaPlaybackAutoRepeatMode.Track;
                else if (info.Controls.IsShuffleEnabled) shuffle = true;
                else repeat = MediaPlaybackAutoRepeatMode.None;
            }
            else if (info.Controls.IsShuffleEnabled) shuffle = true;
            else return false;
            if (shuffle != info.IsShuffleActive.Value && !await WinRtAsync.Complete(session.TryChangeShuffleActiveAsync(shuffle))) return false;
            if (repeat != info.AutoRepeatMode.Value && !await WinRtAsync.Complete(session.TryChangeAutoRepeatModeAsync(repeat))) return false;
            var deadline = DateTime.UtcNow.AddSeconds(2);
            do
            {
                await Task.Delay(120); info = session.GetPlaybackInfo();
                if (info.IsShuffleActive == shuffle && info.AutoRepeatMode == repeat) return true;
            } while (DateTime.UtcNow < deadline);
            return false;
        }
        private static async Task<bool> ConfirmCommand(GlobalSystemMediaTransportControlsSession session, string command,
            string track, GlobalSystemMediaTransportControlsSessionPlaybackStatus before, double position)
        {
            var deadline = DateTime.UtcNow.AddSeconds(2);
            do
            {
                await Task.Delay(120);
                if (command == "toggle")
                {
                    var status = session.GetPlaybackInfo().PlaybackStatus;
                    if (before == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing ?
                        status == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Paused :
                        status == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing) return true;
                }
                else
                {
                    var value = await WinRtAsync.Complete(session.TryGetMediaPropertiesAsync());
                    if (session.SourceAppUserModelId + "\n" + value.Title + "\n" + value.Artist + "\n" + value.AlbumTitle != track) return true;
                    if (position > 2 && session.GetTimelineProperties().Position.TotalSeconds < 1) return true;
                }
            } while (DateTime.UtcNow < deadline);
            return false;
        }
        private bool CommandAvailable(string source, string track, string command)
        { lock (gate) { string failed; return !failedCommands.TryGetValue(source + ":" + command, out failed) || failed != track; } }
        private void DisableCommand(string source, string track, string command)
        { lock (gate) failedCommands[source + ":" + command] = track; }
        private static void Unwatch(Watch watch) { try { watch.Session.MediaPropertiesChanged -= watch.Changed; } catch { } }
        public void Dispose()
        {
            lock (gate) { disposed = true; generation++; current = null; }
            timer.Dispose();
            // Poll owns the watch dictionary. Cleanup runs after its in-flight callback exits.
            Task.Run(async delegate {
                while (Volatile.Read(ref busy) != 0) await Task.Delay(50);
                foreach (var watch in watches.Values) Unwatch(watch);
                watches.Clear(); manager = null;
            });
        }
    }
}
