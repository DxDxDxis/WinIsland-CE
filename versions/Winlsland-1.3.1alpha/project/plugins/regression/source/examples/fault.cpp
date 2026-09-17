#include "../src/mod_api.h"
#include <windows.h>
static IWinIslandMod api;
static int fail(void*,const char*) {RaiseException(0xE0000123,0,0,nullptr);return 0;}
static void destroy(void*){}
extern "C" WINISLAND_MOD_API uint32_t WinIsland_ModAbi(){return WINISLAND_MOD_ABI;}
extern "C" WINISLAND_MOD_API IWinIslandMod* WinIsland_CreateMod(const WinIslandHostApi*){api={sizeof(api),WINISLAND_MOD_ABI,nullptr,nullptr,fail,nullptr,nullptr,destroy};return &api;}

