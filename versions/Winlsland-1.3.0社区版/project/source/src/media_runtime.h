#pragma once
#include "core.h"
#include "media_api.h"
namespace wi {
// Decoder thread owns COM objects. Snapshots contain immutable host data only.
struct MediaFrame {
    uint32_t width=0,height=0;
    uint64_t sequence=0;
    std::vector<uint8_t> bgra;
};
struct MediaSnapshot {
    WiMediaInfo info{sizeof(WiMediaInfo),WI_MEDIA_ABI};
    uint64_t generation=1,revision=0,eventSequence=0;
    std::string event;
    std::shared_ptr<const MediaFrame> frame;
};
class MediaEngine {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    MediaEngine();~MediaEngine();
    int load(WiMedia,const fs::path& root,const fs::path& relative,uint32_t maxEdge=1920);
    int command(WiMedia,uint32_t command,double value=0);
    int replace(WiMedia,const fs::path& root,const fs::path& relative);
    int chooseLocalFile(WiMedia,uint32_t maxEdge=1920);
    bool snapshot(WiMedia,MediaSnapshot&) const;
    void release(WiMedia);
    static bool selfTest(const fs::path& assets,const fs::path& report);
};
enum MediaCommand { MediaPlay=1,MediaPause,MediaStop,MediaSeek,MediaLoop,MediaRate,MediaMuted,MediaVolume,MediaHidden,MediaHiddenPolicy };
}
