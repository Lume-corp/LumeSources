#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define BUILDING_PLUGIN
#include "lume_plugin.h"
LumeHostAPI* g_api = nullptr;
void* mem_alloc(size_t size) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}
void mem_free(void* ptr) {
    if (ptr) HeapFree(GetProcessHeap(), 0, ptr);
}
int my_strlen(const char* s) {
    if (!s) return 0;
    int l = 0;
    while (s[l]) l++;
    return l;
}
int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
char* my_strndup(const char* s, int n) {
    char* res = (char*)mem_alloc(n + 1);
    for (int i = 0; i < n; i++) res[i] = s[i];
    res[n] = '\0';
    return res;
}
int my_atoi(const char* s) {
    if (!s) return 0;
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res;
}
int hex2int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
COLORREF HexToRGB(const char* hex, COLORREF def) {
    if (!hex || hex[0] != '#' || my_strlen(hex) < 7) return def;
    int r = (hex2int(hex[1]) << 4) | hex2int(hex[2]);
    int g = (hex2int(hex[3]) << 4) | hex2int(hex[4]);
    int b = (hex2int(hex[5]) << 4) | hex2int(hex[6]);
    return RGB(r, g, b);
}
struct Prop {
    char* key;
    char* val;
    Prop* next;
};
struct PhtNode {
    char* tag;
    Prop* props;
    PhtNode* first_child;
    PhtNode* next_sibling;
    int calc_x, calc_y, calc_w, calc_h;
};
PhtNode* create_node(const char* tag_start, int tag_len) {
    PhtNode* n = (PhtNode*)mem_alloc(sizeof(PhtNode));
    n->tag = my_strndup(tag_start, tag_len);
    return n;
}
void add_prop(PhtNode* n, const char* k_start, int k_len, const char* v_start, int v_len) {
    Prop* p = (Prop*)mem_alloc(sizeof(Prop));
    p->key = my_strndup(k_start, k_len);
    if (v_start) p->val = my_strndup(v_start, v_len);
    p->next = n->props;
    n->props = p;
}
const char* get_prop(PhtNode* n, const char* key) {
    for (Prop* p = n->props; p; p = p->next) {
        if (my_strcmp(p->key, key) == 0) return p->val;
    }
    return nullptr;
}
void add_child(PhtNode* parent, PhtNode* child) {
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        PhtNode* last = parent->first_child;
        while (last->next_sibling) last = last->next_sibling;
        last->next_sibling = child;
    }
}
void free_node(PhtNode* n) {
    if (!n) return;
    mem_free(n->tag);
    Prop* p = n->props;
    while (p) {
        Prop* next = p->next;
        mem_free(p->key);
        mem_free(p->val);
        mem_free(p);
        p = next;
    }
    PhtNode* c = n->first_child;
    while (c) {
        PhtNode* next = c->next_sibling;
        free_node(c);
        c = next;
    }
    mem_free(n);
}
struct PhtContext {
    PhtNode* root;
    int total_h;
    int last_scroll;
};
PhtNode* parsePht(const char* code) {
    PhtNode* root = create_node("page", 4);
    struct StackItem { int indent; PhtNode* node; };
    StackItem stack[128];
    int stack_size = 0;
    stack[stack_size++] = { -1, root };
    const char* p = code;
    while (*p) {
        const char* eol = p;
        while (*eol && *eol != '\n') eol++;
        int indent = 0;
        const char* start = p;
        while (start < eol && (*start == ' ' || *start == '\t')) {
            indent += (*start == '\t' ? 4 : 1);
            start++;
        }
        bool in_quotes = false;
        const char* logical_end = eol;
        for (const char* s = start; s < eol; s++) {
            if (*s == '"') {
                in_quotes = !in_quotes;
            } else if (*s == '#' && !in_quotes) {
                logical_end = s;
                break;
            }
        }
        if (start >= logical_end || *start == '\r') {
            p = (*eol == '\n') ? eol + 1 : eol;
            continue;
        }
        const char* end = logical_end;
        while (end > start && (*(end - 1) == '\r' || *(end - 1) == ':' || *(end - 1) == ' ' || *(end - 1) == '\t')) {
            end--;
        }
        if (start >= end) {
            p = (*eol == '\n') ? eol + 1 : eol;
            continue;
        }
        const char* tag_end = start;
        while (tag_end < end && *tag_end != ' ') tag_end++;
        PhtNode* node = create_node(start, tag_end - start);
        const char* prop_p = tag_end;
        while (prop_p < end) {
            while (prop_p < end && *prop_p == ' ') prop_p++;
            if (prop_p >= end) break;
            const char* k_start = prop_p;
            while (prop_p < end && *prop_p != '=' && *prop_p != ' ') prop_p++;
            const char* k_end = prop_p;
            const char* v_start = nullptr;
            const char* v_end = nullptr;
            if (prop_p < end && *prop_p == '=') {
                prop_p++;
                if (prop_p < end && *prop_p == '"') {
                    prop_p++;
                    v_start = prop_p;
                    while (prop_p < end && *prop_p != '"') prop_p++;
                    v_end = prop_p;
                    if (prop_p < end) prop_p++;
                } else {
                    v_start = prop_p;
                    while (prop_p < end && *prop_p != ' ') prop_p++;
                    v_end = prop_p;
                }
            }
            if (k_end > k_start) {
                add_prop(node, k_start, k_end - k_start, v_start, v_end ? v_end - v_start : 0);
            }
        }
        while (stack_size > 1 && stack[stack_size - 1].indent >= indent) {
            stack_size--;
        }
        add_child(stack[stack_size - 1].node, node);
        stack[stack_size++] = { indent, node };
        p = (*eol == '\n') ? eol + 1 : eol;
    }

    return root;
}
int doLayout(PhtNode* node, int x, int y, int mw) {
    node->calc_x = x; node->calc_y = y; node->calc_w = mw;
    if (my_strcmp(node->tag, "page") == 0 || my_strcmp(node->tag, "block") == 0) {
        const char* pad_s = get_prop(node, "pad");
        int pad = pad_s ? my_atoi(pad_s) : 10;
        int cy = y + pad;
        for (PhtNode* c = node->first_child; c; c = c->next_sibling) {
            cy += doLayout(c, x + pad, cy, mw - pad * 2);
        }
        node->calc_h = (cy - y) + pad;
        return node->calc_h;
    }
    else if (my_strcmp(node->tag, "text") == 0) {
        const char* sz_s = get_prop(node, "size");
        node->calc_h = sz_s ? my_atoi(sz_s) + 10 : 30;
        return node->calc_h;
    }
    else if (my_strcmp(node->tag, "button") == 0) {
        const char* w_s = get_prop(node, "width");
        const char* h_s = get_prop(node, "height");
        node->calc_w = w_s ? my_atoi(w_s) : 120;
        node->calc_h = h_s ? my_atoi(h_s) : 40;
        return node->calc_h + 10;
    }
    else if (my_strcmp(node->tag, "rect") == 0) {
        const char* h_s = get_prop(node, "height");
        node->calc_h = h_s ? my_atoi(h_s) : 2;
        return node->calc_h + 10;
    }
    else if (my_strcmp(node->tag, "glcanvas") == 0) {
        const char* w_s = get_prop(node, "width");
        const char* h_s = get_prop(node, "height");
        node->calc_w = w_s ? my_atoi(w_s) : 400;
        node->calc_h = h_s ? my_atoi(h_s) : 300;
        return node->calc_h + 10;
    }
    return 0;
}
void doDraw(PhtNode* node, HDC hdc, int scroll_y) {
    int dy = node->calc_y - scroll_y;
    if (my_strcmp(node->tag, "page") == 0 || my_strcmp(node->tag, "block") == 0) {
        const char* bg = get_prop(node, "bg");
        if (bg) {
            HBRUSH br = CreateSolidBrush(HexToRGB(bg, RGB(30,30,30)));
            RECT r = { node->calc_x, dy, node->calc_x + node->calc_w, dy + node->calc_h };
            FillRect(hdc, &r, br);
            DeleteObject(br);
        }
    }
    else if (my_strcmp(node->tag, "text") == 0) {
        const char* content = get_prop(node, "content");
        if (content) {
            const char* sz_s = get_prop(node, "size");
            int size = sz_s ? my_atoi(sz_s) : 20;
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, HexToRGB(get_prop(node, "color"), RGB(255,255,255)));
            HFONT f = CreateFontA(-size, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
            HFONT of = (HFONT)SelectObject(hdc, f);
            RECT r = { node->calc_x, dy, node->calc_x + node->calc_w, dy + node->calc_h };
            DrawTextA(hdc, content, -1, &r, DT_LEFT | DT_WORDBREAK);
            SelectObject(hdc, of);
            DeleteObject(f);
        }
    }
    else if (my_strcmp(node->tag, "button") == 0) {
        HBRUSH br = CreateSolidBrush(HexToRGB(get_prop(node, "bg"), RGB(59,130,246)));
        RECT r = { node->calc_x, dy, node->calc_x + node->calc_w, dy + node->calc_h };
        FillRect(hdc, &r, br);
        DeleteObject(br);
        const char* content = get_prop(node, "content");
        if (content) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, HexToRGB(get_prop(node, "color"), RGB(255,255,255)));
            HFONT f = CreateFontA(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
            HFONT of = (HFONT)SelectObject(hdc, f);
            DrawTextA(hdc, content, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, of);
            DeleteObject(f);
        }
    }
    else if (my_strcmp(node->tag, "glcanvas") == 0) {
        const char* id = get_prop(node, "id");
        if (!id) id = "default_gl";
        HBRUSH br = CreateSolidBrush(RGB(80, 80, 100));
        RECT r = { node->calc_x - 1, dy - 1, node->calc_x + node->calc_w + 1, dy + node->calc_h + 1 };
        FrameRect(hdc, &r, br);
        DeleteObject(br);
        g_api->gl_place(id, node->calc_x, node->calc_y, node->calc_w, node->calc_h, scroll_y);
    }
    for (PhtNode* c = node->first_child; c; c = c->next_sibling) {
        doDraw(c, hdc, scroll_y);
    }
}
void checkClicks(PhtNode* node, int mx, int my) {
    if (my_strcmp(node->tag, "button") == 0) {
        if (mx >= node->calc_x && mx <= node->calc_x + node->calc_w &&
            my >= node->calc_y && my <= node->calc_y + node->calc_h) {
            const char* act = get_prop(node, "action");
            if (act) g_api->alert(act);
            else g_api->alert("Python-style button clicked!");
        }
    }
    for (PhtNode* c = node->first_child; c; c = c->next_sibling) {
        checkClicks(c, mx, my);
    }
}
void* PHT_Load(const char* url, const char* raw_data, size_t length) {
    PhtContext* ctx = (PhtContext*)mem_alloc(sizeof(PhtContext));
    ctx->root = parsePht(raw_data);
    return ctx;
}

