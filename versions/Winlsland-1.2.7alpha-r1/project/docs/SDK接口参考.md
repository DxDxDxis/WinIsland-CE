# SDK 精确接口参考 — 1.2.7alpha-r1

以下为实际参与构建的头文件逐字副本。生命周期/场景/设置/动画说明见插件开发者文档与开放场景API；媒体每个函数的线程、单位、默认、异步完成、错误和卸载契约见媒体资源与皮肤扩展。

## mod_api.h
```c
#pragma once
#include <stdint.h>
#include "scene_api.h"
#include "media_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WINISLAND_MOD_ABI 0x00010002u
#define WINISLAND_MOD_API __declspec(dllexport)
// All strings are UTF-8, borrowed for the duration of a call and copied by host.
// All callbacks run serially on the loader worker. No plugin-owned threads.
typedef int (*WinIslandCallback)(void* context, const char* value);
enum WinIslandResourceKind { WI_SETTING=1, WI_BUTTON=2, WI_LAYER=3, WI_REPLACE=4, WI_TIMER=5, WI_EVENT=6, WI_TASK=7, WI_HOOK=8, WI_ANIMATION=9 };
typedef struct WinIslandResource {
    uint32_t size, version, kind;
    const char *key, *label, *value;
    WinIslandCallback callback;
    void* context;
    uint32_t intervalMs; // timer >=100ms
} WinIslandResource;
typedef struct WinIslandAnimationDefinition {
    uint32_t size, version;
    const char *key, *script;
    double duration;
    int32_t priority;
} WinIslandAnimationDefinition;
typedef struct WinIslandAnimationFrame {
    uint32_t size, version;
    uint64_t sequence;
    float x,y,width,height,radius,opacity,scale,offsetX,offsetY,contentOpacity,lyricOpacity,noticeOpacity;
} WinIslandAnimationFrame;
typedef struct WinIslandHostApi {
    uint32_t size, version;
    void* context; // capability bound to THIS plugin; no caller-supplied owner
    void (*log)(void*, const char*);
    uint64_t (*add)(void*, const WinIslandResource*);
    int (*remove)(void*, uint64_t);
    int (*getSetting)(void*, const char*, char*, uint32_t); // writes caller buffer
    uint32_t capabilities;
    uint32_t (*animationCapabilities)(void*);
    uint64_t (*registerAnimation)(void*, const WinIslandAnimationDefinition*);
    int (*animationCommand)(void*, uint64_t, uint32_t, const WinIslandAnimationFrame*, WinIslandAnimationFrame*);
    // Optional tail: check size >= offsetof(queryInterface)+sizeof(queryInterface).
    int (*queryInterface)(void*, const char* name, uint32_t version, void* out, uint32_t bytes);
} WinIslandHostApi;
typedef struct IWinIslandMod {
    uint32_t size, version;
    void* context;
    WinIslandCallback onLoad, onEnable, onDisable, onUnload;
    void (*destroy)(void*); // allocated and released by same DLL CRT
} IWinIslandMod;
typedef uint32_t (*WinIslandModAbi)(void);
typedef IWinIslandMod* (*WinIslandCreateMod)(const WinIslandHostApi*);
// Exports: WinIsland_ModAbi() returns WINISLAND_MOD_ABI;
// WinIsland_CreateMod(host) returns table valid until destroy(context).
#ifdef __cplusplus
}
#endif


```

