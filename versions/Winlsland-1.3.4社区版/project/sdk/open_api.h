#pragma once
#include "community_api.h"
#ifdef __cplusplus
extern "C" {
#endif
#define WI_OPEN_INTERFACE "winisland.open"
#define WI_OPEN_ABI 1u
typedef uint64_t WiView,WiProcess,WiHook;
enum WiViewKind { WI_VIEW_PIXELS=1,WI_VIEW_NATIVE=2 };
enum WiViewEventKind { WI_VIEW_CREATE=1,WI_VIEW_LAYOUT,WI_VIEW_INPUT,WI_VIEW_IME,
 WI_VIEW_FOCUS,WI_VIEW_HIDDEN,WI_VIEW_DESTROY,WI_VIEW_DEVICE_LOST,WI_VIEW_FILES,WI_VIEW_DRAG_RESULT };
/* Views are owned by the host STA UI thread. Callbacks run on that thread, NOT the
 loader. No old SDK calls there. Native child windows inherit real Windows input/IME.
 Offscreen input contains Windows message/key/local pixel coordinates; text is UTF-8.
 CREATE: nativeChild is output, set to a child HWND created on the supplied parent.
 DESTROY is delivered exactly once, even when closing before CREATE or CREATE fails.
 Stop callbacks/close controllers before returning; DLL remains pinned until all
 registered external lifetime protocols also acknowledge joined. Native v1 is a
 rectangular overlay ABOVE the scene, not an arbitrarily composited child surface.
 Pixel v1 clips (does not stretch) the submitted frame to current layout bounds and
 applies the node's radius/opacity; submit a new physical-pixel frame on LAYOUT.
 Hidden views receive no painting; plugin must suspend its own frame production.
 Device loss is queued back to the view STA; recreate resources before resubmitting. */
typedef struct WiViewEvent {
 uint32_t size,version,kind,dpi;WiView view;uintptr_t parent,nativeChild;
 int32_t x,y,width,height;uint32_t message;uint64_t wparam;int64_t lparam;
 const char* text; /* IME composition/result or LF-separated file paths, borrowed */
 uint32_t flags; /* IME: 1=result; focus:1=gained; drag: DROPEFFECT_COPY or 0 */
} WiViewEvent;
typedef int (*WiViewCallback)(void*,WiViewEvent*);
typedef struct WiViewDefinition {
 uint32_t size,version,kind,flags; /* flags bit0 receives external files */
 uint64_t element; /* own scene element, layout is synchronized in island DIP */
 void* context;WiViewCallback callback;
} WiViewDefinition;
typedef struct WiViewFrame {
 uint32_t size,version,width,height,stride;const void* bgra;uint32_t bytes;
} WiViewFrame;
/* D3D11 legacy shared texture, B8G8R8A8_UNORM, SHARED_KEYEDMUTEX, same adapter.
 Producer releases acquireKey; host acquires/copies/releases releaseKey. Producer must
 keep HANDLE valid until submission returns. Host opens a COM reference synchronously.
 Host uses a bounded staging copy into immutable PBGRA for its software-compatible
 compositing path; this v1 is NOT a zero-copy GPU compositor. */
typedef struct WiSharedTexture {
 uint32_t size,version;uintptr_t handle;uint64_t acquireKey,releaseKey;
} WiSharedTexture;
typedef struct WiProcessDefinition {
 uint32_t size,version;const char* executable; /* package-relative, .exe only */
 const char* arguments;uint32_t shutdownTimeoutMs; /* 100..10000 */
} WiProcessDefinition;
typedef struct WiProcessInfo {uint32_t size,version,state,exitCode,pid;uint64_t received,dropped;} WiProcessInfo;
/* Child stdin/stdout are PRIVATE inherited pipes, no globally named endpoint. Framing:
 uint32 little-endian payload length followed by bytes (<=1MiB); length=0 requests
 cooperative shutdown. stdout is protocol only. stderr goes to owner helper log.
 Working directory and APPDATA/LOCALAPPDATA/TEMP/TMP are owner data/helper. Host job kills descendants on
 crash/timeout. This isolates process memory, NOT filesystem/user permissions. */
enum WiHostTopic { WI_HOST_MUSIC=1,WI_HOST_NOTICE=2,WI_HOST_ACTION=3,WI_HOST_TELEMETRY=4 };
enum WiHookResult { WI_HOOK_PASS=0,WI_HOOK_REPLACE=1,WI_HOOK_CONSUME=2 };
typedef struct WiHostPacket {
 uint32_t size,version,topic,flags;uint64_t sequence;
 char source[81],title[1024],detail[2048],extra[512];
 double number[8]; /* MUSIC: position,duration,rate; ACTION: command 1..4;
 TELEMETRY: fps,ping. flags MUSIC bit0 playing bit1 prev bit2 toggle bit3 next */
} WiHostPacket;
typedef int (*WiHostHook)(void*,WiHostPacket*); /* loader thread; copied packet */
typedef struct WiHostHookDefinition {uint32_t size,version,topic;int32_t priority;void* context;WiHostHook callback;} WiHostHookDefinition;
typedef struct WiOpenApi {
 uint32_t size,version;void* context;
 int (*createView)(void*,const WiViewDefinition*,WiView*);
 int (*closeView)(void*,WiView);
 int (*submitFrame)(void*,WiView,const WiViewFrame*); /* copies; any owner thread while alive */
 int (*submitTexture)(void*,WiView,const WiSharedTexture*);
 int (*focusView)(void*,WiView,int32_t caretX,int32_t caretY); /* queue focus/IME caret */
 int (*dragFiles)(void*,WiView,const char* paths); /* LF absolute UTF8, copy only, async */
 int (*startProcess)(void*,const WiProcessDefinition*,WiProcess*); /* x64 executable */
 int (*sendProcess)(void*,WiProcess,const void*,uint32_t);
 int (*receiveProcess)(void*,WiProcess,void*,uint32_t,uint32_t*);
 int (*processInfo)(void*,WiProcess,WiProcessInfo*);
 int (*stopProcess)(void*,WiProcess);
 int (*registerHook)(void*,const WiHostHookDefinition*,WiHook*);
 int (*removeHook)(void*,WiHook);
 int (*submitHost)(void*,const WiHostPacket*); /* alternative data source / real command */
 int (*releaseProcess)(void*,WiProcess); /* only after process AND pipe workers exited */
 int (*viewBarrier)(void*,uint64_t* ticket);
 int (*viewBarrierReady)(void*,uint64_t ticket);
} WiOpenApi;
/* STA callback retirement: call viewBarrier BEFORE publishing pending==0/destroyed.
 The ticket becomes ready only after the STA dispatch batch actually returns. In a
 registered lifetime.joined require pending==0 AND viewBarrierReady(ticket)==OK.
 This closes the callback-epilogue gap; it does not cancel future third-party work.
 Stop/cancel all later callbacks first. Barrier is for THIS host view STA only;
 external threads still need real join. Both methods are allowed during cleanup. */
/* queryInterface/createView/registerHook/removeHook: loader thread. Remaining open
 methods may be used on a live owner's threads, including view callbacks; external
 threads MUST participate in manageLifetime. close/stop/release support cleanup.
 Never block a callback. OLE dragFiles is queued and returns before drag completion;
 its result arrives through WI_VIEW_DRAG_RESULT. It requires a real pressed pointer.
 IPC receive is nonblocking; NOT_READY is not an error. Full buffers return LIMIT.
 Hooks: priority descending, provider ID ascending, registration order within owner.
 PASS ignores edits, REPLACE commits a validated packet, CONSUME stops the chain.
 source and sequence cannot be forged by hooks; submitHost binds source to owner.
 Action topic currently accepts media commands 1=previous,2=toggle,3=next,4=repeat.
 Hook packets are bounded transient work, not a durable command/event journal. */

#define WI_SERVICE_STREAM_DECODER 2ull
enum WiStreamMethod {WI_STREAM_PROBE=100,WI_STREAM_OPEN=101,WI_STREAM_READ=102,WI_STREAM_SEEK=103,WI_STREAM_CLOSE=104};
typedef struct WiStreamInfo {
 uint32_t size,version,width,height,sampleRate,channels;double duration,frameRate;
 uint64_t session;uint32_t hasVideo,hasAudio;char codec[64];
} WiStreamInfo;
/* Provider calls sinks synchronously during READ; host copies, applies backpressure by
 issuing one bounded read at a time. Video PBGRA <=maxEdge, audio PCM s16 interleaved
 1/2 channels, 8..192kHz, max 0.25s per READ. timestamps seconds, monotonic per seek.
 READ returns OK with a sample, 1=end, BUSY=not yet available. CLOSE is always called
 even after owner stopping, while DLL is pinned. Do not retain request/sink pointers. */
typedef struct WiStreamRequest {
 uint32_t size,version,maxEdge;const char* path;uint64_t session;double position;
 void* context;int (*cancelled)(void*);
 int (*video)(void*,double,uint32_t,uint32_t,const void*,uint32_t);
 int (*audio)(void*,double,const int16_t*,uint32_t frames);
} WiStreamRequest;
#ifdef __cplusplus
}
#endif
