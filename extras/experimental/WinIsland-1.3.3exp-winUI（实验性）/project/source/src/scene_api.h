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

