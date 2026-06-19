// cl.exe /O2 /LD /GS- /GR- /EHsc- dreamland.cpp /link /NODEFAULTLIB /ENTRY:DllMain user32.lib gdi32.lib kernel32.lib wininet.lib
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#include <windows.h>
#include <wininet.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "wininet.lib") 
extern "C" const int _fltused = 0;
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    return TRUE;
}
extern "C" {
#pragma function(memset)
    void* __cdecl memset(void* dest, int c, size_t count) {
        unsigned char* p = (unsigned char*)dest;
        while (count--) *p++ = (unsigned char)c;
        return dest;
    }
#pragma function(memcpy)
    void* __cdecl memcpy(void* dest, const void* src, size_t count) {
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        while (count--) *d++ = *s++;
        return dest;
    }
}
LumeHostAPI* g_api = nullptr;
struct PortalData {
    HTP_NodeHandle node;
    char url[256];
    char content[4096];
    int state;
    bool is_expanding;
    float expand_progress;
    bool nav_triggered;
};
PortalData g_portals[4];
DWORD g_animTick = 0;
UINT_PTR g_timerId = 0;
VOID CALLBACK PortalAnimTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    g_animTick++;
    if (g_api) g_api->invalidate_content();
}
DWORD WINAPI FetchThread(LPVOID param) {
    PortalData* p = (PortalData*)param;
    HINTERNET hInt = InternetOpenA("Celerity/Dreamland", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (hInt) {
        HINTERNET hUrl = InternetOpenUrlA(hInt, p->url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
        if (hUrl) {
            DWORD bytesRead = 0;
            InternetReadFile(hUrl, p->content, 4095, &bytesRead);
            p->content[bytesRead] = '\0';
            InternetCloseHandle(hUrl);
        } else {
            lstrcpyA(p->content, "CONNECTION FAILED TO THE OTHER DIMENSION.");
        }
        InternetCloseHandle(hInt);
    }
    p->state = 2; 
    return 0;
}
int portal_est_w(HTP_NodeHandle node, int max_width) {
    if (!g_api) return 300;
    return g_api->get_node_prop_int(node, "width", 300);
}
int portal_est_h(HTP_NodeHandle node) {
    if (!g_api) return 300;
    return g_api->get_node_prop_int(node, "height", 300);
}
void portal_render(HDC hdc, HTP_NodeHandle node, int x, int y, int width, int height, int scroll_y) {
    if (!g_api) return;
    PortalData* portal = nullptr;
    for (int i = 0; i < 4; i++) {
        if (g_portals[i].node == node) { portal = &g_portals[i]; break; }
        if (g_portals[i].node == nullptr) {
            g_portals[i].node = node;
            lstrcpyA(g_portals[i].url, g_api->get_node_prop(node, "dest", "about:home"));
            g_portals[i].state = 0;
            g_portals[i].is_expanding = false;
            g_portals[i].expand_progress = 0.0f;
            g_portals[i].nav_triggered = false;
            portal = &g_portals[i];
            break;
        }
    }
    if (portal && portal->is_expanding) {
        portal->expand_progress += 0.04f;
        if (portal->expand_progress >= 1.0f) {
            portal->expand_progress = 1.0f;
            if (!portal->nav_triggered) {
                portal->nav_triggered = true;
                g_api->navigate_to(portal->url);
            }
        }
        float t = portal->expand_progress;
        float eased = t * t * t;
        int cx = x + width / 2;
        int cy = y + height / 2;
        int max_size = 4000;
        int cur_size = width + (int)((max_size - width) * eased);
        int ex = cx - cur_size / 2;
        int ey = cy - cur_size / 2;
        HBRUSH bgBr = CreateSolidBrush(RGB(10, 10, 15)); 
        int frameThickness = 2 + (int)(t * 100);
        HPEN pn = CreatePen(PS_SOLID, frameThickness, RGB(200 - (int)(t * 150), 0, 255));
        HGDIOBJ oldBr = SelectObject(hdc, bgBr);
        HGDIOBJ oldPn = SelectObject(hdc, pn);
        Rectangle(hdc, ex, ey, ex + cur_size, ey + cur_size);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPn);
        DeleteObject(bgBr);
        DeleteObject(pn);
        return;
    }
    if (portal && portal->state == 0) {
        portal->state = 1;
        lstrcpyA(portal->content, "ESTABLISHING CONNECTION...");
        CreateThread(NULL, 0, FetchThread, portal, 0, NULL);
    }
    int glowIntensity = 100 + (g_animTick % 30) * 5;
    if (glowIntensity > 200) glowIntensity = 200 - (glowIntensity - 200);
    HBRUSH bgBr = CreateSolidBrush(RGB(10, 10, 15)); 
    HPEN pn = CreatePen(PS_SOLID, 2, RGB(glowIntensity, 0, 255));
    HGDIOBJ oldBr = SelectObject(hdc, bgBr);
    HGDIOBJ oldPn = SelectObject(hdc, pn);
    Rectangle(hdc, x, y, x + width, y + height);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(bgBr);
    DeleteObject(pn);
    HRGN clipRgn = CreateRectRgn(x + 5, y + 5, x + width - 5, y + height - 5);
    SelectClipRgn(hdc, clipRgn);
    SetTextColor(hdc, RGB(0, 255, 150)); 
    SetBkMode(hdc, TRANSPARENT);
    if (portal) {
        int textOffset_Y = y + 10 - ((g_animTick * 2) % 1000);
        if (portal->state == 1) textOffset_Y = y + 10;
        RECT r = {x + 10, textOffset_Y, x + width - 10, 10000}; 
        DrawTextA(hdc, portal->content, -1, &r, DT_WORDBREAK);
    }
    SelectClipRgn(hdc, NULL);
    DeleteObject(clipRgn);
}
void portal_click(HTP_NodeHandle node, int local_x, int local_y, int button) {
    for (int i = 0; i < 4; i++) {
        if (g_portals[i].node == node) {
            g_portals[i].is_expanding = true;
            g_portals[i].expand_progress = 0.0f;
            g_portals[i].nav_triggered = false;
            break;
        }
    }
}
static unsigned int g_seed = 12345;
unsigned int my_rand() {
    g_seed = (1103515245 * g_seed + 12345);
    return (g_seed / 65536) % 32768;
}
void scramble_colors_recursive(HTP_NodeHandle node) {
    if (!node || !g_api) return;
    int r = my_rand() % 100 + 155; 
    int g = my_rand() % 100;       
    int b = my_rand() % 100 + 155; 
    char colorBuf[32];
    wsprintfA(colorBuf, "#%02x%02x%02x", r, g, b);
    g_api->set_node_prop(node, "color", colorBuf);
    int children = g_api->get_node_children_count(node);
    for (int i = 0; i < children; i++) scramble_colors_recursive(g_api->get_node_child(node, i));
}
static int l_lucid_mode(lua_State* L) {
    if (!g_api) return 0;
    HTP_NodeHandle root = g_api->get_dom_root();
    if (root) {
        g_seed = GetTickCount(); 
        scramble_colors_recursive(root);
        g_api->invalidate_content();
    }
    return 0;
}
extern "C" {
    __declspec(dllexport) void __cdecl lume_plugin_init(lua_State* L, LumeHostAPI* api) {
        if (!api) return;
        g_api = api;
        memset(g_portals, 0, sizeof(g_portals));
        HWND hwnd = g_api->get_main_hwnd();
        g_timerId = SetTimer(hwnd, 8888, 30, PortalAnimTimer);
        CustomTagHandler portal;
        memset(&portal, 0, sizeof(portal));
        portal.tag_name = "portal";
        portal.estimate_width = portal_est_w;
        portal.estimate_height = portal_est_h;
        portal.render = portal_render;
        portal.on_click = portal_click;
        g_api->register_tag(portal);
        g_api->p_lua_pushcclosure(L, l_lucid_mode, 0);
        g_api->p_lua_setglobal(L, "lucid_mode");
    }
    __declspec(dllexport) void __cdecl lume_plugin_shutdown() {
        if (g_api && g_timerId) {
            KillTimer(g_api->get_main_hwnd(), g_timerId);
        }
        memset(g_portals, 0, sizeof(g_portals));
        g_api = nullptr;
    }
}