void PHT_Free(void* ctx) {
    if (!ctx) return;
    PhtContext* c = (PhtContext*)ctx;
    free_node(c->root);
    mem_free(c);
}
void PHT_Render(void* ctx, HDC hdc, int w, int h, int scroll_y) {
    PhtContext* c = (PhtContext*)ctx;
    if (!c->root) return;
    c->last_scroll = scroll_y;
    c->total_h = doLayout(c->root, 0, 0, w);
    doDraw(c->root, hdc, scroll_y);
}
int PHT_Height(void* ctx) {
    return ((PhtContext*)ctx)->total_h;
}
void PHT_Click(void* ctx, int x, int y, int button) {
    PhtContext* c = (PhtContext*)ctx;
    int abs_y = y + c->last_scroll;
    checkClicks(c->root, x, abs_y);
}
extern "C" LUME_PLUGIN_EXPORT int lume_plugin_init(lua_State* L, LumeHostAPI* api) {
    g_api = api;
    CustomPageHandler engine = {};
    engine.extension = ".pht";
    engine.load_page = PHT_Load;
    engine.free_page = PHT_Free;
    engine.render_page = PHT_Render;
    engine.get_document_height = PHT_Height;
    engine.on_mouse_down = PHT_Click;

    g_api->register_page_engine(engine);
    return 0;
}
extern "C" BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    return TRUE;
}