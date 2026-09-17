// Native integration producer. This is not linked into or required by the application.
#include "../src/core.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
using namespace wi;
using namespace winrt;
using namespace winrt::Windows::Media;
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc!=2){LocalFree(argv);return 1;}
    fs::path dir=argv[1];LocalFree(argv);fs::create_directories(dir);init_apartment();SetCurrentProcessExplicitAppUserModelID(L"WinIsland.MediaFixture");
    try{
        auto path=dir/L"fixture.wav";std::ofstream wave(path,std::ios::binary);
        auto word=[&](auto value){wave.write((char*)&value,sizeof(value));};
        int length=8000*2*180;wave.write("RIFF",4);word(length+36);wave.write("WAVEfmt ",8);word(16);word((short)1);word((short)1);word(8000);word(16000);word((short)2);word((short)16);wave.write("data",4);word(length);
        for(int i=0;i<length/2;i++){double level=i%32000<16000?.05:.004;word((short)(32767*level*std::sin(2*3.141592653589793*220*i/8000)));}wave.close();
        Playback::MediaPlayer player;player.Volume(0);player.CommandManager().IsEnabled(false);
        player.Source(Core::MediaSource::CreateFromUri(winrt::Windows::Foundation::Uri(path.wstring())));
        auto smtc=player.SystemMediaTransportControls();smtc.IsEnabled(true);smtc.IsPlayEnabled(true);smtc.IsPauseEnabled(true);smtc.IsNextEnabled(true);smtc.IsPreviousEnabled(true);
        bool playing=true,stopped=false,ignore=false;int track=1;double position=15,stamp=now();std::mutex mutex;std::deque<std::wstring> pending;
        auto queue=[&](std::wstring s){std::lock_guard l(mutex);pending.push_back(s);};
        auto button=smtc.ButtonPressed([&](auto&&,auto&&e){auto b=e.Button();if(b==SystemMediaTransportControlsButton::Play)queue(L"play");if(b==SystemMediaTransportControlsButton::Pause)queue(L"pause");if(b==SystemMediaTransportControlsButton::Next)queue(L"next");if(b==SystemMediaTransportControlsButton::Previous)queue(L"previous");});
        auto repeat=smtc.AutoRepeatModeChangeRequested([&](auto&&,auto&&e){queue(L"repeat="+std::to_wstring((int)e.RequestedAutoRepeatMode()));});
        auto shuffle=smtc.ShuffleEnabledChangeRequested([&](auto&&,auto&&e){queue(e.RequestedShuffleEnabled()?L"shuffle=1":L"shuffle=0");});
        auto update=[&]{auto u=smtc.DisplayUpdater();u.Type(MediaPlaybackType::Music);u.MusicProperties().Title(L"WinIsland 合成媒体 "+std::to_wstring(track));u.MusicProperties().Artist(L"测试歌手");u.MusicProperties().AlbumTitle(L"本机验证");u.Update();smtc.PlaybackStatus(playing?MediaPlaybackStatus::Playing:stopped?MediaPlaybackStatus::Stopped:MediaPlaybackStatus::Paused);SystemMediaTransportControlsTimelineProperties t;t.StartTime(std::chrono::seconds(0));t.EndTime(std::chrono::seconds(180));t.MinSeekTime(std::chrono::seconds(0));t.MaxSeekTime(std::chrono::seconds(180));t.Position(winrt::Windows::Foundation::TimeSpan((long long)(position*1e7)));smtc.UpdateTimelineProperties(t);};
        player.Play();update();writeAtomic(dir/L"media-ready.txt",std::to_string(GetCurrentProcessId()));double lifetime=now();bool exit=false;
        while(!exit&&now()-lifetime<600){auto command=dir/L"media-command.txt";if(fs::exists(command)){queue(wide(readFile(command)));fs::remove(command);}std::deque<std::wstring> actions;{std::lock_guard l(mutex);actions.swap(pending);}for(auto &c:actions){
            if(c==L"exit"){exit=true;break;}if(c==L"audio-on"){player.Volume(.12);player.IsMuted(false);continue;}if(c==L"mute"){player.IsMuted(true);continue;}if(c==L"unmute"){player.IsMuted(false);continue;}if(c==L"mode-on"){smtc.AutoRepeatMode(MediaPlaybackAutoRepeatMode::None);smtc.ShuffleEnabled(false);continue;}if(c==L"ignore-next"){ignore=true;continue;}if(c==L"disable-previous"){smtc.IsPreviousEnabled(false);continue;}
            if(c.rfind(L"repeat=",0)==0){smtc.AutoRepeatMode((MediaPlaybackAutoRepeatMode)std::stoi(c.substr(7)));continue;}if(c.rfind(L"shuffle=",0)==0){smtc.ShuffleEnabled(c==L"shuffle=1");continue;}if(c==L"next"&&ignore)continue;
            if(c==L"pause"||c==L"stop"){if(playing)position+=now()-stamp;playing=false;stopped=c==L"stop";player.Pause();}
            if(c==L"play"){stamp=now();playing=true;stopped=false;player.Play();}
            if(c==L"next"||c==L"previous"){track+=c==L"next"?1:-1;position=0;stamp=now();}
            if(c.rfind(L"seek=",0)==0){position=std::stod(c.substr(5));stamp=now();player.PlaybackSession().Position(winrt::Windows::Foundation::TimeSpan((long long)(position*1e7)));}update();
        }
        writeAtomic(dir/L"media-state.txt",std::string("Playing=")+(playing?"True":"False")+"\nTrack="+std::to_string(track)+"\nRepeat="+std::to_string((int)smtc.AutoRepeatMode())+"\nShuffle="+(smtc.ShuffleEnabled()?"True":"False")+"\nMuted="+(player.IsMuted()?"True":"False")+"\n");std::this_thread::sleep_for(std::chrono::milliseconds(100));}
        smtc.ButtonPressed(button);smtc.AutoRepeatModeChangeRequested(repeat);smtc.ShuffleEnabledChangeRequested(shuffle);smtc.IsEnabled(false);player.Close();return 0;
    }catch(const winrt::hresult_error&e){writeAtomic(dir/L"fixture-error.txt",utf8(e.message().c_str()));return 1;}
}
