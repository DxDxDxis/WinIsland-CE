#include "media_runtime.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
namespace wi {
static bool makeTestVideo(const fs::path& file){
    if(fs::exists(file))return true;
    HRESULT startup=MFStartup(MF_VERSION);if(FAILED(startup))return false;
    bool ok=false;
    {ComPtr<IMFSinkWriter> writer;ComPtr<IMFMediaType> output,input;DWORD stream=0;
    if(SUCCEEDED(MFCreateSinkWriterFromURL(file.c_str(),nullptr,nullptr,&writer))&&SUCCEEDED(MFCreateMediaType(&output))&&SUCCEEDED(MFCreateMediaType(&input))){
        output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);output->SetUINT32(MF_MT_AVG_BITRATE,800000);output->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,320,180);MFSetAttributeRatio(output.Get(),MF_MT_FRAME_RATE,30,1);MFSetAttributeRatio(output.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);MFSetAttributeSize(input.Get(),MF_MT_FRAME_SIZE,320,180);MFSetAttributeRatio(input.Get(),MF_MT_FRAME_RATE,30,1);MFSetAttributeRatio(input.Get(),MF_MT_PIXEL_ASPECT_RATIO,1,1);
        if(SUCCEEDED(writer->AddStream(output.Get(),&stream))&&SUCCEEDED(writer->SetInputMediaType(stream,input.Get(),nullptr))&&SUCCEEDED(writer->BeginWriting())){
            ok=true;for(int i=0;i<90&&ok;++i){ComPtr<IMFMediaBuffer> buffer;ComPtr<IMFSample> sample;BYTE* data=nullptr;
                if(FAILED(MFCreateMemoryBuffer(320*180*4,&buffer))||FAILED(MFCreateSample(&sample))||FAILED(buffer->Lock(&data,nullptr,nullptr))){ok=false;break;}
                for(int y=0;y<180;++y)for(int x=0;x<320;++x){size_t at=(y*320+x)*4;data[at]=uint8_t(x+i*3);data[at+1]=uint8_t(y+i*2);data[at+2]=uint8_t(i*5);data[at+3]=255;}
                buffer->Unlock();buffer->SetCurrentLength(320*180*4);sample->AddBuffer(buffer.Get());sample->SetSampleTime(i*10000000ll/30);sample->SetSampleDuration(10000000/30);ok=SUCCEEDED(writer->WriteSample(stream,sample.Get()));
            }ok=SUCCEEDED(writer->Finalize())&&ok;
        }
    }}MFShutdown();return ok;
}
bool MediaEngine::selfTest(const fs::path& assets,const fs::path& report){
    std::ostringstream log;unsigned passed=0,failed=0;
    auto check=[&](bool yes,const char* name){log<<(yes?"PASS ":"FAIL ")<<name<<'\n';yes?++passed:++failed;};
    check(makeTestVideo(assets/L"motion.mp4"),"local H.264 sample generated or supplied");
    MediaEngine engine;MediaSnapshot s;
    auto wait=[&](WiMedia id,auto condition){double deadline=now()+8;while(now()<deadline){if(engine.snapshot(id,s)&&condition(s))return true;Sleep(10);}return false;};
    auto ready=[&](WiMedia id){bool ok=wait(id,[](auto& x){return x.info.state==WI_MEDIA_READY||x.info.state==WI_MEDIA_ERROR;});if(ok&&s.info.error[0])log<<"decoder-error="<<s.info.error<<'\n';return ok&&s.info.state==WI_MEDIA_READY&&s.frame;};
    double start=now();check(engine.load(1,assets,L"skin.png")==0&&now()-start<.05,"load queues in under 50ms");check(ready(1)&&s.info.width==320&&s.info.height==120,"PNG decoded asynchronously at actual dimensions");
    check(engine.command(1,MediaPlay)==WI_MEDIA_UNSUPPORTED,"static image does not fake playback");engine.release(1);check(!engine.snapshot(1,s)&&engine.command(1,MediaPlay)==WI_MEDIA_NOT_FOUND,"release invalidates handle immediately");
    engine.load(2,assets,L"motion.gif");check(ready(2)&&s.info.frameCount==4&&std::abs(s.info.duration-.72)<.001,"GIF frame count and nonuniform duration");
    const double times[]={.01,.13,.33,.49};
    for(int i=0;i<4;++i){engine.command(2,MediaSeek,times[i]);bool sampled=wait(2,[&](auto& x){return std::abs(x.info.position-times[i])<.001;});std::string expected=readFile(assets/wide("gif-"+std::to_string(i)+".bgra"));
        bool equal=sampled&&s.frame&&s.frame->bgra.size()==expected.size()&&!memcmp(s.frame->bgra.data(),expected.data(),expected.size());log<<"frame="<<i<<" expectedBytes="<<expected.size()<<'\n';check(equal,"GIF RGBA composition matches independent Pillow disposal oracle");}
    engine.command(2,MediaSeek,0);engine.command(2,MediaPlay);check(wait(2,[](auto& x){return x.info.position>.22&&x.info.state==WI_MEDIA_PLAYING;}),"GIF time advances with decoded frames");
    engine.command(2,MediaPause);check(wait(2,[](auto& x){return x.info.state==WI_MEDIA_PAUSED;}),"pause acknowledged");double paused=s.info.position;auto frame=s.frame;Sleep(180);engine.snapshot(2,s);check(s.info.position==paused&&s.frame==frame,"pause freezes clock and frame");
    engine.command(2,MediaPlay);check(wait(2,[&](auto& x){return x.info.position>paused+.08;}),"resume continues position");
    engine.command(2,MediaHidden,1);Sleep(80);engine.snapshot(2,s);double hidden=s.info.position;Sleep(130);engine.snapshot(2,s);check(s.info.position==hidden,"hidden pause policy freezes time");
    engine.command(2,MediaHiddenPolicy,1);check(wait(2,[&](auto& x){return x.info.position!=hidden;}),"hidden continue policy keeps clock running");engine.command(2,MediaHidden,0);
    engine.command(2,MediaLoop,0);engine.command(2,MediaSeek,.69);check(wait(2,[](auto& x){return x.info.state==WI_MEDIA_ENDED;}),"nonloop GIF ends");
    engine.command(2,MediaLoop,1);engine.command(2,MediaPlay);check(wait(2,[](auto& x){return x.info.state==WI_MEDIA_PLAYING&&x.info.position<.5;}),"restart from ended");
    engine.command(2,MediaRate,2);check(wait(2,[](auto& x){return x.info.rate==2;}),"playback rate applied");check(engine.command(2,MediaRate,NAN)==WI_MEDIA_INVALID,"nonfinite rate rejected");
    engine.load(3,assets,L"motion.mp4");bool videoReady=ready(3);check(videoReady&&s.info.kind==WI_MEDIA_VIDEO&&s.info.duration>2.9,"real MP4/H.264 decoded by Media Foundation");
    if(videoReady){auto first=s.frame;engine.command(3,MediaPlay);check(wait(3,[](auto& x){return x.info.position>.4&&x.info.frameSequence>4;}),"video produces multiple timed frames");check(s.frame&&s.frame->bgra!=first->bgra,"video pixels actually change");engine.command(3,MediaPause);wait(3,[](auto& x){return x.info.state==WI_MEDIA_PAUSED;});double p=s.info.position;Sleep(150);engine.snapshot(3,s);check(s.info.position==p,"video pause freezes position");engine.command(3,MediaSeek,2);check(wait(3,[](auto& x){return x.info.position==2&&x.event=="seek.completed";}),"video seek completes");engine.command(3,MediaStop);check(wait(3,[](auto& x){return x.info.state==WI_MEDIA_STOPPED&&x.info.position==0;}),"video stop rewinds");engine.command(3,MediaLoop,1);engine.command(3,MediaSeek,2.95);engine.command(3,MediaPlay);check(wait(3,[](auto& x){return x.info.state==WI_MEDIA_PLAYING&&x.info.position<.3;}),"video loops to start");}
    if(videoReady&&s.info.hasAudio){
        engine.command(3,MediaVolume,.05);engine.command(3,MediaMuted,0);
        bool audible=wait(3,[](auto& x){return x.info.audioSamplesSubmitted>0&&!x.info.muted;});
        if(!audible)log<<"audio-error="<<s.info.error<<'\n';check(audible,"AAC decoded to bounded PCM buffers and submitted to Windows audio device");
        engine.command(3,MediaMuted,1);check(wait(3,[](auto& x){return x.info.muted==1;}),"mute stops audio output");
    }
    engine.command(2,MediaPause);engine.replace(2,assets,L"motion.mp4");engine.replace(2,assets,L"skin.png");check(ready(2)&&s.generation==3&&s.info.kind==WI_MEDIA_IMAGE,"rapid source replacement keeps latest generation only");
    engine.load(4,assets,L"broken.png");check(wait(4,[](auto& x){return x.info.state==WI_MEDIA_ERROR&&x.info.error[0];}),"corrupt media reports async error");
    engine.load(5,assets,L"../skin.png");check(wait(5,[](auto& x){return x.info.state==WI_MEDIA_ERROR;}),"root traversal rejected by decoder worker");
    engine.release(2);engine.release(3);engine.release(4);engine.release(5);
    for(int i=0;i<20;++i){engine.load(100+i,assets,L"motion.gif");engine.release(100+i);}check(!engine.snapshot(119,s),"release during load leaves no callable handle");
    log<<"Passed="<<passed<<" Failed="<<failed<<'\n';writeAtomic(report,log.str());return failed==0;
}
}
