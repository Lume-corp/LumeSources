// cl /LD /O2 /GS- /GR- /EHa- sysmon.cpp /link /ENTRY:DllMain /NODEFAULTLIB kernel32.lib
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "lume_plugin.h"
static FILETIME prevSysIdle, prevSysKernel, prevSysUser;
LumeHostAPI* g_api = nullptr;
extern "C" {
    int _fltused = 0;
#pragma function(memset)
    void* __cdecl memset(void* dest, int c, size_t count) {
        unsigned char* p = (unsigned char*)dest;
        while (count--) *p++ = (unsigned char)c;
        return dest;
    }
}
static int api_get_cpu(lua_State* L) {
    FILETIME sysIdle, sysKernel, sysUser;
    double cpuLoad = 0.0;
    if (GetSystemTimes(&sysIdle, &sysKernel, &sysUser)) {
        ULARGE_INTEGER uIdle, uKernel, uUser;
        ULARGE_INTEGER uPrevIdle, uPrevKernel, uPrevUser;
        uIdle.LowPart = sysIdle.dwLowDateTime;
        uIdle.HighPart = sysIdle.dwHighDateTime;
        uKernel.LowPart = sysKernel.dwLowDateTime;
        uKernel.HighPart = sysKernel.dwHighDateTime;
        uUser.LowPart = sysUser.dwLowDateTime;
        uUser.HighPart = sysUser.dwHighDateTime;
        uPrevIdle.LowPart = prevSysIdle.dwLowDateTime;
        uPrevIdle.HighPart = prevSysIdle.dwHighDateTime;
        uPrevKernel.LowPart = prevSysKernel.dwLowDateTime;
        uPrevKernel.HighPart = prevSysKernel.dwHighDateTime;
        uPrevUser.LowPart = prevSysUser.dwLowDateTime;
        uPrevUser.HighPart = prevSysUser.dwHighDateTime;
        ULONGLONG idleDiff = uIdle.QuadPart - uPrevIdle.QuadPart;
        ULONGLONG kernelDiff = uKernel.QuadPart - uPrevKernel.QuadPart;
        ULONGLONG userDiff = uUser.QuadPart - uPrevUser.QuadPart;
        ULONGLONG totalSysDiff = kernelDiff + userDiff;
        unsigned int td = (unsigned int)totalSysDiff;
        unsigned int id = (unsigned int)idleDiff;
        if (td > 0) cpuLoad = (td - id) * 100.0 / td;
        prevSysIdle = sysIdle;
        prevSysKernel = sysKernel;
        prevSysUser = sysUser;
        if (cpuLoad < 0.0) cpuLoad = 0.0;
        if (cpuLoad > 100.0) cpuLoad = 100.0;
    }
    if (g_api) g_api->p_lua_pushnumber(L, cpuLoad);
    return 1;
}
static int api_get_ram(lua_State* L) {
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    double freeRamMB = 0.0;
    if (GlobalMemoryStatusEx(&memInfo)) {
        unsigned int mb = (unsigned int)(memInfo.ullAvailPhys >> 20);
        freeRamMB = (double)mb;
    }
    if (g_api) g_api->p_lua_pushnumber(L, freeRamMB);
    return 1;
}
extern "C" {
    __declspec(dllexport) void __cdecl lume_plugin_init(lua_State* L, LumeHostAPI* api) {
        g_api = api;
        GetSystemTimes(&prevSysIdle, &prevSysKernel, &prevSysUser);
        api->p_lua_pushcclosure(L, api_get_cpu, 0);
        api->p_lua_setglobal(L, "sysmon_get_cpu");
        api->p_lua_pushcclosure(L, api_get_ram, 0);
        api->p_lua_setglobal(L, "sysmon_get_ram");
    }
    __declspec(dllexport) void __cdecl lume_plugin_shutdown() {
        g_api = nullptr;
    }
}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    return TRUE;
}