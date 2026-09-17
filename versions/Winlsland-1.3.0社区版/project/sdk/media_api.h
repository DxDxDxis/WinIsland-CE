#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WI_MEDIA_INTERFACE "winisland.media"
#define WI_MEDIA_ABI 1u
typedef uint64_t WiMedia;
enum WiMediaResult { WI_MEDIA_OK=0, WI_MEDIA_INVALID=-1, WI_MEDIA_NOT_FOUND=-2, WI_MEDIA_VERSION=-3, WI_MEDIA_LIMIT=-4, WI_MEDIA_UNSUPPORTED=-5, WI_MEDIA_BUSY=-6, WI_MEDIA_WRONG_THREAD=-7, WI_MEDIA_OWNER_STOPPED=-8 };
enum WiMediaKind { WI_MEDIA_IMAGE=1, WI_MEDIA_ANIMATION=2, WI_MEDIA_VIDEO=3 };
enum WiMediaState { WI_MEDIA_LOADING=1, WI_MEDIA_READY=2, WI_MEDIA_PLAYING=3, WI_MEDIA_PAUSED=4, WI_MEDIA_STOPPED=5, WI_MEDIA_ENDED=6, WI_MEDIA_ERROR=7, WI_MEDIA_RELEASED=8 };
enum WiMediaFormat { WI_MEDIA_PNG=1,WI_MEDIA_JPEG=2,WI_MEDIA_BMP=4,WI_MEDIA_GIF=8,WI_MEDIA_H264=16 };
enum WiMediaHiddenPolicy { WI_MEDIA_PAUSE_HIDDEN=0,WI_MEDIA_CONTINUE_HIDDEN=1 };
enum WiMediaSourceKind { WI_MEDIA_PACKAGE=0,WI_MEDIA_PLUGIN_DATA=1 };
typedef struct WiMediaInfo {
    uint32_t size, version, kind, state;
    uint32_t width, height, frameCount;
    double duration, frameRate;
    uint32_t hasAlpha, hasAudio;
    char format[32], codec[64], error[256];
    uint32_t originalWidth, originalHeight;
    double position, rate, volume;
    uint64_t generation, frameSequence;
    uint32_t muted,loop, audioSupported;
    uint64_t audioSamplesSubmitted;
} WiMediaInfo;
typedef struct WiMediaCapabilities {
    uint32_t size, version, imageFormats, animationFormats, videoFormats;
    uint32_t hardwareDecode, softwareDecode, transparentVideo;
    char formats[512];
} WiMediaCapabilities;
typedef struct WiMediaApi {
    uint32_t size, version; void* context;
    int (*capabilities)(void*, WiMediaCapabilities*);
    int (*load)(void*, const char* relativePath, WiMedia*);
    int (*info)(void*, WiMedia, WiMediaInfo*);
    int (*bind)(void*, WiMedia, uint64_t element);
    int (*play)(void*, WiMedia); int (*pause)(void*, WiMedia); int (*stop)(void*, WiMedia);
    int (*seek)(void*, WiMedia, double seconds); int (*setLoop)(void*, WiMedia, uint32_t loop);
    int (*setRate)(void*, WiMedia, double rate); int (*setMuted)(void*, WiMedia, uint32_t muted);
    int (*release)(void*, WiMedia);
    int (*replaceSource)(void*, WiMedia, const char* relativePath);
    int (*unbind)(void*, WiMedia, uint64_t element);
    int (*setVolume)(void*, WiMedia, double volume);
    int (*setHiddenPolicy)(void*, WiMedia, uint32_t policy);
    int (*loadFrom)(void*, uint32_t sourceKind, const char* path, uint32_t maxDecodeEdge, WiMedia*);
    // Opens an asynchronous host-owned file chooser. Loading starts only after user selection.
    int (*chooseLocalFile)(void*, uint32_t maxDecodeEdge, WiMedia*);
} WiMediaApi;
#ifdef __cplusplus
}
#endif