## scene_api.h
```c
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Independent extension version. Lifecycle ABI remains 0x00010002.
#define WI_SCENE_ABI 1u
#define WI_SCENE_INTERFACE "winisland.scene"
typedef uint64_t WiElement;
enum WiResult { WI_OK=0, WI_INVALID=-1, WI_NOT_FOUND=-2, WI_VERSION=-3,
    WI_THREAD=-4, WI_STOPPED=-5, WI_LIMIT=-6, WI_UNSUPPORTED=-7, WI_CONFLICT=-8 };
enum WiElementType { WI_CONTAINER=1, WI_TEXT, WI_IMAGE, WI_ICON, WI_SCENE_BUTTON,
    WI_SHAPE, WI_PROGRESS, WI_SLIDER, WI_INPUT, WI_CUSTOM_DRAW };
enum WiProperty {
    WI_RECT=1, WI_VISIBLE, WI_OPACITY, WI_SCALE, WI_ROTATION, WI_RADIUS,
    WI_BACKGROUND, WI_BORDER_COLOR, WI_BORDER_WIDTH, WI_TEXT_COLOR, WI_FONT_SIZE,
    WI_FONT_WEIGHT, WI_TEXT_VALUE, WI_FONT_FAMILY, WI_ALIGN, WI_Z_ORDER,
    WI_RECEIVE_INPUT, WI_BLOCK_INPUT, WI_CLIP, WI_PARENT, WI_VALUE,
    WI_MIN_SIZE, WI_MAX_SIZE, WI_ANCHOR, WI_MARGIN, WI_PADDING, WI_LAYOUT,
    WI_SHADOW, WI_CUSTOM_DATA, WI_ELEMENT_TYPE, WI_SIZE_MODE, WI_POSITION,
    WI_EXPANDED, WI_ENABLED, WI_MEDIA_FIT, WI_MEDIA_CROP
};
enum WiMediaFit { WI_FIT_STRETCH=0,WI_FIT_CONTAIN=1,WI_FIT_COVER=2 };
enum WiSizeMode { WI_HOST_MANAGED=0, WI_PLUGIN_MANAGED=1, WI_OVERLAY=2, WI_INTRINSIC=3 };
enum WiSceneFlags { WI_IDLE=1, WI_MUSIC=2, WI_PLAYING=4, WI_LYRIC=8,
    WI_NOTICE=16, WI_IS_EXPANDED=32, WI_SETTINGS_OPEN=64, WI_REDUCED_MOTION=128,
    WI_SOFTWARE_RENDERER=256 };
// All geometry is final island-local DIP (not multiplied by MusicScale).
// Colors use straight RGBA in number[0..3]. No caller-owned memory retained.
typedef struct WiPropertyValue {
    uint32_t size, version;
    double number[4];
    uint64_t handle; // parent element for WI_PARENT
    char text[1024]; // UTF-8, terminated; WI_TEXT_VALUE/FONT_FAMILY/CUSTOM_DATA
} WiPropertyValue;
typedef struct WiElementInfo {
    uint32_t size, version, type, visible;
    WiElement id, parent;
    char owner[96], key[128];
    float x,y,width,height,opacity;
    int32_t z;
} WiElementInfo;
typedef struct WiSceneSnapshot {
    uint32_t size, version, flags, dpi;
    uint64_t generation;
    double time;
    float x,y,width,height;
    WiElement focus;
    uint32_t elementCount, sizeMode;
} WiSceneSnapshot;
enum WiInputKind { WI_POINTER_ENTER=1, WI_POINTER_LEAVE, WI_POINTER_MOVE,
    WI_POINTER_DOWN, WI_POINTER_UP, WI_POINTER_CLICK, WI_POINTER_WHEEL,
    WI_KEY_DOWN, WI_KEY_UP, WI_FOCUS_GAINED, WI_FOCUS_LOST, WI_TEXT_INPUT,
    WI_DRAG_START,WI_DRAG_UPDATE,WI_DRAG_END,WI_DRAG_CANCEL };
typedef struct WiInputEvent {
    uint32_t size, version, kind, key;
    WiElement target;
    uint64_t generation;
    double time;
    float x,y,wheel;
    uint32_t modifiers;
} WiInputEvent;
typedef int (*WiInputCallback)(void*, const WiInputEvent*); // 0 unhandled, 1 handled
enum WiDrawKind { WI_DRAW_RECT=1, WI_DRAW_ELLIPSE, WI_DRAW_LINE, WI_DRAW_TEXT, WI_DRAW_TRIANGLE, WI_PATH_BEGIN, WI_PATH_LINE, WI_PATH_CUBIC, WI_PATH_END };
typedef struct WiDrawCommand {
    uint32_t size, version, kind, reserved;
    float x,y,width,height,x2,y2,radius,stroke,fontSize;
    float color[4];
    char text[512];
} WiDrawCommand;
typedef struct WiDrawContext {
    uint32_t size,version;
    WiSceneSnapshot scene;
    WiElementInfo element;
    WiInputEvent lastInput;
    float clip[4],background[4],foreground[4];
} WiDrawContext;
typedef int (*WiDrawCallback)(void*,const WiDrawContext*);
typedef struct WiSceneApi {
    uint32_t size, version;
    void* context; // bound to host Record; never supply an owner ID
    int (*snapshot)(void*, WiSceneSnapshot*);
    int (*enumerate)(void*, uint64_t generation, WiElement parent, uint32_t type,
                     WiElementInfo* out, uint32_t capacity, uint32_t* count);
    int (*find)(void*, const char* key, WiElement*);
    int (*read)(void*, WiElement, uint32_t property, WiPropertyValue*);
    int (*create)(void*, uint32_t type, const char* key, WiElement parent, WiElement*);
    int (*clone)(void*, WiElement source, const char* key, WiElement*);
    int (*set)(void*, WiElement, uint32_t property, const WiPropertyValue*, int32_t priority);
    int (*clear)(void*, WiElement, uint32_t property); // remove this owner's property layer
    int (*remove)(void*, WiElement); // own node: destroy; host/other: reversible hide
    int (*listen)(void*, WiElement, WiInputCallback, void* callbackContext);
    int (*draw)(void*, WiElement, const WiDrawCommand*, uint32_t count);
    int (*bitmap)(void*, WiElement, uint32_t width, uint32_t height, const void* bgra, uint32_t bytes);
    int (*commit)(void*); // publish copied scene changes; no DLL code on rendering thread
    int (*visible)(void*, uint64_t generation, WiElementInfo* out, uint32_t capacity, uint32_t* count);
    int (*onDraw)(void*,WiElement,WiDrawCallback,void* callbackContext);
    int (*requestDraw)(void*,WiElement);
    int (*reset)(void*); // revoke only this owner's edits/elements/input/draw data
} WiSceneApi;
enum WiSettingType { WI_SETTING_TEXT=1, WI_SETTING_NUMBER, WI_SETTING_SWITCH, WI_SETTING_CHOICE };
typedef struct WiSettingDefinition {
    uint32_t size, version, type, reserved;
    const char *key,*label,*defaultValue,*choices; // choice values separated by LF
    double minimum,maximum;
} WiSettingDefinition;
typedef struct WiSettingsApi {
    uint32_t size, version;
    void* context;
    int (*define)(void*,const WiSettingDefinition*,uint64_t*);
    int (*read)(void*,const char* key,char* out,uint32_t bytes);
    int (*write)(void*,const char* key,const char* value);
} WiSettingsApi;
#ifdef __cplusplus
}
#endif


```

## media_api.h
```c
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

```
