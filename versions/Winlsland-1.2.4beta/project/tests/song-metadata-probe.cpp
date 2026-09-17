#include "../src/core.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    int n;auto args=CommandLineToArgvW(GetCommandLineW(),&n);if(n<2)return 1;winrt::init_apartment();
    try{auto manager=winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();std::ostringstream out;
        for(auto session:manager.GetSessions()){std::wstring id(session.SourceAppUserModelId());auto lower=id;std::transform(lower.begin(),lower.end(),lower.begin(),towlower);
            if(n==4&&lower.find(args[2])!=lower.npos){auto op=std::wstring(args[3]);if(op==L"play")session.TryPlayAsync().get();if(op==L"pause")session.TryPauseAsync().get();std::this_thread::sleep_for(std::chrono::milliseconds(500));}
            auto info=session.GetPlaybackInfo();auto time=session.GetTimelineProperties();auto c=info.Controls();out<<"source="<<wi::utf8(id)<<" playing="<<(int)info.PlaybackStatus()<<" timeline="<<(time.EndTime()>time.StartTime())<<" previous="<<c.IsPreviousEnabled()<<" play="<<c.IsPlayEnabled()<<" pause="<<c.IsPauseEnabled()<<" next="<<c.IsNextEnabled()<<" repeat="<<c.IsRepeatEnabled()<<" shuffle="<<c.IsShuffleEnabled()<<'\n';auto meta=session.TryGetMediaPropertiesAsync().get();out<<"title="<<wi::utf8(meta.Title().c_str())<<"\nartist="<<wi::utf8(meta.Artist().c_str())<<"\nalbum="<<wi::utf8(meta.AlbumTitle().c_str())<<"\n";
            out<<"position_100ns="<<time.Position().count()<<" start_100ns="<<time.StartTime().count()<<" end_100ns="<<time.EndTime().count()<<" minseek_100ns="<<time.MinSeekTime().count()<<" maxseek_100ns="<<time.MaxSeekTime().count()<<" updated_utc_100ns="<<time.LastUpdatedTime().time_since_epoch().count()<<"\n";
        }wi::writeAtomic(args[1],out.str());LocalFree(args);return 0;
    }catch(...){LocalFree(args);return 1;}
}
