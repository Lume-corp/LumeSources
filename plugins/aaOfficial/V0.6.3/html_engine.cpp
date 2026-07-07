// cl.exe /O2 /LD /GS- /GR- /EHsc- html_engine.cpp /link /NODEFAULTLIB /ENTRY:DllMain user32.lib gdi32.lib kernel32.lib
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#include <windows.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
extern "C" const int _fltused = 0;
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
extern "C" {
#pragma function(memset)
    void* __cdecl memset(void* dest, int c, size_t count) {
        unsigned char* p = (unsigned char*)dest;
        while (count--) {
            *p++ = (unsigned char)c;
        }
        return dest;
    }
#pragma function(memcpy)
    void* __cdecl memcpy(void* dest, const void* src, size_t count) {
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        while (count--) {
            *d++ = *s++;
        }
        return dest;
    }
}
float my_sin(float x) {
    while (x < -3.14159265f) x += 6.28318531f;
    while (x > 3.14159265f) x -= 6.28318531f;
    float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * (0.00833333f - x2 * 0.00019841f)));
}
float my_cos(float x) {
    return my_sin(x + 1.57079632f);
}
int my_strlen(const char* s) {
    int l = 0;
    while (s && s[l]) l++;
    return l;
}
int my_wcslen(const wchar_t* s) {
    int l = 0;
    while (s && s[l]) l++;
    return l;
}
bool my_streq(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
bool my_strprefix(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) {
            return false;
        }
    }
    return true;
}
int my_atoi(const char* str) {
    while (*str == ' ' || *str == '\t' || *str == ':') {
        str++;
    }
    int res = 0;
    bool neg = false;
    if (*str == '-') {
        neg = true;
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return neg ? -res : res;
}
int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
LumeHostAPI* g_api = nullptr;
volatile bool g_anim_running = false;
HANDLE g_anim_thread = NULL;
DWORD WINAPI AnimThreadProc(LPVOID) {
    while (g_anim_running) {
        if (g_api) {
            g_api->invalidate_content();
        }
        Sleep(16);
    }
    return 0;
}
struct Color {
    int r, g, b, a;
};
Color ParseColor(const char* hex) {
    while (*hex == ' ' || *hex == ':') {
        hex++;
    }
    if (!hex || *hex != '#') {
        return { 0, 0, 0, 0 };
    }
    hex++;
    int len = 0;
    while (hex[len] && hex[len] != ';' && hex[len] != '"' && hex[len] != ' ' && hex[len] != '}') {
        len++;
    }
    Color c = { 0, 0, 0, 255 };
    if (len >= 6) {
        c.r = (hex_to_int(hex[0]) << 4) | hex_to_int(hex[1]);
        c.g = (hex_to_int(hex[2]) << 4) | hex_to_int(hex[3]);
        c.b = (hex_to_int(hex[4]) << 4) | hex_to_int(hex[5]);
    }
    else if (len >= 3) {
        c.r = hex_to_int(hex[0]) * 17;
        c.g = hex_to_int(hex[1]) * 17;
        c.b = hex_to_int(hex[2]) * 17;
    }
    return c;
}
struct Style {
    Color bg = {0, 0, 0, 0};
    Color color = {44, 44, 44, 255};
    int font_size = 16;
    int font_weight = 400;
    int letter_spacing = 0;
    int padding_x = 0;
    int padding_y = 0;
    int margin_bottom = 0;
    int border_radius = 0;
    int border_bottom = 0;
    int width = 0;
    int max_width = 0;
    int height = 0;
    int height_vh = 0;
    int text_align = 0;
    bool is_flex = false;
    int justify_content = 0;
    int align_items = 0;
    int gap = 0;
    int position = 0;
    int right = -1;
    int right_pct = -1;
    int top = -1;
    bool has_color = false;
    bool has_bg = false;
    bool is_morph = false;
};
enum HtmlNodeType {
    HTML_NODE_BLOCK,
    HTML_NODE_TEXT
};
struct Rect {
    int x, y, w, h;
};
struct HtmlNode {
    HtmlNodeType type;
    wchar_t* text;
    char href[256];
    Style style;
    Style hover_style;
    bool has_hover_rule;
    float hover_progress;
    Rect layout;
    HtmlNode* parent;
    HtmlNode* first_child;
    HtmlNode* last_child;
    HtmlNode* next;
};
struct CssRule {
    char class_name[64];
    bool is_hover;
    Style style;
};
struct Arena {
    char* mem;
    size_t size;
    size_t used;
};
void* arena_alloc(Arena* arena, size_t size) {
    size = (size + 7) & ~7;
    if (arena->used + size > arena->size) {
        return nullptr;
    }
    void* ptr = arena->mem + arena->used;
    arena->used += size;
    return ptr;
}
wchar_t* utf8_to_utf16(Arena* arena, const char* utf8_str, int len) {
    if (!utf8_str || len <= 0) {
        return nullptr;
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str, len, NULL, 0);
    if (wlen <= 0) {
        return nullptr;
    }
    wchar_t* wstr = (wchar_t*)arena_alloc(arena, (wlen + 1) * sizeof(wchar_t));
    if (wstr) {
        MultiByteToWideChar(CP_UTF8, 0, utf8_str, len, wstr, wlen);
        wstr[wlen] = L'\0';
    }
    return wstr;
}
int decode_html_entities(char* text, int len) {
    int write_pos = 0;
    for (int read_pos = 0; read_pos < len; ) {
        if (text[read_pos] == '&') {
            if (my_strprefix(&text[read_pos], "&amp;")) {text[write_pos++] = '&'; read_pos += 5; continue;}
            if (my_strprefix(&text[read_pos], "&lt;")) {text[write_pos++] = '<'; read_pos += 4; continue;}
            if (my_strprefix(&text[read_pos], "&gt;")) {text[write_pos++] = '>'; read_pos += 4; continue;}
            if (my_strprefix(&text[read_pos], "&nbsp;")) {text[write_pos++] = ' '; read_pos += 6; continue;}
            if (my_strprefix(&text[read_pos], "&quot;")) {text[write_pos++] = '"'; read_pos += 6; continue;}
        }
        text[write_pos++] = text[read_pos++];
    }
    text[write_pos] = '\0';
    return write_pos;
}
struct HtmlPage {
    Arena arena;
    HtmlNode* root;
    CssRule rules[256];
    int rule_count;
    int total_height;
    int layout_width;
    int mouse_x;
    int mouse_y;
    int scroll_y;
};
const char* extract_attr(const char* tag, const char* attr, char* out, int max_len) {
    const char* p = tag;
    int attr_len = my_strlen(attr);
    while (*p && *p != '>') {
        if ((p == tag || *(p - 1) == ' ' || *(p - 1) == '\t') && my_strprefix(p, attr)) {
            const char* next = p + attr_len;
            if (*next == '=' || *next == ' ') {
                p = next;
                while (*p == ' ' || *p == '=') {
                    p++;
                }
                char quote = ' ';
                if (*p == '"' || *p == '\'') {
                    quote = *p++;
                }
                int i = 0;
                while (*p && *p != quote && *p != '>' && i < max_len - 1) {
                    out[i++] = *p++;
                }
                out[i] = '\0';
                return out;
            }
        }
        p++;
    }
    out[0] = '\0';
    return out;
}
bool HasClass(const char* class_attr, const char* class_name) {
    const char* p = class_attr;
    int len = my_strlen(class_name);
    while (*p) {
        while (*p == ' ') {
            p++;
        }
        const char* start = p;
        while (*p && *p != ' ') {
            p++;
        }
        if (p - start == len) {
            bool match = true;
            for (int i = 0; i < len; i++) {
                if (start[i] != class_name[i]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
    return false;
}
void ParseInlineStyle(Style* s, const char* style_str) {
    const char* p = style_str;
    while (*p && *p != '}') {
        while (*p == ' ' || *p == '\n' || *p == '\r') {
            p++;
        }
        if (my_strprefix(p, "color:")) {
            s->color = ParseColor(p + 6);
            s->has_color = true;
        }
        else if (my_strprefix(p, "background:") || my_strprefix(p, "background-color:")) {
            const char* bg_start = p;
            while (*bg_start && *bg_start != ':') bg_start++;
            if (*bg_start == ':') bg_start++;
            s->bg = ParseColor(bg_start);
            s->has_bg = true;
        }
        else if (my_strprefix(p, "padding:")) {
            p += 8;
            while (*p == ' ') p++;
            s->padding_y = my_atoi(p);
            s->padding_x = s->padding_y;

            if (*p == '-') p++;
            while (*p >= '0' && *p <= '9') p++;
            if (*p == 'p' && *(p + 1) == 'x') p += 2;

            while (*p == ' ') p++;
            if ((*p >= '0' && *p <= '9') || *p == '-') {
                s->padding_x = my_atoi(p);
            }
        }
        else if (my_strprefix(p, "margin-bottom:")) {
            s->margin_bottom = my_atoi(p + 14);
        }
        else if (my_strprefix(p, "border-radius:")) {
            if (my_strprefix(p + 14, " morph")) {
                s->is_morph = true;
            }
            else if (my_strprefix(p + 14, " 50%")) {
                s->border_radius = 9999;
            }
            else {
                s->border_radius = my_atoi(p + 14);
            }
        }
        else if (my_strprefix(p, "border-bottom:")) {
            s->border_bottom = my_atoi(p + 14);
        }
        else if (my_strprefix(p, "font-size:")) {
            s->font_size = my_atoi(p + 10);
        }
        else if (my_strprefix(p, "font-weight:")) {
            s->font_weight = my_atoi(p + 12);
        }
        else if (my_strprefix(p, "letter-spacing:")) {
            s->letter_spacing = my_atoi(p + 15);
        }
        else if (my_strprefix(p, "text-align:")) {
            p += 11;
            while (*p == ' ') p++;
            if (my_strprefix(p, "center")) s->text_align = 1;
            else if (my_strprefix(p, "right")) s->text_align = 2;
        }
        else if (my_strprefix(p, "display:")) {
            p += 8;
            while (*p == ' ') p++;
            if (my_strprefix(p, "flex") || my_strprefix(p, "grid")) s->is_flex = true;
        }
        else if (my_strprefix(p, "justify-content:")) {
            p += 16;
            while (*p == ' ') p++;
            if (my_strprefix(p, "center")) s->justify_content = 1;
            else if (my_strprefix(p, "space-between")) s->justify_content = 2;
        }
        else if (my_strprefix(p, "align-items:")) {
            p += 12;
            while (*p == ' ') p++;
            if (my_strprefix(p, "center")) s->align_items = 1;
        }
        else if (my_strprefix(p, "gap:")) {
            s->gap = my_atoi(p + 4);
        }
        else if (my_strprefix(p, "width:")) {
            s->width = my_atoi(p + 6);
        }
        else if (my_strprefix(p, "max-width:")) {
            s->max_width = my_atoi(p + 10);
        }
        else if (my_strprefix(p, "height:")) {
            p += 7;
            while (*p == ' ') p++;
            if (my_strprefix(p, "100vh")) s->height_vh = 100;
            else s->height = my_atoi(p);
        }
        else if (my_strprefix(p, "position:")) {
            p += 9;
            while (*p == ' ') p++;
            if (my_strprefix(p, "absolute")) s->position = 1;
        }
        else if (my_strprefix(p, "right:")) {
            p += 6;
            while (*p == ' ') p++;
            s->right = my_atoi(p);
            while (*p >= '0' && *p <= '9') p++;
            if (*p == '%') {
                s->right_pct = s->right;
                s->right = -1;
            }
        }
        else if (my_strprefix(p, "top:")) {
            s->top = my_atoi(p + 4);
        }
        while (*p && *p != ';' && *p != '}') {
            p++;
        }
        if (*p == ';') {
            p++;
        }
    }
}
void ParseCssBlock(HtmlPage* page, const char* css, int len) {
    const char* p = css;
    const char* end = css + len;
    while (p < end && page->rule_count < 256) {
        if (*p == '.') {
            p++;
            CssRule* rule = &page->rules[page->rule_count];
            rule->is_hover = false;
            int i = 0;
            while (p < end && *p != '{' && *p != ' ' && *p != '\n' && *p != '\r') {
                if (*p == ':') {
                    if (my_strprefix(p, ":hover")) {
                        rule->is_hover = true;
                    }
                    while (*p && *p != '{' && *p != ' ') {
                        p++;
                    }
                    break;
                }
                if (i < 63) {
                    rule->class_name[i++] = *p;
                }
                p++;
            }
            rule->class_name[i] = '\0';
            while (p < end && *p != '{') {
                p++;
            }
            if (*p == '{') {
                p++;
                const char* start = p;
                while (p < end && *p != '}') {
                    p++;
                }
                char rule_body[1024] = { 0 };
                int body_len = (p - start < 1023) ? (int)(p - start) : 1023;
                memcpy(rule_body, start, body_len);
                ParseInlineStyle(&rule->style, rule_body);
                page->rule_count++;
            }
        }
        p++;
    }
}
HtmlNode* create_node(HtmlPage* page, HtmlNodeType type) {
    HtmlNode* n = (HtmlNode*)arena_alloc(&page->arena, sizeof(HtmlNode));
    if (n) {
        memset(n, 0, sizeof(HtmlNode));
        n->type = type;
        n->hover_progress = 0.0f;
    }
    return n;
}
void ApplyCssRule(Style* dest, const Style* src) {
    if (src->has_bg) { dest->bg = src->bg; dest->has_bg = true; }
    if (src->has_color) { dest->color = src->color; dest->has_color = true; }
    if (src->padding_x > 0) dest->padding_x = src->padding_x;
    if (src->padding_y > 0) dest->padding_y = src->padding_y;
    if (src->margin_bottom > 0) dest->margin_bottom = src->margin_bottom;
    if (src->border_radius > 0) dest->border_radius = src->border_radius;
    if (src->border_bottom > 0) dest->border_bottom = src->border_bottom;
    if (src->font_size > 16) dest->font_size = src->font_size;
    if (src->font_weight != 400) dest->font_weight = src->font_weight;
    if (src->letter_spacing > 0) dest->letter_spacing = src->letter_spacing;
    if (src->text_align > 0) dest->text_align = src->text_align;
    if (src->is_flex) dest->is_flex = true;
    if (src->justify_content > 0) dest->justify_content = src->justify_content;
    if (src->align_items > 0) dest->align_items = src->align_items;
    if (src->gap > 0) dest->gap = src->gap;
    if (src->width > 0) dest->width = src->width;
    if (src->max_width > 0) dest->max_width = src->max_width;
    if (src->height > 0) dest->height = src->height;
    if (src->height_vh > 0) dest->height_vh = src->height_vh;
    if (src->position > 0) dest->position = src->position;
    if (src->right >= 0) dest->right = src->right;
    if (src->right_pct >= 0) dest->right_pct = src->right_pct;
    if (src->top >= 0) dest->top = src->top;
    if (src->is_morph) dest->is_morph = true;
}
void ParseHtml(HtmlPage* page, const char* html) {
    page->root = create_node(page, HTML_NODE_BLOCK);
    page->root->style.bg = ParseColor("#eae6dc");
    page->root->style.has_bg = true;
    HtmlNode* current = page->root;
    const char* p = html;
    while (*p) {
        if (my_strprefix(p, "<style>")) {
            p += 7;
            const char* start = p;
            while (*p && !my_strprefix(p, "</style>")) {
                p++;
            }
            ParseCssBlock(page, start, (int)(p - start));
            if (*p) {
                p += 8;
            }
            continue;
        }
        if (*p == '<') {
            if (p[1] == '/') {
                if (current->parent) {
                    current = current->parent;
                }
                while (*p && *p != '>') {
                    p++;
                }
                if (*p == '>') {
                    p++;
                }
            }
            else {
                HtmlNode* node = create_node(page, HTML_NODE_BLOCK);
                node->parent = current;
                if (!current->first_child) {
                    current->first_child = node;
                }
                else {
                    current->last_child->next = node;
                }
                current->last_child = node;
                node->style.color = node->parent->style.color;
                node->style.has_color = node->parent->style.has_color;
                node->style.font_size = node->parent->style.font_size;
                node->style.font_weight = node->parent->style.font_weight;
                node->style.letter_spacing = node->parent->style.letter_spacing;
                node->style.text_align = node->parent->style.text_align;
                char tag_buf[512] = {0};
                int i = 0;
                const char* t = p;
                while (*t && *t != '>' && i < 511) {
                    tag_buf[i++] = *t++;
                }
                tag_buf[i] = '\0';
                bool is_self_closing = false;
                if (i > 0 && tag_buf[i - 1] == '/') {
                    is_self_closing = true;
                }
                char tag_name[32] = {0};
                int tn_i = 0;
                int start_idx = 0;
                while (tag_buf[start_idx] == '<' || tag_buf[start_idx] == '/' || tag_buf[start_idx] == ' ') {
                    start_idx++;
                }
                while (tag_buf[start_idx] && tag_buf[start_idx] != ' ' && tag_buf[start_idx] != '/' && tag_buf[start_idx] != '>' && tn_i < 31) {
                    tag_name[tn_i++] = tag_buf[start_idx++];
                }
                tag_name[tn_i] = '\0';
                if (my_streq(tag_name, "br") || my_streq(tag_name, "img") ||
                    my_streq(tag_name, "hr") || my_streq(tag_name, "input") ||
                    my_streq(tag_name, "meta") || my_streq(tag_name, "link")) {
                    is_self_closing = true;
                }
                extract_attr(tag_buf, "href", node->href, 256);
                char cls[64];
                extract_attr(tag_buf, "class", cls, 64);
                if (cls[0]) {
                    for (int r = 0; r < page->rule_count; r++) {
                        if (HasClass(cls, page->rules[r].class_name)) {
                            if (page->rules[r].is_hover) {
                                ApplyCssRule(&node->hover_style, &page->rules[r].style);
                                node->has_hover_rule = true;
                            }
                            else {
                                ApplyCssRule(&node->style, &page->rules[r].style);
                            }
                        }
                    }
                }
                char attr[256];
                extract_attr(tag_buf, "style", attr, 256);
                if (attr[0]) {
                    ParseInlineStyle(&node->style, attr);
                }
                p = t;
                if (*p == '>') {
                    p++;
                }
                if (!is_self_closing) {
                    current = node;
                }
            }
        }
        else {
            const char* start = p;
            while (*p && *p != '<') {
                p++;
            }
            int len = (int)(p - start);
            bool has_text = false;
            for (int i = 0; i < len; i++) {
                if (start[i] != ' ' && start[i] != '\n' && start[i] != '\r') {
                    has_text = true;
                    break;
                }
            }
            if (has_text) {
                char* temp_text = (char*)arena_alloc(&page->arena, len + 1);
                if (temp_text) {
                    memcpy(temp_text, start, len);
                    temp_text[len] = '\0';
                    int decoded_len = decode_html_entities(temp_text, len);
                    HtmlNode* text_node = create_node(page, HTML_NODE_TEXT);
                    text_node->text = utf8_to_utf16(&page->arena, temp_text, decoded_len);
                    text_node->style.color = current->style.color;
                    text_node->style.font_size = current->style.font_size;
                    text_node->style.font_weight = current->style.font_weight;
                    text_node->style.letter_spacing = current->style.letter_spacing;
                    text_node->style.text_align = current->style.text_align;
                    if (!current->first_child) {
                        current->first_child = text_node;
                    }
                    else {
                        current->last_child->next = text_node;
                    }
                    current->last_child = text_node;
                }
            }
        }
    }
}
HFONT GetCachedFont(int size, int weight) {
    static HFONT cache[512] = {0};
    if (size < 0) size = 16;
    if (size > 100) size = 100;
    int weight_idx = (weight <= 300) ? 0 : (weight >= 700 ? 2 : 1);
    int idx = size + (weight_idx * 150);
    if (idx < 0 || idx >= 512) {
        idx = 0;
    }
    if (!cache[idx]) {
        int fw = (weight <= 300) ? FW_LIGHT : (weight >= 700 ? FW_BOLD : FW_NORMAL);
        cache[idx] = CreateFontW(-size, 0, 0, 0, fw, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    }
    return cache[idx];
}
void MeasureNode(HtmlNode* node, int avail_w, int view_h, HDC hdc) {
    if (!node) return;
    int current_w = avail_w;
    if (node->style.width > 0) {
        current_w = node->style.width;
    }
    if (node->style.max_width > 0 && current_w > node->style.max_width) {
        current_w = node->style.max_width;
    }
    int inner_avail_w = current_w - (node->style.padding_x * 2);
    if (inner_avail_w < 0) inner_avail_w = 0;
    if (node->type == HTML_NODE_TEXT && node->text) {
        HFONT f = GetCachedFont(node->style.font_size, node->style.font_weight);
        HGDIOBJ old = SelectObject(hdc, f);
        SetTextCharacterExtra(hdc, node->style.letter_spacing);
        RECT r = { 0, 0, inner_avail_w, 10000 };
        UINT format = DT_CALCRECT;
        if (node->parent && node->parent->style.is_flex) {
            format |= DT_SINGLELINE;
            r.right = 10000;
        }
        else {
            format |= DT_WORDBREAK;
        }
        DrawTextW(hdc, node->text, -1, &r, format);
        SelectObject(hdc, old);
        SetTextCharacterExtra(hdc, 0);
        int letter_spacing_buffer = node->style.letter_spacing * my_wcslen(node->text);
        node->layout.w = r.right + letter_spacing_buffer + 5 + (node->style.padding_x * 2);
        node->layout.h = r.bottom + (node->style.padding_y * 2);
        return;
    }
    int children_h = 0;
    int max_child_w = 0;
    int total_flex_w = 0;
    int flex_count = 0;
    for (HtmlNode* c = node->first_child; c; c = c->next) {
        if (c->style.position == 1) continue;
        MeasureNode(c, inner_avail_w, view_h, hdc);
        if (node->style.is_flex) {
            total_flex_w += c->layout.w;
            if (c->layout.h > children_h) {
                children_h = c->layout.h;
            }
            flex_count++;
        }
        else {
            children_h += c->layout.h + c->style.margin_bottom;
            if (c->layout.w > max_child_w) {
                max_child_w = c->layout.w;
            }
        }
    }
    if (node->style.is_flex && flex_count > 0) {
        total_flex_w += node->style.gap * (flex_count - 1);
        max_child_w = total_flex_w;
    }
    if (node->style.width > 0) {
        node->layout.w = node->style.width;
    }
    else if (node->parent && node->parent->style.is_flex) {
        node->layout.w = max_child_w + (node->style.padding_x * 2);
    }
    else {
        node->layout.w = current_w;
    }
    int final_h = children_h;
    if (node->style.height_vh > 0) {
        final_h = (view_h * node->style.height_vh) / 100;
    }
    else if (node->style.height > 0) {
        final_h = node->style.height;
    }
    node->layout.h = final_h + (node->style.padding_y * 2);
    for (HtmlNode* c = node->first_child; c; c = c->next) {
        if (c->style.position == 1) {
            MeasureNode(c, node->layout.w - (node->style.padding_x * 2), view_h, hdc);
        }
    }
}
void ArrangeNode(HtmlNode* node, int x, int y) {
    if (!node) return;
    node->layout.x = x;
    node->layout.y = y;
    if (node->type == HTML_NODE_TEXT) {
        if (node->parent && node->parent->style.align_items == 1 && node->parent->style.height > 0) {
            node->layout.y = y + (node->parent->style.height - node->layout.h) / 2;
        }
        return;
    }
    int content_x = x + node->style.padding_x;
    int content_y = y + node->style.padding_y;
    int content_w = node->layout.w - (node->style.padding_x * 2);
    if (node->style.is_flex) {
        int count = 0;
        int total_intrinsic_w = 0;
        for (HtmlNode* c = node->first_child; c; c = c->next) {
            if (c->style.position != 1) {
                count++;
                total_intrinsic_w += c->layout.w;
            }
        }
        int cur_x = content_x;
        int actual_gap = node->style.gap;
        if (node->style.justify_content == 1) {
            cur_x += (content_w - total_intrinsic_w - (node->style.gap * (count - 1))) / 2;
        }
        else if (node->style.justify_content == 2 && count > 1) {
            int free_space = content_w - total_intrinsic_w;
            actual_gap = free_space / (count - 1);
        }
        int flex_h = node->layout.h - (node->style.padding_y * 2);
        for (HtmlNode* c = node->first_child; c; c = c->next) {
            if (c->style.position == 1) continue;
            int cy = content_y;
            if (node->style.align_items == 1) {
                cy += (flex_h - c->layout.h) / 2;
            }
            ArrangeNode(c, cur_x, cy);
            cur_x += c->layout.w + actual_gap;
        }
    }
    else {
        int cur_y = content_y;
        for (HtmlNode* c = node->first_child; c; c = c->next) {
            if (c->style.position == 1) continue;
            int cx = content_x;
            if (node->style.text_align == 1 && c->layout.w < content_w) {
                cx += (content_w - c->layout.w) / 2;
            }
            ArrangeNode(c, cx, cur_y);
            cur_y += c->layout.h + c->style.margin_bottom;
        }
    }
    for (HtmlNode* c = node->first_child; c; c = c->next) {
        if (c->style.position == 1) {
            int ax = x;
            if (c->style.right >= 0) {
                ax = x + node->layout.w - c->layout.w - c->style.right;
            }
            else if (c->style.right_pct >= 0) {
                ax = x + node->layout.w - c->layout.w - (node->layout.w * c->style.right_pct / 100);
            }
            ArrangeNode(c, ax, y + c->style.top);
        }
    }
}
void RenderNode(HtmlPage* page, HtmlNode* node, HDC hdc, int scroll_y, int view_h) {
    if (!node) return;
    int draw_y = node->layout.y - scroll_y;
    if (draw_y + node->layout.h < -1000 || draw_y > view_h + 1000) {
        return;
    }
    Style cur = node->style;
    if (node->has_hover_rule) {
        bool is_hovered = (page->mouse_x >= node->layout.x && page->mouse_x <= node->layout.x + node->layout.w &&
            page->mouse_y >= draw_y && page->mouse_y <= draw_y + node->layout.h);
        if (is_hovered) {
            node->hover_progress += 0.1f;
            if (node->hover_progress > 1.0f) node->hover_progress = 1.0f;
        }
        else {
            node->hover_progress -= 0.1f;
            if (node->hover_progress < 0.0f) node->hover_progress = 0.0f;
        }
        if (node->hover_progress > 0.0f) {
            if (node->hover_style.has_bg) {
                cur.bg.r = cur.bg.r + (node->hover_style.bg.r - cur.bg.r) * node->hover_progress;
                cur.bg.g = cur.bg.g + (node->hover_style.bg.g - cur.bg.g) * node->hover_progress;
                cur.bg.b = cur.bg.b + (node->hover_style.bg.b - cur.bg.b) * node->hover_progress;
                cur.has_bg = true;
            }
            if (node->hover_style.has_color) {
                cur.color.r = cur.color.r + (node->hover_style.color.r - cur.color.r) * node->hover_progress;
                cur.color.g = cur.color.g + (node->hover_style.color.g - cur.color.g) * node->hover_progress;
                cur.color.b = cur.color.b + (node->hover_style.color.b - cur.color.b) * node->hover_progress;
            }
        }
    }
    if (cur.has_bg && node->type != HTML_NODE_TEXT) {
        HBRUSH b = CreateSolidBrush(RGB(cur.bg.r, cur.bg.g, cur.bg.b));
        HPEN p = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldB = SelectObject(hdc, b);
        HGDIOBJ oldP = SelectObject(hdc, p);
        if (cur.is_morph) {
            POINT pts[60];
            float time = GetTickCount() * 0.001f;
            int cx = node->layout.x + node->layout.w / 2;
            int cy = draw_y + node->layout.h / 2;
            float base_r = node->layout.w / 2.0f;
            for (int i = 0; i < 60; i++) {
                float angle = i * (3.14159f * 2.0f / 60.0f);
                float r = base_r + my_sin(angle * 3.0f + time * 2.0f) * (base_r * 0.05f) + my_cos(angle * 4.0f - time * 1.5f) * (base_r * 0.05f);
                pts[i].x = cx + my_cos(angle) * r;
                pts[i].y = cy + my_sin(angle) * r;
            }
            Polygon(hdc, pts, 60);
        }
        else if (cur.border_radius >= 100) {
            Ellipse(hdc, node->layout.x, draw_y, node->layout.x + node->layout.w, draw_y + node->layout.h);
        }
        else if (cur.border_radius > 0) {
            RoundRect(hdc, node->layout.x, draw_y, node->layout.x + node->layout.w, draw_y + node->layout.h, cur.border_radius, cur.border_radius);
        }
        else {
            Rectangle(hdc, node->layout.x, draw_y, node->layout.x + node->layout.w + 1, draw_y + node->layout.h + 1);
        }
        SelectObject(hdc, oldB);
        SelectObject(hdc, oldP);
        DeleteObject(b);
        DeleteObject(p);
    }
    if (node->hover_style.border_bottom > 0 && node->hover_progress > 0.0f) {
        HPEN bp = CreatePen(PS_SOLID, node->hover_style.border_bottom, RGB(cur.color.r, cur.color.g, cur.color.b));
        HGDIOBJ oldBp = SelectObject(hdc, bp);
        int line_w = node->layout.w * node->hover_progress;
        int line_x = node->layout.x + (node->layout.w - line_w) / 2;
        MoveToEx(hdc, line_x, draw_y + node->layout.h, nullptr);
        LineTo(hdc, line_x + line_w, draw_y + node->layout.h);
        SelectObject(hdc, oldBp);
        DeleteObject(bp);
    }
    if (node->type == HTML_NODE_TEXT && node->text) {
        HFONT f = GetCachedFont(cur.font_size, cur.font_weight);
        HGDIOBJ old = SelectObject(hdc, f);
        SetTextColor(hdc, RGB(cur.color.r, cur.color.g, cur.color.b));
        SetBkMode(hdc, TRANSPARENT);
        SetTextCharacterExtra(hdc, cur.letter_spacing);
        int px = node->parent ? (node->parent->layout.x + node->parent->style.padding_x) : node->layout.x;
        int pw = node->parent ? (node->parent->layout.w - (node->parent->style.padding_x * 2)) : node->layout.w;
        RECT r = { px, draw_y, px + pw + 20, draw_y + node->layout.h };
        UINT format = DT_WORDBREAK;
        if (node->parent && node->parent->style.text_align == 1) {
            format |= DT_CENTER;
        }
        DrawTextW(hdc, node->text, -1, &r, format);
        SelectObject(hdc, old);
        SetTextCharacterExtra(hdc, 0);
    }
    for (HtmlNode* c = node->first_child; c; c = c->next) {
        RenderNode(page, c, hdc, scroll_y, view_h);
    }
}
HtmlNode* HitTest(HtmlNode* node, int x, int y) {
    if (!node) return nullptr;
    HtmlNode* hit = nullptr;
    for (HtmlNode* c = node->first_child; c; c = c->next) {
        HtmlNode* c_hit = HitTest(c, x, y);
        if (c_hit) hit = c_hit;
    }
    if (hit) return hit;
    if (x >= node->layout.x && x <= node->layout.x + node->layout.w &&
        y >= node->layout.y && y <= node->layout.y + node->layout.h) {
        if (node->href[0] != '\0' || node->has_hover_rule) {
            return node;
        }
    }
    return nullptr;
}
void* html_load(const char* url, const char* content, size_t length) {
    size_t mem_size = 3 * 1024 * 1024;
    HtmlPage* page = (HtmlPage*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HtmlPage) + mem_size);
    if (!page) return nullptr;
    page->arena.mem = (char*)(page + 1);
    page->arena.size = mem_size;
    page->arena.used = 0;
    page->mouse_x = -1;
    page->mouse_y = -1;
    page->scroll_y = 0;
    ParseHtml(page, content);
    page->layout_width = 0;
    return page;
}
void html_render(void* ctx, HDC hdc, int w, int h, int scroll_y) {
    HtmlPage* page = (HtmlPage*)ctx;
    if (!page || !page->root) return;
    page->scroll_y = scroll_y;
    if (page->layout_width != w) {
        page->layout_width = w;
        MeasureNode(page->root, w, h, hdc);
        ArrangeNode(page->root, 0, 0);
        page->total_height = page->root->layout.h;
    }
    RenderNode(page, page->root, hdc, scroll_y, h);
}
void html_free(void* ctx) {
    if (ctx) {
        HeapFree(GetProcessHeap(), 0, ctx);
    }
}
int html_get_height(void* ctx) {
    HtmlPage* page = (HtmlPage*)ctx;
    return page ? page->total_height : 0;
}
void html_mouse_move(void* ctx, int x, int y) {
    HtmlPage* page = (HtmlPage*)ctx;
    if (!page || !page->root) return;
    page->mouse_x = x;
    page->mouse_y = y;
    HtmlNode* hit = HitTest(page->root, x, y + page->scroll_y);
    if (hit && (hit->href[0] != '\0' || hit->has_hover_rule)) {
        SetCursor(LoadCursor(0, IDC_HAND));
    }
    else {
        SetCursor(LoadCursor(0, IDC_ARROW));
    }
}
void html_mouse_down(void* ctx, int x, int y, int btn) {
    HtmlPage* page = (HtmlPage*)ctx;
    if (!page || !page->root || btn != 1) return;
    HtmlNode* hit = HitTest(page->root, x, y + page->scroll_y);
    if (hit && hit->href[0] && g_api) {
        g_api->navigate_to(hit->href);
    }
}
extern "C" {
    __declspec(dllexport) void __cdecl lume_plugin_init(lua_State* L, LumeHostAPI* api) {
        g_api = api;
        g_anim_running = true;
        g_anim_thread = CreateThread(NULL, 0, AnimThreadProc, NULL, 0, NULL);
        CustomPageHandler html_handler;
        memset(&html_handler, 0, sizeof(html_handler));
        html_handler.extension = ".html";
        html_handler.load_page = html_load;
        html_handler.render_page = html_render;
        html_handler.free_page = html_free;
        html_handler.get_document_height = html_get_height;
        html_handler.on_mouse_move = html_mouse_move;
        html_handler.on_mouse_down = html_mouse_down;
        g_api->register_page_engine(html_handler);
    }
    __declspec(dllexport) void __cdecl lume_plugin_shutdown() {
        g_anim_running = false;
        if (g_anim_thread) {
            WaitForSingleObject(g_anim_thread, 1000);
            CloseHandle(g_anim_thread);
        }
        g_api = nullptr;
    }
}