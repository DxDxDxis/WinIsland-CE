using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using Windows.Media;
using Windows.Media.Core;
using Windows.Media.Playback;
using Forms = System.Windows.Forms;

namespace WinIsland
{
    // A silent local Windows media session used only by the explicit diagnostic mode.
    internal static class MediaFixture
    {
        internal static int Run(string directory)
        {
            Directory.CreateDirectory(directory);
            Native.SetCurrentProcessExplicitAppUserModelID("WinIsland.MediaFixture");
            string audio = Path.Combine(directory, "silent-fixture.wav");
            int length = 8000 * 2 * 180;
            using (var writer = new BinaryWriter(File.Create(audio)))
            {
                writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write(length + 36);
                writer.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16);
                writer.Write((short)1); writer.Write((short)1); writer.Write(8000); writer.Write(16000);
                writer.Write((short)2); writer.Write((short)16); writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write(length);
                // Diagnostic PCM only. Muted by default; explicit audio-on tests the real
                // Windows session meter with alternating low-level tones, never UI animation.
                for (int sample = 0; sample < length / 2; sample++)
                {
                    double amplitude = sample % 32000 < 16000 ? .05 : .004;
                    writer.Write((short)(32767 * amplitude * Math.Sin(2 * Math.PI * 220 * sample / 8000)));
                }
            }
            using (var player = new MediaPlayer())
            using (var timer = new Forms.Timer { Interval = 200 })
            {
                player.Volume = 0; player.CommandManager.IsEnabled = false;
                player.Source = MediaSource.CreateFromUri(new Uri(audio));
                var controls = player.SystemMediaTransportControls;
                controls.IsEnabled = true; controls.IsPlayEnabled = true; controls.IsPauseEnabled = true;
                controls.IsNextEnabled = true; controls.IsPreviousEnabled = true;
                bool playing = true, ignoreNext = false, stopped = false; int track = 1;
                bool? requestedShuffle = null; MediaPlaybackAutoRepeatMode? requestedRepeat = null;
                controls.AutoRepeatModeChangeRequested += (s, e) => requestedRepeat = e.RequestedAutoRepeatMode;
                controls.ShuffleEnabledChangeRequested += (s, e) => requestedShuffle = e.RequestedShuffleEnabled;
                var lifetime = Stopwatch.StartNew(); var elapsed = Stopwatch.StartNew();
                double position = 15;
                Action update = delegate {
                    controls.DisplayUpdater.Type = MediaPlaybackType.Music;
                    controls.DisplayUpdater.MusicProperties.Title = "WinIsland 合成媒体 " + track;
                    controls.DisplayUpdater.MusicProperties.Artist = "测试歌手";
                    controls.DisplayUpdater.MusicProperties.AlbumTitle = "本机验证";
                    controls.DisplayUpdater.Update();
                    controls.PlaybackStatus = playing ? MediaPlaybackStatus.Playing : stopped ? MediaPlaybackStatus.Stopped : MediaPlaybackStatus.Paused;
                    controls.UpdateTimelineProperties(new SystemMediaTransportControlsTimelineProperties {
                        StartTime = TimeSpan.Zero, EndTime = TimeSpan.FromSeconds(180), MinSeekTime = TimeSpan.Zero,
                        MaxSeekTime = TimeSpan.FromSeconds(180), Position = TimeSpan.FromSeconds(position)
                    });
                };
                Action<string> command = text => {
                    if (text == "audio-on") { player.Volume = .12; player.IsMuted = false; return; }
                    if (text == "mute") { player.IsMuted = true; return; }
                    if (text == "unmute") { player.IsMuted = false; return; }
                    if (text == "mode-on") { controls.AutoRepeatMode = MediaPlaybackAutoRepeatMode.None; controls.ShuffleEnabled = false; return; }
                    if (text == "ignore-next") { ignoreNext = true; return; }
                    if (text == "disable-previous") { controls.IsPreviousEnabled = false; return; }
                    if (text == "next" && ignoreNext) return;
                    if (text == "pause" || text == "stop") { position += elapsed.Elapsed.TotalSeconds; elapsed.Reset(); playing = false; stopped = text == "stop"; player.Pause(); }
                    if (text == "play") { elapsed.Restart(); playing = true; stopped = false; player.Play(); }
                    if (text.StartsWith("seek=")) { position = double.Parse(text.Substring(5), System.Globalization.CultureInfo.InvariantCulture); player.PlaybackSession.Position = TimeSpan.FromSeconds(position); if (playing) elapsed.Restart(); }
                    if (text == "next" || text == "previous") { track += text == "next" ? 1 : -1; position = 0; elapsed.Restart(); }
                    update();
                };
                string pendingButton = null;
                controls.ButtonPressed += (s, e) => {
                    if (e.Button == SystemMediaTransportControlsButton.Play) pendingButton = "play";
                    if (e.Button == SystemMediaTransportControlsButton.Pause) pendingButton = "pause";
                    if (e.Button == SystemMediaTransportControlsButton.Next) pendingButton = "next";
                    if (e.Button == SystemMediaTransportControlsButton.Previous) pendingButton = "previous";
                };
                player.Play(); update();
                timer.Tick += delegate {
                    string path = Path.Combine(directory, "media-command.txt");
                    if (File.Exists(path))
                    {
                        string value = File.ReadAllText(path).Trim(); File.Delete(path);
                        if (value == "exit") { Forms.Application.ExitThread(); return; }
                        command(value);
                    }
                    if (pendingButton != null) { string value = pendingButton; pendingButton = null; command(value); }
                    if (requestedRepeat.HasValue) { controls.AutoRepeatMode = requestedRepeat.Value; requestedRepeat = null; }
                    if (requestedShuffle.HasValue) { controls.ShuffleEnabled = requestedShuffle.Value; requestedShuffle = null; }
                    File.WriteAllLines(Path.Combine(directory, "media-state.txt"), new[] { "Playing=" + playing, "Track=" + track,
                        "Repeat=" + controls.AutoRepeatMode, "Shuffle=" + controls.ShuffleEnabled, "Muted=" + player.IsMuted, "Volume=" + player.Volume });
                    if (lifetime.Elapsed.TotalSeconds > 180) Forms.Application.ExitThread();
                };
                timer.Start();
                File.WriteAllText(Path.Combine(directory, "media-ready.txt"), Process.GetCurrentProcess().Id.ToString());
                Forms.Application.Run();
                controls.IsEnabled = false;
            }
            return 0;
        }
    }
}
