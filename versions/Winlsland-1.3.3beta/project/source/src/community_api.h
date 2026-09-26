#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define WI_COMMUNITY_INTERFACE "winisland.community"
#define WI_COMMUNITY_ABI 1u
/* All strings UTF-8; buffers borrowed during calls. No C++ types/exceptions across DLLs.
   Registry mutation/acquire/release/subscription/task creation: loader thread.
   invoke/publish/path: loader or THIS owner's managed task. Other threads: no SDK calls.
   A reference is an opaque host handle, NEVER a provider function pointer. */
enum WiExtResult { WI_EXT_OK=0, WI_EXT_INVALID=-200, WI_EXT_VERSION=-201,
 WI_EXT_NOT_FOUND=-202, WI_EXT_STOPPED=-203, WI_EXT_THREAD=-204,
 WI_EXT_BUSY=-205, WI_EXT_LIMIT=-206, WI_EXT_FAULT=-207,
 WI_EXT_CANCELLED=-208, WI_EXT_UNSUPPORTED=-209, WI_EXT_NOT_READY=-210 };
enum WiServiceState { WI_SERVICE_REGISTERED=1, WI_SERVICE_READY=2,
 WI_SERVICE_STOPPING=3, WI_SERVICE_FAILED=4, WI_SERVICE_REMOVED=5 };
enum WiServiceThread { WI_SERVICE_LOADER=1, WI_SERVICE_CONCURRENT=2 };
#define WI_SERVICE_IMAGE_DECODER 1ull
#define WI_IMAGE_PROBE 1u
#define WI_IMAGE_DECODE 2u
typedef uint64_t WiService, WiServiceRef, WiJob, WiSubscription;
typedef struct WiServiceInfo {
 uint32_t size,version; WiService handle;
 char id[160],provider[81]; uint32_t interfaceVersion,interfaceSize;
 uint64_t capabilities; uint32_t state,thread;
} WiServiceInfo;
typedef int (*WiServiceInvoke)(void*,uint32_t method,const void* input,uint32_t inputBytes,
 void* output,uint32_t capacity,uint32_t* written);
typedef struct WiServiceTable { uint32_t size,version; void* context; WiServiceInvoke invoke; } WiServiceTable;
typedef struct WiServiceDefinition {
 uint32_t size,version; const char* id; uint32_t interfaceVersion;
 uint64_t capabilities; uint32_t thread; const WiServiceTable* table;
} WiServiceDefinition;
typedef struct WiServiceQuery {
 uint32_t size,version; const char* id; const char* provider; /* empty = deterministic selection */
 uint32_t minVersion,maxVersion; uint64_t requiredCapabilities;
} WiServiceQuery;
typedef struct WiCommunityEvent {
 uint32_t size,version; uint64_t sequence; const char* source; const char* topic;
 const void* data; uint32_t bytes; /* copied payload, valid only during callback */
} WiCommunityEvent;
typedef int (*WiCommunityListener)(void*,const WiCommunityEvent*);
typedef struct WiJobContext {
 uint32_t size,version; void* context;
 int (*cancelled)(void*); int (*result)(void*,const void*,uint32_t);
 int (*progress)(void*,uint32_t perMille);
} WiJobContext;
typedef int (*WiJobWork)(void*,const WiJobContext*); /* background thread; cooperative cancellation */
typedef void (*WiJobDone)(void*,WiJob,int,const void*,uint32_t); /* loader; suppressed on owner stop */
typedef struct WiJobDefinition { uint32_t size,version; void* context; WiJobWork work; WiJobDone done; } WiJobDefinition;
typedef struct WiJobInfo { uint32_t size,version,state,progress; int32_t result; } WiJobInfo;
/* Third-party libraries may have threads. stop requests cancellation without blocking;
   joined returns 1 ONLY after the library has joined all its threads and callbacks.
   Host polls on loader thread and pins the DLL until joined. Not a sandbox. */
typedef struct WiExternalLifetime {
 uint32_t size,version; void* context; void (*stop)(void*); int (*joined)(void*);
} WiExternalLifetime;
enum WiPluginPath { WI_PATH_DATA=1,WI_PATH_PACKAGE=2 };
typedef struct WiImageDecodeRequest {
 uint32_t size,version,maxEdge; const char* path;
 const uint8_t* prefix; uint32_t prefixBytes;
 void* context; int (*cancelled)(void*);
 /* Top-down premultiplied BGRA. Host copies before return. One frame, <=4096^2. */
 int (*frame)(void*,uint32_t width,uint32_t height,const void*,uint32_t bytes);
} WiImageDecodeRequest;
typedef struct WiCommunityApi {
 uint32_t size,version; void* context;
 int (*registerService)(void*,const WiServiceDefinition*,WiService*);
 int (*setServiceState)(void*,WiService,uint32_t);
 int (*unregisterService)(void*,WiService);
 int (*enumerate)(void*,WiServiceInfo*,uint32_t,uint32_t*);
 int (*acquire)(void*,const WiServiceQuery*,WiServiceRef*,WiServiceInfo*);
 int (*release)(void*,WiServiceRef);
 int (*invoke)(void*,WiServiceRef,uint32_t,const void*,uint32_t,void*,uint32_t,uint32_t*);
 /* service.changed payload = WiServiceInfo; other topics must start with provider ID + '.' */
 int (*subscribe)(void*,const char* topic,WiCommunityListener,void*,WiSubscription*);
 int (*unsubscribe)(void*,WiSubscription);
 int (*publish)(void*,const char* topic,const void*,uint32_t);
 int (*startJob)(void*,const WiJobDefinition*,WiJob*);
 int (*cancelJob)(void*,WiJob);
 int (*jobInfo)(void*,WiJob,WiJobInfo*);
 int (*manageLifetime)(void*,const WiExternalLifetime*);
 int (*path)(void*,uint32_t kind,const char* relative,char*,uint32_t,uint32_t*);
} WiCommunityApi;
#ifdef __cplusplus
}
#endif
