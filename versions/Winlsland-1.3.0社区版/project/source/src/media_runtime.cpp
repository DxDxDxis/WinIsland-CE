#include "media_runtime.h"
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <mmsystem.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <thread>
namespace wi {
namespace {
void require(HRESULT hr,const char* step){if(FAILED(hr))throw std::runtime_error(std::string(step)+" HRESULT="+std::to_string((uint32_t)hr));}
uint32_t metadata(IWICMetadataQueryReader* q,const wchar_t* key,uint32_t fallback=0){PROPVARIANT v{};if(!q||FAILED(q->GetMetadataByName(key,&v)))return fallback;uint32_t n=fallback;if(v.vt==VT_UI1)n=v.bVal;else if(v.vt==VT_UI2)n=v.uiVal;else if(v.vt==VT_UI4)n=v.ulVal;else if(v.vt==VT_BOOL)n=v.boolVal!=0;PropVariantClear(&v);return n;}
fs::path checkedFile(const fs::path& root,const fs::path& rel){
    if(rel.empty()||rel.is_absolute()||rel.has_root_path())throw std::runtime_error("Media source must be relative to its authorized root");
    for(auto& part:rel){auto s=part.wstring();if(s==L".."||s.find(L':')!=s.npos)throw std::runtime_error("Media path traversal/stream rejected");}
    auto base=fs::canonical(root),file=fs::canonical(root/rel);auto a=base.begin(),b=file.begin();
    for(;a!=base.end();++a,++b)if(b==file.end()||_wcsicmp(a->c_str(),b->c_str()))throw std::runtime_error("Media source escapes authorized root");
    if(b==file.end()||!fs::is_regular_file(file))throw std::runtime_error("Media source is not a file");
    if(fs::file_size(file)>512ull*1024*1024)throw std::runtime_error("Media source exceeds 512 MiB");return file;
}
std::shared_ptr<MediaFrame> pixels(IWICImagingFactory* f,IWICBitmapSource* src,uint32_t maxEdge){
    UINT w=0,h=0;require(src->GetSize(&w,&h),"image size");if(!w||!h||uint64_t(w)*h>64ull*1024*1024)throw std::runtime_error("Image exceeds 64 megapixel source budget");
    ComPtr<IWICBitmapScaler> scaler;UINT dw=w,dh=h;
    if(std::max(w,h)>maxEdge){double ratio=double(maxEdge)/std::max(w,h);dw=std::max(1u,UINT(w*ratio));dh=std::max(1u,UINT(h*ratio));require(f->CreateBitmapScaler(&scaler),"image scaler");require(scaler->Initialize(src,dw,dh,WICBitmapInterpolationModeFant),"image scale");src=scaler.Get();}
    ComPtr<IWICFormatConverter> converter;require(f->CreateFormatConverter(&converter),"image converter");require(converter->Initialize(src,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"PBGRA conversion");
    auto frame=std::make_shared<MediaFrame>();frame->width=dw;frame->height=dh;frame->bgra.resize(size_t(dw)*dh*4);require(converter->CopyPixels(nullptr,dw*4,(UINT)frame->bgra.size(),frame->bgra.data()),"image pixels");return frame;
}
struct Command {uint32_t op;double value;};
struct Session {
    std::mutex mutex;
    std::atomic<bool> cancelled=false;
    MediaSnapshot published;
    std::deque<Command> commands;
    fs::path root,relative;uint32_t maxEdge=1920;
    uint64_t generation=1;bool needsLoad=true;
    // Fields below are used only by decoder thread.
    uint64_t activeGeneration=0,frameSequence=0;WiMediaInfo info{sizeof(WiMediaInfo),WI_MEDIA_ABI};
    ComPtr<IMFSourceReader> video;
    struct AudioChunk {WAVEHDR header{};std::vector<uint8_t> data;};
    ComPtr<IMFSourceReader> audioReader;HWAVEOUT audioDevice=nullptr;
    std::deque<std::unique_ptr<AudioChunk>> audioQueue;fs::path sourceFile;
    bool audioEof=false;double audioQueuedUntil=0;
    std::vector<std::shared_ptr<MediaFrame>> frames;std::vector<double> ends;
    std::shared_ptr<MediaFrame> nextVideo,lastFrame;
    double nextTime=0,position=0,lastTick=0,rate=1;uint32_t state=WI_MEDIA_LOADING;
    bool loop=true,loopExplicit=false,hidden=false;uint32_t hiddenPolicy=WI_MEDIA_PAUSE_HIDDEN;
    uint32_t intrinsicLoops=0,completedLoops=0;bool videoEof=false,seekPending=false;
    bool stale(){return cancelled.load();}
    ~Session(){closeAudio();}
    void clearAudio(){if(audioDevice){waveOutReset(audioDevice);for(auto& c:audioQueue)waveOutUnprepareHeader(audioDevice,&c->header,sizeof(WAVEHDR));}audioQueue.clear();audioEof=false;audioQueuedUntil=position;}
    void closeAudio(){clearAudio();if(audioDevice){waveOutClose(audioDevice);audioDevice=nullptr;}audioReader.Reset();}
    void seekAudio(double t){if(!audioReader)return;clearAudio();PROPVARIANT v{};InitPropVariantFromInt64((LONGLONG)(t*10000000),&v);require(audioReader->SetCurrentPosition(GUID_NULL,v),"audio seek");PropVariantClear(&v);}
    void openAudio(){
        if(audioDevice||!info.hasAudio||info.muted)return;
        require(MFCreateSourceReaderFromURL(sourceFile.c_str(),nullptr,&audioReader),"audio source");audioReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);require(audioReader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM,TRUE),"audio selection");
        ComPtr<IMFMediaType> pcm;require(MFCreateMediaType(&pcm),"audio type");pcm->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio);pcm->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_PCM);pcm->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2);pcm->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,44100);pcm->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,16);pcm->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,4);pcm->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,176400);
        require(audioReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,nullptr,pcm.Get()),"PCM audio conversion");
        WAVEFORMATEX format{WAVE_FORMAT_PCM,2,44100,176400,4,16,0};auto rc=waveOutOpen(&audioDevice,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);if(rc)throw std::runtime_error("Audio output unavailable, waveOut="+std::to_string(rc));
        DWORD volume=(DWORD)(std::clamp(info.volume,0.,1.)*65535);waveOutSetVolume(audioDevice,volume|(volume<<16));
        if(rate!=1&&waveOutSetPlaybackRate(audioDevice,(DWORD)(rate*65536)))throw std::runtime_error("Audio device does not support this playback rate");seekAudio(position);
    }
    void pumpAudio(){
        if(info.muted||!info.hasAudio)return;openAudio();if(!audioDevice)return;
        while(!audioQueue.empty()&&(audioQueue.front()->header.dwFlags&WHDR_DONE)){waveOutUnprepareHeader(audioDevice,&audioQueue.front()->header,sizeof(WAVEHDR));audioQueue.pop_front();}
        bool playing=state==WI_MEDIA_PLAYING&&!(hidden&&hiddenPolicy==WI_MEDIA_PAUSE_HIDDEN);
        if(!playing){waveOutPause(audioDevice);return;}waveOutRestart(audioDevice);
        if(audioQueuedUntil+.3<position)seekAudio(position);
        while(audioQueue.size()<6&&audioQueuedUntil<position+.2&&!audioEof&&!stale()){
            DWORD flags=0;LONGLONG timestamp=0;ComPtr<IMFSample> sample;require(audioReader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,nullptr,&flags,&timestamp,&sample),"audio sample");if(flags&MF_SOURCE_READERF_ENDOFSTREAM){audioEof=true;break;}if(!sample)break;
            ComPtr<IMFMediaBuffer> buffer;require(sample->ConvertToContiguousBuffer(&buffer),"audio buffer");BYTE* data=nullptr;DWORD bytes=0;require(buffer->Lock(&data,nullptr,&bytes),"audio lock");
            if(bytes>1024*1024){buffer->Unlock();throw std::runtime_error("Audio sample exceeds 1 MiB");}
            size_t skip=timestamp/10000000.<position?std::min<size_t>(bytes,size_t((position-timestamp/10000000.)*44100)*4):0;
            auto chunk=std::make_unique<AudioChunk>();chunk->data.assign(data+skip,data+bytes);buffer->Unlock();audioQueuedUntil=timestamp/10000000.+bytes/176400.;if(chunk->data.empty())continue;
            chunk->header.lpData=(LPSTR)chunk->data.data();chunk->header.dwBufferLength=(DWORD)chunk->data.size();
            if(waveOutPrepareHeader(audioDevice,&chunk->header,sizeof(WAVEHDR)))throw std::runtime_error("Audio buffer preparation failed");
            if(waveOutWrite(audioDevice,&chunk->header,sizeof(WAVEHDR))){waveOutUnprepareHeader(audioDevice,&chunk->header,sizeof(WAVEHDR));throw std::runtime_error("Audio buffer submission failed");}info.audioSamplesSubmitted+=chunk->data.size()/4;audioQueue.push_back(std::move(chunk));
        }
    }
    void publish(const std::string& event={},std::shared_ptr<MediaFrame> frame={}){
        std::lock_guard lock(mutex);if(stale()||activeGeneration!=generation)return;
        info.state=state;info.position=position;info.rate=rate;info.loop=loop;info.generation=generation;
        if(frame){published.frame=frame;lastFrame=frame;info.frameSequence=++frameSequence;++published.revision;}
        published.info=info;published.generation=generation;
        if(!event.empty()){published.event=event;++published.eventSequence;++published.revision;}
    }
    void seekVideo(double t){PROPVARIANT v{};InitPropVariantFromInt64((LONGLONG)(t*10000000),&v);require(video->SetCurrentPosition(GUID_NULL,v),"video seek");PropVariantClear(&v);nextVideo.reset();videoEof=false;seekAudio(t);}
    void readVideo(){
        for(int n=0;n<8&&!stale();++n){DWORD flags=0;LONGLONG timestamp=0;ComPtr<IMFSample> sample;
            require(video->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&timestamp,&sample),"video frame");
            if(flags&MF_SOURCE_READERF_ENDOFSTREAM){videoEof=true;return;}if(!sample)continue;
            if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED){ComPtr<IMFMediaType> t;require(video->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&t),"video type");require(MFGetAttributeSize(t.Get(),MF_MT_FRAME_SIZE,&info.width,&info.height),"video resize");}
            if(uint64_t(info.width)*info.height>4096ull*4096)throw std::runtime_error("Video decoded frame exceeds 4096 squared budget");
            ComPtr<IMFMediaBuffer> buffer;require(sample->ConvertToContiguousBuffer(&buffer),"video buffer");
            auto frame=std::make_shared<MediaFrame>();frame->width=info.width;frame->height=info.height;frame->bgra.resize(size_t(info.width)*info.height*4);
            ComPtr<IMF2DBuffer> twoD;
            if(SUCCEEDED(buffer.As(&twoD))){BYTE* row=nullptr;LONG stride=0;require(twoD->Lock2D(&row,&stride),"video lock2D");for(uint32_t y=0;y<info.height;++y)memcpy(frame->bgra.data()+size_t(y)*info.width*4,row+ptrdiff_t(y)*stride,info.width*4);twoD->Unlock2D();}
            else {BYTE* data=nullptr;DWORD bytes=0;require(buffer->Lock(&data,nullptr,&bytes),"video lock");if(bytes<frame->bgra.size()){buffer->Unlock();throw std::runtime_error("Video frame truncated");}memcpy(frame->bgra.data(),data,frame->bgra.size());buffer->Unlock();}
            for(size_t a=3;a<frame->bgra.size();a+=4)frame->bgra[a]=255;
            nextVideo=frame;nextTime=timestamp/10000000.;return;
        }
    }
    void loadVideo(const fs::path& file){
        ComPtr<IMFAttributes> attrs;require(MFCreateAttributes(&attrs,3),"video attributes");attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);attrs->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS,FALSE);
        require(MFCreateSourceReaderFromURL(file.c_str(),attrs.Get(),&video),"video source");
        require(video->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE),"video streams");require(video->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE),"video selection");
        ComPtr<IMFMediaType> native;require(video->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,&native),"native video type");GUID codec{};native->GetGUID(MF_MT_SUBTYPE,&codec);
        require(MFGetAttributeSize(native.Get(),MF_MT_FRAME_SIZE,&info.originalWidth,&info.originalHeight),"native size");
        UINT32 num=0,den=1;if(SUCCEEDED(MFGetAttributeRatio(native.Get(),MF_MT_FRAME_RATE,&num,&den))&&den)info.frameRate=double(num)/den;
        if(codec!=MFVideoFormat_H264)throw std::runtime_error("This backend currently accepts H.264 video only");
        ComPtr<IMFMediaType> audio;info.hasAudio=SUCCEEDED(video->GetNativeMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM,0,&audio));
        ComPtr<IMFMediaType> output;require(MFCreateMediaType(&output),"RGB media type");output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
        // Native software conversion; frames are bounded and never decoded on UI/plugin callbacks.
        info.width=info.originalWidth;info.height=info.originalHeight;
        if(std::max(info.width,info.height)>maxEdge){double s=double(maxEdge)/std::max(info.width,info.height);info.width=std::max(2u,(uint32_t(info.width*s)/2)*2);info.height=std::max(2u,(uint32_t(info.height*s)/2)*2);MFSetAttributeSize(output.Get(),MF_MT_FRAME_SIZE,info.width,info.height);}
        require(video->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,output.Get()),"RGB32 video output");
        ComPtr<IMFMediaType> actual;require(video->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&actual),"video output type");require(MFGetAttributeSize(actual.Get(),MF_MT_FRAME_SIZE,&info.width,&info.height),"output video size");
        PROPVARIANT duration{};if(SUCCEEDED(video->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,MF_PD_DURATION,&duration))&&duration.vt==VT_UI8)info.duration=duration.uhVal.QuadPart/10000000.;PropVariantClear(&duration);
        info.kind=WI_MEDIA_VIDEO;info.hasAlpha=0;info.muted=1;info.audioSupported=info.hasAudio;strcpy_s(info.format,"MP4");strcpy_s(info.codec,"Windows Media Foundation H.264 / software RGB32");
        readVideo();if(!nextVideo)throw std::runtime_error("Video has no decoded frames");lastFrame=nextVideo;nextVideo.reset();
    }
    void loadImage(IWICImagingFactory* factory,IWICBitmapDecoder* decoder){
        GUID format{};require(decoder->GetContainerFormat(&format),"image format");UINT count=0;require(decoder->GetFrameCount(&count),"frame count");info.frameCount=count;
        if(format!=GUID_ContainerFormatGif){
            if(format!=GUID_ContainerFormatPng&&format!=GUID_ContainerFormatJpeg&&format!=GUID_ContainerFormatBmp)throw std::runtime_error("Image codec is not in the verified capability table");
            ComPtr<IWICBitmapFrameDecode> frame;require(decoder->GetFrame(0,&frame),"image frame");require(frame->GetSize(&info.originalWidth,&info.originalHeight),"image size");
            lastFrame=pixels(factory,frame.Get(),maxEdge);info.kind=WI_MEDIA_IMAGE;info.hasAlpha=format==GUID_ContainerFormatPng;
            strcpy_s(info.format,format==GUID_ContainerFormatPng?"PNG":format==GUID_ContainerFormatJpeg?"JPEG":"BMP");strcpy_s(info.codec,"Windows Imaging Component / PBGRA32");return;
        }
        ComPtr<IWICMetadataQueryReader> meta;decoder->GetMetadataQueryReader(&meta);UINT w=metadata(meta.Get(),L"/logscrdesc/Width"),h=metadata(meta.Get(),L"/logscrdesc/Height");
        if(!w||!h||uint64_t(w)*h>4096ull*4096||count>2000)throw std::runtime_error("GIF canvas/frame count budget exceeded");
        info.originalWidth=w;info.originalHeight=h;info.kind=WI_MEDIA_ANIMATION;info.hasAlpha=1;strcpy_s(info.format,"GIF");strcpy_s(info.codec,"WIC GIF / disposal-aware PBGRA composition");
        intrinsicLoops=1;PROPVARIANT loops{};if(meta&&SUCCEEDED(meta->GetMetadataByName(L"/appext/data",&loops))&&loops.vt==(VT_VECTOR|VT_UI1)&&loops.caub.cElems>=4){auto* b=loops.caub.pElems;if(b[0]==3&&b[1]==1){auto count=uint32_t(b[2])|(uint32_t(b[3])<<8);intrinsicLoops=count?count+1:0;}}PropVariantClear(&loops);
        uint32_t background=0;ComPtr<IWICBitmapFrameDecode> first;decoder->GetFrame(0,&first);ComPtr<IWICMetadataQueryReader> firstMeta;if(first)first->GetMetadataQueryReader(&firstMeta);
        if(!metadata(firstMeta.Get(),L"/grctlext/TransparencyFlag")){ComPtr<IWICPalette> palette;factory->CreatePalette(&palette);if(palette&&SUCCEEDED(decoder->CopyPalette(palette.Get()))){WICColor colors[256]{};UINT count=0;palette->GetColors(256,colors,&count);auto index=metadata(meta.Get(),L"/logscrdesc/BackgroundColorIndex");if(index<count)background=colors[index]|0xff000000;}}
        std::vector<uint8_t> canvas(size_t(w)*h*4),previous;for(size_t p=0;p<canvas.size();p+=4)memcpy(canvas.data()+p,&background,4);size_t total=0;double end=0;
        for(UINT i=0;i<count;++i){if(stale())return;ComPtr<IWICBitmapFrameDecode> frame;require(decoder->GetFrame(i,&frame),"GIF frame");ComPtr<IWICMetadataQueryReader> q;frame->GetMetadataQueryReader(&q);
            UINT x=metadata(q.Get(),L"/imgdesc/Left"),y=metadata(q.Get(),L"/imgdesc/Top"),disposal=metadata(q.Get(),L"/grctlext/Disposal");
            auto part=pixels(factory,frame.Get(),4096);if(x+part->width>w||y+part->height>h)throw std::runtime_error("GIF frame rectangle exceeds canvas");
            if(disposal==3)previous=canvas;
            for(UINT row=0;row<part->height;++row)for(UINT col=0;col<part->width;++col){auto* src=part->bgra.data()+(size_t(row)*part->width+col)*4;auto* dst=canvas.data()+(size_t(row+y)*w+x+col)*4;uint32_t alpha=src[3];for(int c=0;c<4;++c)dst[c]=uint8_t(src[c]+(dst[c]*(255-alpha)+127)/255);}
            ComPtr<IWICBitmap> bitmap;require(factory->CreateBitmapFromMemory(w,h,GUID_WICPixelFormat32bppPBGRA,w*4,(UINT)canvas.size(),canvas.data(),&bitmap),"GIF canvas");auto composed=pixels(factory,bitmap.Get(),maxEdge);
            total+=composed->bgra.size();if(total>64ull*1024*1024)throw std::runtime_error("GIF decoded frame cache exceeds 64 MiB; reduce dimensions/frame count");frames.push_back(composed);
            end+=std::max(2u,metadata(q.Get(),L"/grctlext/Delay",10))/100.;ends.push_back(end);
            if(disposal==2)for(UINT row=0;row<part->height;++row)for(UINT col=0;col<part->width;++col)memcpy(canvas.data()+(size_t(row+y)*w+x+col)*4,&background,4);
            else if(disposal==3)canvas.swap(previous);
        }
        if(frames.empty())throw std::runtime_error("GIF has no frames");info.duration=end;info.frameRate=count/end;lastFrame=frames[0];
    }
    void load(const fs::path& path){
        closeAudio();sourceFile=path;video.Reset();frames.clear();ends.clear();nextVideo.reset();lastFrame.reset();videoEof=false;seekPending=false;position=0;completedLoops=0;intrinsicLoops=0;
        info={sizeof(WiMediaInfo),WI_MEDIA_ABI};info.muted=1;info.volume=1;
        ComPtr<IWICImagingFactory> factory;require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"WIC factory");ComPtr<IWICBitmapDecoder> decoder;
        HRESULT imageResult=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder);
        if(SUCCEEDED(imageResult))loadImage(factory.Get(),decoder.Get());else loadVideo(path);
        if(stale())return;if(!lastFrame)throw std::runtime_error("No decoded media frame");info.width=lastFrame->width;info.height=lastFrame->height;state=WI_MEDIA_READY;lastTick=now();publish("ready",lastFrame);
    }
    void step(){
        fs::path srcRoot,srcRelative;bool loading=false;std::deque<Command> work;
        {std::lock_guard lock(mutex);if(needsLoad){needsLoad=false;loading=true;activeGeneration=generation;srcRoot=root;srcRelative=relative;}work.swap(commands);}
        try {
            if(loading)load(checkedFile(srcRoot,srcRelative));if(stale())return;
            for(auto cmd:work){switch(cmd.op){
                case MediaPlay:if(state==WI_MEDIA_ENDED){position=0;completedLoops=0;if(video)seekVideo(0);}state=WI_MEDIA_PLAYING;lastTick=now();publish("playing");break;
                case MediaPause:state=WI_MEDIA_PAUSED;publish("paused");break;
                case MediaStop:state=WI_MEDIA_STOPPED;position=0;completedLoops=0;if(video)seekVideo(0);publish("stopped",frames.empty()?nullptr:frames[0]);break;
                case MediaSeek:position=std::clamp(cmd.value,0.,info.duration);if(video)seekVideo(position);seekPending=true;break;
                case MediaLoop:loop=cmd.value!=0;loopExplicit=true;completedLoops=0;break;
                case MediaRate:if(audioDevice&&waveOutSetPlaybackRate(audioDevice,(DWORD)(cmd.value*65536))){strcpy_s(info.error,"Audio device rejected playback rate");publish("control.error");}else rate=cmd.value;break;
                case MediaMuted:info.muted=cmd.value!=0;if(info.muted)closeAudio();publish("muted.changed");break;
                case MediaVolume:info.volume=cmd.value;if(audioDevice){DWORD v=(DWORD)(cmd.value*65535);waveOutSetVolume(audioDevice,v|(v<<16));}publish("volume.changed");break;
                case MediaHidden:hidden=cmd.value!=0;lastTick=now();break;
                case MediaHiddenPolicy:hiddenPolicy=(uint32_t)cmd.value;break;
                default:break;
            }}
            double t=now(),dt=std::max(0.,t-lastTick);lastTick=t;
            if(state==WI_MEDIA_PLAYING&&!(hidden&&hiddenPolicy==WI_MEDIA_PAUSE_HIDDEN))position+=dt*rate;
            if(info.duration>0&&position>=info.duration&&state==WI_MEDIA_PLAYING){
                ++completedLoops;bool repeat=loop&&(loopExplicit||!intrinsicLoops||completedLoops<intrinsicLoops);
                if(repeat){position=std::fmod(position,info.duration);if(video)seekVideo(position);publish("loop");}
                else {position=info.duration;state=WI_MEDIA_ENDED;publish("ended");}
            }
            if(!hidden||!lastFrame||seekPending){
                if(!frames.empty()){size_t index=std::min(frames.size()-1,size_t(std::upper_bound(ends.begin(),ends.end(),position)-ends.begin()));if(frames[index]!=lastFrame)publish({},frames[index]);}
                else if(video){if(!nextVideo&&!videoEof)readVideo();int dropped=0;std::shared_ptr<MediaFrame> selected;
                    while(nextVideo&&nextTime<=position+.001&&dropped++<8&&!stale()){selected=nextVideo;nextVideo.reset();if(!videoEof)readVideo();}
                    if(selected)publish({},selected);
                    if(videoEof&&!nextVideo&&state==WI_MEDIA_PLAYING&&info.duration<=0){state=WI_MEDIA_ENDED;publish("ended");}
                }
            }
            if(seekPending&&(!video||videoEof||(nextVideo&&nextTime>position))){seekPending=false;publish("seek.completed");}
            try{pumpAudio();}catch(const std::exception& e){closeAudio();info.muted=1;strncpy_s(info.error,e.what(),_TRUNCATE);publish("audio.error");}
            publish();
        }catch(const std::exception& e){state=WI_MEDIA_ERROR;strncpy_s(info.error,e.what(),_TRUNCATE);video.Reset();frames.clear();nextVideo.reset();publish("error");}
    }
};
}
struct MediaEngine::Impl {
    mutable std::mutex mutex;std::condition_variable cv;std::map<WiMedia,std::shared_ptr<Session>> sessions;std::vector<std::shared_ptr<Session>> retired;bool stopping=false,wake=false;std::thread worker,picker;
    std::atomic<bool> picking=false;
    Impl():worker([this]{run();}){}
    ~Impl(){{std::lock_guard lock(mutex);stopping=true;for(auto& [id,s]:sessions)s->cancelled=true;}cv.notify_all();if(picker.joinable())picker.join();worker.join();}
    void run(){CoInitializeEx(nullptr,COINIT_MULTITHREADED);HRESULT mf=MFStartup(MF_VERSION,MFSTARTUP_LITE);
        bool active=false;
        for(;;){std::vector<std::shared_ptr<Session>> list,cleanup;{std::unique_lock lock(mutex);if(active)cv.wait_for(lock,std::chrono::milliseconds(10),[&]{return stopping||wake;});else cv.wait(lock,[&]{return stopping||wake;});wake=false;if(stopping)break;for(auto& [id,s]:sessions)list.push_back(s);cleanup.swap(retired);}
            cleanup.clear();active=false;
            for(auto& s:list)if(!s->stale()){s->step();active|=s->seekPending||(s->state==WI_MEDIA_PLAYING&&!(s->hidden&&s->hiddenPolicy==WI_MEDIA_PAUSE_HIDDEN));}
        }
        {std::lock_guard lock(mutex);sessions.clear();retired.clear();}if(SUCCEEDED(mf))MFShutdown();CoUninitialize();
    }
};
MediaEngine::MediaEngine():impl(std::make_unique<Impl>()){}MediaEngine::~MediaEngine()=default;
int MediaEngine::load(WiMedia id,const fs::path& root,const fs::path& relative,uint32_t edge){
    std::lock_guard lock(impl->mutex);if(impl->sessions.size()+impl->retired.size()>=8)return WI_MEDIA_LIMIT;
    auto s=std::make_shared<Session>();s->root=root;s->relative=relative;s->maxEdge=std::clamp(edge,32u,4096u);s->published.info.state=WI_MEDIA_LOADING;s->published.info.generation=1;s->published.generation=1;s->published.revision=1;s->published.eventSequence=1;s->published.event="loading";
    impl->sessions.emplace(id,s);impl->wake=true;impl->cv.notify_one();return WI_MEDIA_OK;
}
int MediaEngine::replace(WiMedia id,const fs::path& root,const fs::path& relative){
    std::lock_guard lock(impl->mutex);auto it=impl->sessions.find(id);if(it==impl->sessions.end())return WI_MEDIA_NOT_FOUND;auto& s=*it->second;std::lock_guard state(s.mutex);
    s.root=root;s.relative=relative;++s.generation;s.needsLoad=true;s.commands.clear();s.published.info.state=WI_MEDIA_LOADING;s.published.info.generation=s.generation;s.published.generation=s.generation;++s.published.revision;++s.published.eventSequence;s.published.event="loading";impl->wake=true;impl->cv.notify_one();return WI_MEDIA_OK;
}
int MediaEngine::command(WiMedia id,uint32_t op,double value){
    if(!std::isfinite(value))return WI_MEDIA_INVALID;
    if((op==MediaRate&&(value<.1||value>4))||(op==MediaSeek&&value<0)||(op==MediaVolume&&(value<0||value>1))||(op==MediaHiddenPolicy&&(value<0||value>1)))return WI_MEDIA_INVALID;
    std::lock_guard lock(impl->mutex);auto it=impl->sessions.find(id);if(it==impl->sessions.end())return WI_MEDIA_NOT_FOUND;auto& s=*it->second;std::lock_guard state(s.mutex);
    if((op==MediaMuted||op==MediaVolume)&&s.published.info.state!=WI_MEDIA_LOADING&&!s.published.info.audioSupported)return WI_MEDIA_UNSUPPORTED;
    if(s.published.info.kind==WI_MEDIA_IMAGE&&(op==MediaPlay||op==MediaPause||op==MediaStop||op==MediaSeek||op==MediaRate))return WI_MEDIA_UNSUPPORTED;
    if(s.published.info.state==WI_MEDIA_ERROR)return WI_MEDIA_INVALID;
    // Latest playback and seek intent wins; no unbounded queue on rapid interaction.
    std::erase_if(s.commands,[&](auto& c){return c.op==op||(op<=MediaStop&&c.op<=MediaStop);});if(s.commands.size()>=16)return WI_MEDIA_BUSY;s.commands.push_back({op,value});impl->wake=true;impl->cv.notify_one();return WI_MEDIA_OK;
}
bool MediaEngine::snapshot(WiMedia id,MediaSnapshot& out) const{std::lock_guard lock(impl->mutex);auto it=impl->sessions.find(id);if(it==impl->sessions.end())return false;std::lock_guard state(it->second->mutex);out=it->second->published;return true;}
namespace {thread_local IFileDialog* selectionDialog=nullptr;thread_local Session* selectionSession=nullptr;
void CALLBACK cancelSelection(HWND,UINT,UINT_PTR,DWORD){if(selectionDialog&&selectionSession&&selectionSession->cancelled)selectionDialog->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));}}
int MediaEngine::chooseLocalFile(WiMedia id,uint32_t edge){
    if(edge<32||edge>4096)return WI_MEDIA_INVALID;
    if(impl->picking.exchange(true))return WI_MEDIA_BUSY;
    if(impl->picker.joinable())impl->picker.join();
    auto s=std::make_shared<Session>();s->needsLoad=false;s->maxEdge=edge;s->state=WI_MEDIA_LOADING;s->published.info.state=WI_MEDIA_LOADING;s->published.info.generation=1;s->published.revision=1;s->published.eventSequence=1;s->published.event="selection.pending";
    {std::lock_guard lock(impl->mutex);if(impl->sessions.size()+impl->retired.size()>=8){impl->picking=false;return WI_MEDIA_LIMIT;}impl->sessions.emplace(id,s);}
    auto host=impl.get();impl->picker=std::thread([host,s]{
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);ComPtr<IFileOpenDialog> dialog;HRESULT rc=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog));fs::path file;
        if(SUCCEEDED(rc)&&!s->cancelled){COMDLG_FILTERSPEC types[]={{L"Media skins (PNG, JPEG, BMP, GIF, MP4)",L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.mp4"}};dialog->SetTitle(L"WinIsland：为插件选择本地媒体（不会移动或删除原文件）");dialog->SetFileTypes(1,types);dialog->SetOptions(FOS_FILEMUSTEXIST|FOS_FORCEFILESYSTEM|FOS_NOCHANGEDIR);
            selectionDialog=dialog.Get();selectionSession=s.get();auto timer=SetTimer(nullptr,0,50,cancelSelection);rc=dialog->Show(nullptr);KillTimer(nullptr,timer);selectionDialog=nullptr;selectionSession=nullptr;
            if(SUCCEEDED(rc)){ComPtr<IShellItem> item;PWSTR path=nullptr;if(SUCCEEDED(dialog->GetResult(&item))&&SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH,&path))){file=path;CoTaskMemFree(path);}else rc=E_FAIL;}
        }
        {std::lock_guard lock(s->mutex);if(!s->cancelled){if(SUCCEEDED(rc)&&!file.empty()){s->root=file.parent_path();s->relative=file.filename();s->needsLoad=true;}else{s->published.info.state=WI_MEDIA_ERROR;strcpy_s(s->published.info.error,"Local media selection cancelled or failed");s->published.event="selection.cancelled";++s->published.revision;++s->published.eventSequence;}}}
        dialog.Reset();CoUninitialize();{std::lock_guard lock(host->mutex);host->wake=true;}host->cv.notify_one();host->picking=false;
    });return WI_MEDIA_OK;
}
void MediaEngine::release(WiMedia id){std::lock_guard lock(impl->mutex);auto it=impl->sessions.find(id);if(it==impl->sessions.end())return;it->second->cancelled=true;impl->retired.push_back(it->second);impl->sessions.erase(it);impl->wake=true;impl->cv.notify_one();}
}
