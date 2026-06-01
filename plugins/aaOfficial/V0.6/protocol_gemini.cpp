#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include "lume_plugin.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#ifndef SCH_CRED_IGNORE_UNKNOWN_CA
#define SCH_CRED_IGNORE_UNKNOWN_CA 0x00000001
#endif
#ifndef SCH_CRED_IGNORE_NO_REVOCATION_CHECK
#define SCH_CRED_IGNORE_NO_REVOCATION_CHECK 0x00000002
#endif
#ifndef SCH_CRED_IGNORE_CERT_CN_INVALID
#define SCH_CRED_IGNORE_CERT_CN_INVALID 0x00001000
#endif
#ifndef SCH_CRED_IGNORE_CERT_DATE_INVALID
#define SCH_CRED_IGNORE_CERT_DATE_INVALID 0x00002000
#endif
extern "C" {
    int _fltused = 0;
#pragma function(memset)
    void* __cdecl memset(void* dest, int c, size_t count) {
        unsigned char* bytes = (unsigned char*)dest;
        while (count--) *bytes++ = (unsigned char)c;
        return dest;
    }
#pragma function(memcpy)
    void* __cdecl memcpy(void* dest, const void* src, size_t count) {
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        while (count--) *d++ = *s++;
        return dest;
    }
#pragma function(memmove)
    void* __cdecl memmove(void* dest, const void* src, size_t count) {
        unsigned char* d = (unsigned char*)dest;
        const unsigned char* s = (const unsigned char*)src;
        if (d < s) { while (count--) *d++ = *s++; }
        else { d += count; s += count; while (count--) *--d = *--s; }
        return dest;
    }
#pragma function(strlen)
    size_t __cdecl strlen(const char* str) {
        return (size_t)lstrlenA(str);
    }
}
void* __cdecl operator new(size_t size) {return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);}
void* __cdecl operator new[](size_t size) {return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);}
void __cdecl operator delete(void* ptr) noexcept {if (ptr) HeapFree(GetProcessHeap(), 0, ptr);}
void __cdecl operator delete[](void* ptr) noexcept {if (ptr) HeapFree(GetProcessHeap(), 0, ptr);}
void __cdecl operator delete(void* ptr, size_t) noexcept {if (ptr) HeapFree(GetProcessHeap(), 0, ptr);}
enum class GType {TEXT, LINK, H1, H2, H3, LIST, QUOTE, PRE};
struct GemElem {
    GType type;
    char text[4096];
    char url[2048];
    RECT r;
    GemElem() {
        type = GType::TEXT;
        text[0] = '\0';
        url[0] = '\0';
        r.left = r.top = r.right = r.bottom = 0;
    }
};
struct GeminiContext {
    GemElem* elements = nullptr;
    size_t elemCount = 0;
    size_t elemCap = 0;
    int totalHeight = 0;
    HFONT fH1, fH2, fH3, fBody, fPre;
    int last_w = -1;
    int last_scroll = 0;
    wchar_t* render_wbuf = nullptr;
    HDC cacheDC = nullptr;
    HBITMAP cacheBmp = nullptr;
    HBITMAP oldBmp = nullptr;
    GeminiContext() {
        render_wbuf = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, 4096 * sizeof(wchar_t));
    }
    GemElem* add_elem() {
        if (elemCount == elemCap) {
            size_t newCap = elemCap == 0 ? 32 : elemCap * 2;
            GemElem* nd = (GemElem*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newCap * sizeof(GemElem));
            if (elements) {
                memcpy(nd, elements, elemCount * sizeof(GemElem));
                HeapFree(GetProcessHeap(), 0, elements);
            }
            elements = nd;
            elemCap = newCap;
        }
        GemElem* e = &elements[elemCount++];
        e->type = GType::TEXT;
        e->text[0] = '\0';
        e->url[0] = '\0';
        e->r = { 0, 0, 0, 0 };
        return e;
    }
    void clearCache() {
        if (cacheDC) {SelectObject(cacheDC, oldBmp); DeleteDC(cacheDC); cacheDC = nullptr;}
        if (cacheBmp) {DeleteObject(cacheBmp); cacheBmp = nullptr;}
    }
    void UpdateLayout(HDC refDC, int w) {
        int y = 20;
        int x = 20;
        int max_w = w - 40;
        if (max_w < 100) max_w = 100;
        for (size_t i = 0; i < elemCount; ++i) {
            GemElem& e = elements[i];
            HFONT f = fBody;
            if (e.type == GType::H1) f = fH1;
            else if (e.type == GType::PRE) f = fPre;

            auto oldF = SelectObject(refDC, f);
            int wlen = MultiByteToWideChar(CP_UTF8, 0, e.text, -1, render_wbuf, 4096);
            if (wlen > 0) {
                RECT r = { x, y, x + max_w, y + 10000 };
                DrawTextW(refDC, render_wbuf, wlen - 1, &r, DT_WORDBREAK | DT_CALCRECT);
                e.r = r;
                y += (r.bottom - r.top) + 10;
            }
            SelectObject(refDC, oldF);
        }
        totalHeight = y + 20;
        last_w = w;
        clearCache();
        cacheDC = CreateCompatibleDC(refDC);
        cacheBmp = CreateCompatibleBitmap(refDC, w, totalHeight);
        if (cacheBmp) {
            oldBmp = (HBITMAP)SelectObject(cacheDC, cacheBmp);
            HBRUSH bg = CreateSolidBrush(RGB(20, 20, 30));
            RECT r = { 0, 0, w, totalHeight };
            FillRect(cacheDC, &r, bg);
            DeleteObject(bg);
            SetBkMode(cacheDC, TRANSPARENT);
            for (size_t i = 0; i < elemCount; ++i) {
                GemElem& e = elements[i];
                HFONT f = fBody;
                COLORREF col = RGB(160, 160, 170);
                if (e.type == GType::H1) {f = fH1; col = RGB(255, 255, 255);}
                else if (e.type == GType::LINK) {f = fBody; col = RGB(50, 150, 255);}
                else if (e.type == GType::PRE) {f = fPre; col = RGB(100, 200, 100);}
                auto oldF = SelectObject(cacheDC, f);
                SetTextColor(cacheDC, col);
                int wlen = MultiByteToWideChar(CP_UTF8, 0, e.text, -1, render_wbuf, 4096);
                if (wlen > 0) {
                    RECT drawR = e.r;
                    DrawTextW(cacheDC, render_wbuf, wlen - 1, &drawR, DT_WORDBREAK);
                }
                SelectObject(cacheDC, oldF);
            }
        }
    }
    ~GeminiContext() {
        clearCache();
        if (render_wbuf) HeapFree(GetProcessHeap(), 0, render_wbuf);
        if (elements) HeapFree(GetProcessHeap(), 0, elements);
        DeleteObject(fH1); DeleteObject(fH2); DeleteObject(fH3); DeleteObject(fBody); DeleteObject(fPre);
    }
};
LumeHostAPI* g_host = nullptr;
char* ReturnError(const char* msg, int code = 0) {
    char* errBuf = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 1024);
    if (code != 0) {
        wsprintfA(errBuf, "ERROR_MSG: %s (Code: 0x%08X)", msg, code);
    }
    else {
        wsprintfA(errBuf, "ERROR_MSG: %s", msg);
    }
    return errBuf;
}
char* FetchGemini(const char* url) {
    const char* hostStart = url + 9;
    const char* pathStart = hostStart;
    while (*pathStart && *pathStart != '/') pathStart++;
    char host[256] = {0};
    memcpy(host, hostStart, pathStart - hostStart);
    host[pathStart - hostStart] = '\0';
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return ReturnError("WSAStartup failed");
    struct addrinfo hints = { 0 }, * res = nullptr;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "1965", &hints, &res) != 0) {
        WSACleanup();
        return ReturnError("DNS Resolution failed", WSAGetLastError());
    }
    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) return ReturnError("Socket creation failed", WSAGetLastError());
    if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        closesocket(sock); WSACleanup();
        return ReturnError("Connection failed (Port 1965 closed?)", err);
    }
    SCHANNEL_CRED cred = { 0 };
    cred.dwVersion = SCHANNEL_CRED_VERSION;
    cred.grbitEnabledProtocols = 0;
    cred.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS |
        SCH_CRED_IGNORE_NO_REVOCATION_CHECK | SCH_CRED_IGNORE_UNKNOWN_CA |
        SCH_CRED_IGNORE_CERT_CN_INVALID | SCH_CRED_IGNORE_CERT_DATE_INVALID;
    CredHandle hCred;
    SECURITY_STATUS sc = AcquireCredentialsHandleA(NULL, (LPSTR)UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL, &cred, NULL, NULL, &hCred, NULL);
    if (sc != SEC_E_OK) return ReturnError("AcquireCredentialsHandle failed", sc);
    CtxtHandle hCtx;
    SecBuffer outBufs[1] = { {0, SECBUFFER_TOKEN, NULL} };
    SecBufferDesc outDesc = { SECBUFFER_VERSION, 1, outBufs };
    DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
        ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION;
    DWORD outFlags;
    sc = InitializeSecurityContextA(&hCred, NULL, host, flags, 0, 0, NULL, 0, &hCtx, &outDesc, &outFlags, NULL);
    if (sc != SEC_I_CONTINUE_NEEDED) return ReturnError("InitializeSecurityContext 1 failed", sc);
    send(sock, (const char*)outBufs[0].pvBuffer, outBufs[0].cbBuffer, 0);
    FreeContextBuffer(outBufs[0].pvBuffer);
    const int BUF_SIZE = 32768;
    char* inBuffer = (char*)HeapAlloc(GetProcessHeap(), 0, BUF_SIZE);
    int inLen = 0;
    bool handshakeComplete = false;
    bool readMore = true;
    while (!handshakeComplete) {
        if (readMore) {
            if (inLen >= BUF_SIZE) { HeapFree(GetProcessHeap(), 0, inBuffer); return ReturnError("TLS Handshake buffer overflow"); }
            int r = recv(sock, inBuffer + inLen, BUF_SIZE - inLen, 0);
            if (r <= 0) { HeapFree(GetProcessHeap(), 0, inBuffer); return ReturnError("Socket closed during TLS Handshake", WSAGetLastError()); }
            inLen += r;
        }
        readMore = false;
        SecBuffer inBufs[2] = {{(DWORD)inLen, SECBUFFER_TOKEN, inBuffer}, {0, SECBUFFER_EMPTY, NULL}};
        SecBufferDesc inDesc = {SECBUFFER_VERSION, 2, inBufs};
        outBufs[0] = { 0, SECBUFFER_TOKEN, NULL };
        sc = InitializeSecurityContextA(&hCred, &hCtx, host, flags, 0, 0, &inDesc, 0, NULL, &outDesc, &outFlags, NULL);
        if (sc == SEC_E_OK || sc == SEC_I_CONTINUE_NEEDED || sc == SEC_I_INCOMPLETE_CREDENTIALS) {
            if (outBufs[0].cbBuffer > 0 && outBufs[0].pvBuffer) {
                send(sock, (const char*)outBufs[0].pvBuffer, outBufs[0].cbBuffer, 0);
                FreeContextBuffer(outBufs[0].pvBuffer);
            }
            if (sc == SEC_E_OK) {handshakeComplete = true; break;}
            if (inBufs[1].BufferType == SECBUFFER_EXTRA) {
                memmove(inBuffer, inBuffer + (inLen - inBufs[1].cbBuffer), inBufs[1].cbBuffer);
                inLen = inBufs[1].cbBuffer;
                readMore = false;
            }
            else {
                inLen = 0;
                readMore = true;
            }
            if (sc == SEC_I_INCOMPLETE_CREDENTIALS) readMore = false;
        }
        else if (sc == SEC_E_INCOMPLETE_MESSAGE) { readMore = true; }
        else {HeapFree(GetProcessHeap(), 0, inBuffer); return ReturnError("TLS Handshake failed", sc);}
    }
    char* req = (char*)HeapAlloc(GetProcessHeap(), 0, 2048);
    int reqLen = 0;
    if (*pathStart == '\0') reqLen = wsprintfA(req, "%s/\r\n", url);
    else reqLen = wsprintfA(req, "%s\r\n", url);
    SecPkgContext_StreamSizes sizes = {0};
    if (QueryContextAttributes(&hCtx, SECPKG_ATTR_STREAM_SIZES, &sizes) != SEC_E_OK) {
        HeapFree(GetProcessHeap(), 0, req); HeapFree(GetProcessHeap(), 0, inBuffer); return ReturnError("QueryContextAttributes failed");
    }
    char* sendBuf = (char*)HeapAlloc(GetProcessHeap(), 0, sizes.cbHeader + reqLen + sizes.cbTrailer);
    memcpy(sendBuf + sizes.cbHeader, req, reqLen);
    HeapFree(GetProcessHeap(), 0, req);
    SecBuffer encryptBufs[4];
    encryptBufs[0] = {sizes.cbHeader, SECBUFFER_STREAM_HEADER, sendBuf};
    encryptBufs[1] = {(DWORD)reqLen, SECBUFFER_DATA, sendBuf + sizes.cbHeader};
    encryptBufs[2] = {sizes.cbTrailer, SECBUFFER_STREAM_TRAILER, sendBuf + sizes.cbHeader + reqLen};
    encryptBufs[3] = {0, SECBUFFER_EMPTY, NULL};
    SecBufferDesc encryptDesc = {SECBUFFER_VERSION, 4, encryptBufs};
    EncryptMessage(&hCtx, 0, &encryptDesc, 0);
    send(sock, sendBuf, encryptBufs[0].cbBuffer + encryptBufs[1].cbBuffer + encryptBufs[2].cbBuffer, 0);
    HeapFree(GetProcessHeap(), 0, sendBuf);
    size_t responseCapacity = 1024 * 1024;
    char* fullResponse = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, responseCapacity);
    size_t totalDecrypted = 0;
    inLen = 0;
    while (true) {
        if (inLen >= BUF_SIZE) break;
        int r = recv(sock, inBuffer + inLen, BUF_SIZE - inLen, 0);
        if (r <= 0) break;
        inLen += r;
        while (inLen > 0) {
            SecBuffer decBufs[4];
            decBufs[0] = {(DWORD)inLen, SECBUFFER_DATA, inBuffer};
            decBufs[1] = {0, SECBUFFER_EMPTY, NULL};
            decBufs[2] = {0, SECBUFFER_EMPTY, NULL};
            decBufs[3] = {0, SECBUFFER_EMPTY, NULL};
            SecBufferDesc decDesc = {SECBUFFER_VERSION, 4, decBufs};
            sc = DecryptMessage(&hCtx, &decDesc, 0, NULL);
            if (sc == SEC_E_OK) {
                if (decBufs[1].cbBuffer > 0) {
                    if (totalDecrypted + decBufs[1].cbBuffer + 1 > responseCapacity) {
                        size_t newCap = responseCapacity * 2;
                        if (newCap < totalDecrypted + decBufs[1].cbBuffer + 1) {
                            newCap = totalDecrypted + decBufs[1].cbBuffer + 1024 * 1024;
                        }
                        char* newResponse = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newCap);
                        if (newResponse) {
                            memcpy(newResponse, fullResponse, totalDecrypted);
                            HeapFree(GetProcessHeap(), 0, fullResponse);
                            fullResponse = newResponse;
                            responseCapacity = newCap;
                        }
                        else {
                            break;
                        }
                    }
                    memcpy(fullResponse + totalDecrypted, decBufs[1].pvBuffer, decBufs[1].cbBuffer);
                    totalDecrypted += decBufs[1].cbBuffer;
                }
                if (decBufs[3].BufferType == SECBUFFER_EXTRA) {
                    memmove(inBuffer, inBuffer + (inLen - decBufs[3].cbBuffer), decBufs[3].cbBuffer);
                    inLen = decBufs[3].cbBuffer;
                }
                else {
                    inLen = 0;
                }
            }
            else if (sc == SEC_E_INCOMPLETE_MESSAGE) { break; }
            else if (sc == SEC_I_CONTEXT_EXPIRED) { inLen = 0; break; }
            else { inLen = 0; break; }
        }
    }
    HeapFree(GetProcessHeap(), 0, inBuffer);
    DeleteSecurityContext(&hCtx); FreeCredentialsHandle(&hCred); closesocket(sock); WSACleanup();
    if (totalDecrypted == 0) {
        HeapFree(GetProcessHeap(), 0, fullResponse);
        return ReturnError("No data received / Decryption failed");
    }
    fullResponse[totalDecrypted] = '\0';
    return fullResponse;
}
void* Gemini_FetchAndParse(const char* url) {
    char* rawData = FetchGemini(url);
    if (!rawData) return nullptr;
    GeminiContext* ctx = new GeminiContext();
    ctx->fH1 = CreateFontA(-28, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    ctx->fH2 = CreateFontA(-24, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    ctx->fH3 = CreateFontA(-20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    ctx->fBody = CreateFontA(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    ctx->fPre = CreateFontA(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Consolas");
    if (rawData[0] == 'E' && rawData[1] == 'R' && rawData[2] == 'R' && rawData[3] == 'O' &&
        rawData[4] == 'R' && rawData[5] == '_' && rawData[6] == 'M' && rawData[7] == 'S' &&
        rawData[8] == 'G' && rawData[9] == ':') {
        GemElem* e1 = ctx->add_elem(); e1->type = GType::H1; lstrcpyA(e1->text, "TLS / Network Error");
        GemElem* e2 = ctx->add_elem(); e2->type = GType::TEXT; lstrcpynA(e2->text, rawData + 11, 4095);
        HeapFree(GetProcessHeap(), 0, rawData); return ctx;
    }
    char* headerEnd = rawData;
    while (*headerEnd && *headerEnd != '\n') headerEnd++;
    if (*headerEnd == '\n') headerEnd++;
    char statusCode = rawData[0];
    if (statusCode == '3') {
        GemElem* e1 = ctx->add_elem(); e1->type = GType::H1; lstrcpyA(e1->text, "Redirect Required");
        char* urlStart = rawData + 3;
        char* urlEnd = urlStart;
        while (*urlEnd && *urlEnd != '\r' && *urlEnd != '\n') urlEnd++;
        *urlEnd = '\0';
        GemElem* e2 = ctx->add_elem(); e2->type = GType::TEXT; wsprintfA(e2->text, "Server redirected to: %s", urlStart);
        HeapFree(GetProcessHeap(), 0, rawData); return ctx;
    }
    else if (statusCode != '2') {
        GemElem* e1 = ctx->add_elem(); e1->type = GType::H1; wsprintfA(e1->text, "Server Error: %c%c", rawData[0], rawData[1]);
        char* msgStart = rawData + 3;
        char* msgEnd = msgStart;
        while (*msgEnd && *msgEnd != '\r' && *msgEnd != '\n') msgEnd++;
        *msgEnd = '\0';
        GemElem* e2 = ctx->add_elem(); e2->type = GType::TEXT; lstrcpynA(e2->text, msgStart, 4095);
        HeapFree(GetProcessHeap(), 0, rawData); return ctx;
    }
    char* p = headerEnd;
    bool inPre = false;
    while (*p) {
        char* lineEnd = p;
        while (*lineEnd && *lineEnd != '\n') lineEnd++;
        int len = (int)(lineEnd - p);
        if (len > 0 && *(lineEnd - 1) == '\r') len--;
        if (len >= 3 && p[0] == '`' && p[1] == '`' && p[2] == '`') {
            inPre = !inPre;
        }
        else if (inPre) {
            GemElem* e = ctx->add_elem();
            e->type = GType::PRE;
            int cplen = len > 4095 ? 4095 : len;
            memcpy(e->text, p, cplen);
            e->text[cplen] = '\0';
        }
        else if (len >= 2 && p[0] == '=' && p[1] == '>') {
            GemElem* e = ctx->add_elem();
            e->type = GType::LINK;
            int offset = 2;
            while (offset < len && (p[offset] == ' ' || p[offset] == '\t')) offset++;
            int urlEnd = offset;
            while (urlEnd < len&& p[urlEnd] != ' ' && p[urlEnd] != '\t') urlEnd++;
            int urlLen = urlEnd - offset;
            int cpUrl = urlLen > 2047 ? 2047 : urlLen;
            memcpy(e->url, p + offset, cpUrl);
            e->url[cpUrl] = '\0';
            int labelStart = urlEnd;
            while (labelStart < len && (p[labelStart] == ' ' || p[labelStart] == '\t')) labelStart++;
            if (labelStart < len) {
                int labelLen = len - labelStart;
                int cpText = labelLen > 4095 ? 4095 : labelLen;
                memcpy(e->text, p + labelStart, cpText);
                e->text[cpText] = '\0';
            }
            else {
                memcpy(e->text, e->url, 2047);
            }
        }
        else if (len >= 1 && p[0] == '#') {
            GemElem* e = ctx->add_elem();
            e->type = GType::H1;
            int cplen = (len - 1) > 4095 ? 4095 : (len - 1);
            memcpy(e->text, p + 1, cplen);
            e->text[cplen] = '\0';
        }
        else {
            GemElem* e = ctx->add_elem();
            e->type = GType::TEXT;
            int cplen = len > 4095 ? 4095 : len;
            memcpy(e->text, p, cplen);
            e->text[cplen] = '\0';
        }
        p = *lineEnd ? lineEnd + 1 : lineEnd;
    }
    HeapFree(GetProcessHeap(), 0, rawData);
    return ctx;
}
void Gemini_Render(void* c, HDC dc, int w, int h, int scroll_y) {
    GeminiContext* ctx = (GeminiContext*)c;
    ctx->last_scroll = scroll_y;
    if (ctx->last_w != w) {
        ctx->UpdateLayout(dc, w);
    }
    if (ctx->cacheBmp) {
        BitBlt(dc, 0, 0, w, h, ctx->cacheDC, 0, scroll_y, SRCCOPY);
    }
    else {
        SetBkMode(dc, TRANSPARENT);
        for (size_t i = 0; i < ctx->elemCount; ++i) {
            GemElem& e = ctx->elements[i];
            int draw_y = e.r.top - scroll_y;
            int draw_bottom = e.r.bottom - scroll_y;
            if (draw_bottom < 0) continue;
            if (draw_y > h) break;
            HFONT f = ctx->fBody;
            COLORREF col = RGB(160, 160, 170);
            if (e.type == GType::H1) {f = ctx->fH1; col = RGB(255, 255, 255);}
            else if (e.type == GType::LINK) {f = ctx->fBody; col = RGB(50, 150, 255);}
            else if (e.type == GType::PRE) {f = ctx->fPre; col = RGB(100, 200, 100);}

            auto oldF = SelectObject(dc, f);
            SetTextColor(dc, col);
            int wlen = MultiByteToWideChar(CP_UTF8, 0, e.text, -1, ctx->render_wbuf, 4096);
            if (wlen > 0) {
                RECT r = e.r;
                r.top -= scroll_y;
                r.bottom -= scroll_y;
                DrawTextW(dc, ctx->render_wbuf, wlen - 1, &r, DT_WORDBREAK);
            }
            SelectObject(dc, oldF);
        }
    }
}
void Gemini_Free(void* c) { delete (GeminiContext*)c; }
int Gemini_GetHeight(void* c) { return ((GeminiContext*)c)->totalHeight; }
void Gemini_OnMouseDown(void* c, int x, int y, int button) {
    if (button != 1) return;
    GeminiContext* ctx = (GeminiContext*)c;
    int abs_y = y + ctx->last_scroll;
    for (size_t i = 0; i < ctx->elemCount; ++i) {
        if (ctx->elements[i].type == GType::LINK) {
            RECT r = ctx->elements[i].r;
            if (x >= r.left && x <= r.right && abs_y >= r.top && abs_y <= r.bottom) {
                g_host->navigate_to(ctx->elements[i].url);
                return;
            }
        }
    }
}
void Gemini_OnMouseMove(void* c, int x, int y) {
    GeminiContext* ctx = (GeminiContext*)c;
    int abs_y = y + ctx->last_scroll;
    bool overLink = false;
    for (size_t i = 0; i < ctx->elemCount; ++i) {
        if (ctx->elements[i].type == GType::LINK) {
            RECT r = ctx->elements[i].r;
            if (x >= r.left && x <= r.right && abs_y >= r.top && abs_y <= r.bottom) {
                overLink = true;
                break;
            }
        }
    }
    if (overLink) SetCursor(LoadCursor(NULL, IDC_HAND));
    else SetCursor(LoadCursor(NULL, IDC_ARROW));
}
extern "C" __declspec(dllexport) int lume_plugin_init(lua_State* L, LumeHostAPI* api) {
    g_host = api;
    CustomProtocolHandler h;
    h.scheme = "gemini";
    h.fetch_and_parse = Gemini_FetchAndParse;
    h.render_page = Gemini_Render;
    h.free_page = Gemini_Free;
    h.get_document_height = Gemini_GetHeight;
    h.on_mouse_down = Gemini_OnMouseDown;
    h.on_mouse_move = Gemini_OnMouseMove;
    g_host->register_protocol_engine(h);

    return 0;
}
extern "C" __declspec(dllexport) void lume_plugin_shutdown() {}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {return TRUE;}