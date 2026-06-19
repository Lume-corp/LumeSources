// cl.exe /O2 /LD lume_js_plugin.c quickjs.c libregexp.c libunicode.c cutils.c libbf.c /link user32.lib kernel32.lib
// https://github.com/c-smile/quickjs
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "quickjs.h"
static JSRuntime* g_rt = NULL;
static JSContext* g_ctx = NULL;
static LumeHostAPI* g_host = NULL;
HTP_NodeHandle find_by_id(HTP_NodeHandle root, const char* id) {
    if (!root || !g_host || !id) return NULL;
    const char* propId = g_host->get_node_prop(root, "id", "");
    if (propId && strcmp(propId, id) == 0) return root;
    int count = g_host->get_node_children_count(root);
    for (int i = 0; i < count; ++i) {
        HTP_NodeHandle res = find_by_id(g_host->get_node_child(root, i), id);
        if (res) return res;
    }
    return NULL;
}
static JSValue js_lume_alert(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *str = JS_ToCString(ctx, argv[0]);
    if (str) {
        if(g_host) g_host->alert(str);
        JS_FreeCString(ctx, str);
    }
    return JS_UNDEFINED;
}
static JSValue js_lume_setProp(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *id = JS_ToCString(ctx, argv[0]);
    const char *key = JS_ToCString(ctx, argv[1]);
    const char *val = JS_ToCString(ctx, argv[2]);
    if (id && key && val && g_host) {
        HTP_NodeHandle node = find_by_id(g_host->get_dom_root(), id);
        if (node) {
            g_host->set_node_prop(node, key, val);
            g_host->invalidate_content();
        }
    }
    if (id) JS_FreeCString(ctx, id);
    if (key) JS_FreeCString(ctx, key);
    if (val) JS_FreeCString(ctx, val);
    return JS_UNDEFINED;
}
static JSValue js_lume_getProp(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *id = JS_ToCString(ctx, argv[0]);
    const char *key = JS_ToCString(ctx, argv[1]);
    JSValue ret = JS_UNDEFINED;
    if (id && key && g_host) {
        HTP_NodeHandle node = find_by_id(g_host->get_dom_root(), id);
        if (node) {
            const char* propVal = g_host->get_node_prop(node, key, "");
            ret = JS_NewString(ctx, propVal ? propVal : "");
        }
    }
    if (id) JS_FreeCString(ctx, id);
    if (key) JS_FreeCString(ctx, key);
    return ret;
}
int init_js(void) {
    if (g_ctx) JS_FreeContext(g_ctx);
    if (g_rt) JS_FreeRuntime(g_rt);
    g_rt = JS_NewRuntime();
    if (!g_rt) return 0;
    g_ctx = JS_NewContext(g_rt);
    if (!g_ctx) return 0;
    JSValue global = JS_GetGlobalObject(g_ctx);
    JSValue lume = JS_NewObject(g_ctx);
    JS_SetPropertyStr(g_ctx, lume, "alert", JS_NewCFunction(g_ctx, js_lume_alert, "alert", 1));
    JS_SetPropertyStr(g_ctx, lume, "setProp", JS_NewCFunction(g_ctx, js_lume_setProp, "setProp", 3));
    JS_SetPropertyStr(g_ctx, lume, "getProp", JS_NewCFunction(g_ctx, js_lume_getProp, "getProp", 2));
    JS_SetPropertyStr(g_ctx, global, "Lume", lume);
    JSValue console = JS_NewObject(g_ctx);
    JS_SetPropertyStr(g_ctx, console, "log", JS_NewCFunction(g_ctx, js_lume_alert, "log", 1));
    JS_SetPropertyStr(g_ctx, global, "console", console);
    JS_FreeValue(g_ctx, global);
    return 1;
}
static void execute_js(const char* js_code) {
    if (!js_code || !js_code[0] || !g_ctx) return;
    JSValue val = JS_Eval(g_ctx, js_code, strlen(js_code), "<script>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(g_ctx);
        const char* err = JS_ToCString(g_ctx, exc);
        char buf[1024];
        snprintf(buf, sizeof(buf), "JavaScript Error:\n%s", err ? err : "Unknown");
        if (g_host) g_host->alert(buf);
        
        JS_FreeCString(g_ctx, err);
        JS_FreeValue(g_ctx, exc);
    }
    JS_FreeValue(g_ctx, val);
}
static void on_lume_reset(void) {
    init_js();
}
__declspec(dllexport) int lume_plugin_init(lua_State* L, LumeHostAPI* host) {
    if (!host) return 0;
    g_host = host;
    if (!init_js()) {
        host->alert("QuickJS: Failed to initialize runtime!");
        return 0;
    }
    g_host->register_on_reset(on_lume_reset);
    if (g_host->register_script_engine) {
        g_host->register_script_engine("js", execute_js);
        g_host->register_script_engine("javascript", execute_js);
    }
    return 1;
}
__declspec(dllexport) void lume_plugin_shutdown(void) {
    if (g_ctx) JS_FreeContext(g_ctx);
    if (g_rt) JS_FreeRuntime(g_rt);
    g_ctx = NULL;
    g_rt = NULL;
    g_host = NULL;
}
