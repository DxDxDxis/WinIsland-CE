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

