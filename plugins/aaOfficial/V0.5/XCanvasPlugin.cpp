#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <gdiplus.h>
#include <string>
#include <map>
#include <vector>
#pragma comment(lib, "gdiplus.lib")
#include "lume_plugin.h"
std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}
struct DXContext {
    HWND hwnd = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* renderTargetView = nullptr;
    ID3D11Texture2D* depthStencilBuffer = nullptr;
    ID3D11DepthStencilView* depthStencilView = nullptr;
    ID3D11Buffer* vertexBuffer = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* inputLayout = nullptr;
    ID3D11RasterizerState* rsNone = nullptr;
    ID3D11RasterizerState* rsFront = nullptr;
    ID3D11RasterizerState* rsBack = nullptr;
    ID3D11DepthStencilState* dsOn = nullptr;
    ID3D11DepthStencilState* dsOff = nullptr;
    ID3D11BlendState* bsNone = nullptr;
    ID3D11BlendState* bsAlpha = nullptr;
    ID3D11BlendState* bsAdd = nullptr;
    std::map<std::string, ID3D11ShaderResourceView*> textures;
    std::map<std::string, ID3D11SamplerState*> samplers;
    std::map<std::string, ID3D11Buffer*> constantBuffers;
    int width = 0, height = 0;
    bool initialized = false;
};
static std::map<std::string, DXContext> g_dxContexts;
static LumeHostAPI* g_host = nullptr;
static ULONG_PTR g_gdiplusToken;
static WNDPROC g_oldStaticProc = nullptr;
static LRESULT CALLBACK DxWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) {
        return HTTRANSPARENT;
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return CallWindowProcA(g_oldStaticProc, hwnd, msg, wp, lp);
}
bool InitDX11(DXContext& ctx, HWND parentHwnd, int w, int h) {
    if (ctx.initialized) return true;
    ctx.hwnd = CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, w, h, parentHwnd, nullptr, GetModuleHandle(nullptr), nullptr);
    if (!ctx.hwnd) return false;
    g_oldStaticProc = (WNDPROC)SetWindowLongPtrA(ctx.hwnd, GWLP_WNDPROC, (LONG_PTR)DxWindowProc);
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1; scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width = w; scd.BufferDesc.Height = h;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; scd.OutputWindow = ctx.hwnd;
    scd.SampleDesc.Count = 1; scd.Windowed = TRUE; scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevelArray, 1, D3D11_SDK_VERSION, &scd, &ctx.swapChain, &ctx.device, nullptr, &ctx.context);
    if (FAILED(hr)) return false;
    ID3D11Texture2D* pBackBuffer = nullptr;
    ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (pBackBuffer) {
        ctx.device->CreateRenderTargetView(pBackBuffer, nullptr, &ctx.renderTargetView);
        pBackBuffer->Release();
    }
    D3D11_RASTERIZER_DESC rd = {}; rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE; ctx.device->CreateRasterizerState(&rd, &ctx.rsNone);
    rd.CullMode = D3D11_CULL_FRONT; ctx.device->CreateRasterizerState(&rd, &ctx.rsFront);
    rd.CullMode = D3D11_CULL_BACK; ctx.device->CreateRasterizerState(&rd, &ctx.rsBack);
    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable = TRUE; dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc = D3D11_COMPARISON_LESS;
    ctx.device->CreateDepthStencilState(&dsd, &ctx.dsOn);
    dsd.DepthEnable = FALSE; ctx.device->CreateDepthStencilState(&dsd, &ctx.dsOff);
    D3D11_BLEND_DESC bd = {};
    bd.RenderTarget[0].BlendEnable = FALSE; bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ctx.device->CreateBlendState(&bd, &ctx.bsNone);
    bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA; bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    ctx.device->CreateBlendState(&bd, &ctx.bsAlpha);
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; ctx.device->CreateBlendState(&bd, &ctx.bsAdd);
    ctx.width = w; ctx.height = h; ctx.initialized = true;
    return true;
}
void ResizeDX11(DXContext& ctx, int w, int h) {
    if (!ctx.initialized || (ctx.width == w && ctx.height == h)) return;
    SetWindowPos(ctx.hwnd, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
    if (ctx.renderTargetView) { ctx.renderTargetView->Release(); ctx.renderTargetView = nullptr; }
    if (ctx.depthStencilView) { ctx.depthStencilView->Release(); ctx.depthStencilView = nullptr; }
    if (ctx.depthStencilBuffer) { ctx.depthStencilBuffer->Release(); ctx.depthStencilBuffer = nullptr; }
    ctx.swapChain->ResizeBuffers(1, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    ID3D11Texture2D* pBackBuffer = nullptr;
    ctx.swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (pBackBuffer) {
        ctx.device->CreateRenderTargetView(pBackBuffer, nullptr, &ctx.renderTargetView);
        pBackBuffer->Release();
    }
    ctx.width = w; ctx.height = h;
}
void CleanupDX11(DXContext& ctx) {
    if (!ctx.initialized) return;
    if (ctx.hwnd) {
        DestroyWindow(ctx.hwnd);
        ctx.hwnd = nullptr;
    }
    if (ctx.context) {
        ctx.context->ClearState();
        ctx.context->Flush();
    }
    for (auto& p : ctx.textures) p.second->Release(); ctx.textures.clear();
    for (auto& p : ctx.samplers) p.second->Release(); ctx.samplers.clear();
    for (auto& p : ctx.constantBuffers) p.second->Release(); ctx.constantBuffers.clear();
    if (ctx.rsNone) ctx.rsNone->Release();
    if (ctx.rsFront) ctx.rsFront->Release();
    if (ctx.rsBack) ctx.rsBack->Release();
    if (ctx.dsOn) ctx.dsOn->Release();
    if (ctx.dsOff) ctx.dsOff->Release();
    if (ctx.bsNone) ctx.bsNone->Release();
    if (ctx.bsAlpha) ctx.bsAlpha->Release();
    if (ctx.bsAdd) ctx.bsAdd->Release();
    if (ctx.vertexBuffer) ctx.vertexBuffer->Release();
    if (ctx.inputLayout) ctx.inputLayout->Release();
    if (ctx.vertexShader) ctx.vertexShader->Release();
    if (ctx.pixelShader) ctx.pixelShader->Release();
    if (ctx.depthStencilView) ctx.depthStencilView->Release();
    if (ctx.depthStencilBuffer) ctx.depthStencilBuffer->Release();
    if (ctx.renderTargetView) ctx.renderTargetView->Release();
    if (ctx.swapChain) ctx.swapChain->Release();
    if (ctx.context) ctx.context->Release();
    if (ctx.device) ctx.device->Release();
    ctx.initialized = false;
}
static DXContext* GetCtx(lua_State* L) {
    const char* id = g_host->p_luaL_checklstring(L, 1, nullptr);
    auto it = g_dxContexts.find(id);
    if (it != g_dxContexts.end() && it->second.initialized) return &it->second;
    return nullptr;
}
static int l_dx_begin(lua_State* L) {
    const char* id = g_host->p_luaL_checklstring(L, 1, nullptr);
    int w = (int)g_host->p_luaL_optinteger(L, 2, 400);
    int h = (int)g_host->p_luaL_optinteger(L, 3, 300);
    DXContext& ctx = g_dxContexts[id];
    if (!ctx.initialized) {
        if (!InitDX11(ctx, g_host->get_main_hwnd(), w, h)) { g_host->p_lua_pushboolean(L, 0); return 1; }
    }
    else ResizeDX11(ctx, w, h);
    g_host->p_lua_pushboolean(L, 1);
    return 1;
}
static int l_dx_create_depth_buffer(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    if (ctx->depthStencilView) { ctx->depthStencilView->Release(); ctx->depthStencilView = nullptr; }
    if (ctx->depthStencilBuffer) { ctx->depthStencilBuffer->Release(); ctx->depthStencilBuffer = nullptr; }
    D3D11_TEXTURE2D_DESC descDepth = {};
    descDepth.Width = ctx->width; descDepth.Height = ctx->height;
    descDepth.MipLevels = 1; descDepth.ArraySize = 1;
    descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descDepth.SampleDesc.Count = 1; descDepth.Usage = D3D11_USAGE_DEFAULT;
    descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ctx->device->CreateTexture2D(&descDepth, nullptr, &ctx->depthStencilBuffer);
    ctx->device->CreateDepthStencilView(ctx->depthStencilBuffer, nullptr, &ctx->depthStencilView);
    return 0;
}
static int l_dx_clear(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    float col[4] = { (float)g_host->p_luaL_optnumber(L, 2, 0), (float)g_host->p_luaL_optnumber(L, 3, 0), (float)g_host->p_luaL_optnumber(L, 4, 0), (float)g_host->p_luaL_optnumber(L, 5, 1) };
    ctx->context->ClearRenderTargetView(ctx->renderTargetView, col);
    D3D11_VIEWPORT vp = {}; vp.Width = (float)ctx->width; vp.Height = (float)ctx->height; vp.MaxDepth = 1.0f;
    ctx->context->RSSetViewports(1, &vp);
    return 0;
}
static int l_dx_clear_depth(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    if (ctx->depthStencilView) ctx->context->ClearDepthStencilView(ctx->depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
    return 0;
}
static int l_dx_set_blend_mode(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int mode = (int)g_host->p_luaL_optinteger(L, 2, 0);
    float bf[4] = { 0,0,0,0 };
    if (mode == 1) ctx->context->OMSetBlendState(ctx->bsAlpha, bf, 0xffffffff);
    else if (mode == 2) ctx->context->OMSetBlendState(ctx->bsAdd, bf, 0xffffffff);
    else ctx->context->OMSetBlendState(ctx->bsNone, bf, 0xffffffff);
    return 0;
}
static int l_dx_set_cull_mode(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int mode = (int)g_host->p_luaL_optinteger(L, 2, 0);
    if (mode == 1) ctx->context->RSSetState(ctx->rsBack);
    else if (mode == 2) ctx->context->RSSetState(ctx->rsFront);
    else ctx->context->RSSetState(ctx->rsNone);
    return 0;
}
static int l_dx_set_depth_test(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int enable = (int)g_host->p_luaL_optinteger(L, 2, 1);
    ctx->context->OMSetDepthStencilState(enable ? ctx->dsOn : ctx->dsOff, 1);
    return 0;
}
static int l_dx_create_buffer(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    const char* data = g_host->p_luaL_checklstring(L, 2, nullptr);
    std::vector<float> verts; const char* p = data;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        bool neg = false; if (*p == '-') { neg = true; p++; }
        else if (*p == '+') p++;
        float val = 0.0f;
        while (*p >= '0' && *p <= '9') { val = val * 10.0f + (*p - '0'); p++; }
        if (*p == '.') { p++; float frac = 1.0f; while (*p >= '0' && *p <= '9') { frac *= 0.1f; val += (*p - '0') * frac; p++; } }
        verts.push_back(neg ? -val : val);
    }
    if (verts.empty()) return 0;
    if (ctx->vertexBuffer) { ctx->vertexBuffer->Release(); ctx->vertexBuffer = nullptr; }
    D3D11_BUFFER_DESC bd = {}; bd.Usage = D3D11_USAGE_DEFAULT; bd.ByteWidth = (UINT)(verts.size() * sizeof(float)); bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {}; initData.pSysMem = verts.data();
    ctx->device->CreateBuffer(&bd, &initData, &ctx->vertexBuffer);
    return 0;
}
static int l_dx_load_shader(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    const char* vsCode = g_host->p_luaL_checklstring(L, 2, nullptr);
    const char* psCode = g_host->p_luaL_checklstring(L, 3, nullptr);
    ID3DBlob* vsBlob = nullptr; ID3DBlob* psBlob = nullptr; ID3DBlob* errBlob = nullptr;
    if (FAILED(D3DCompile(vsCode, lstrlenA(vsCode), nullptr, nullptr, nullptr, "VSMain", "vs_5_0", 0, 0, &vsBlob, &errBlob))) {
        if (errBlob) g_host->alert((const char*)errBlob->GetBufferPointer());
        if (errBlob) errBlob->Release(); return 0;
    }
    if (FAILED(D3DCompile(psCode, lstrlenA(psCode), nullptr, nullptr, nullptr, "PSMain", "ps_5_0", 0, 0, &psBlob, &errBlob))) {
        if (errBlob) g_host->alert((const char*)errBlob->GetBufferPointer());
        if (errBlob) errBlob->Release(); return 0;
    }

    if (ctx->vertexShader) ctx->vertexShader->Release();
    if (ctx->pixelShader) ctx->pixelShader->Release();
    if (ctx->inputLayout) ctx->inputLayout->Release();
    ctx->device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &ctx->vertexShader);
    ctx->device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ctx->pixelShader);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    ctx->device->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &ctx->inputLayout);
    vsBlob->Release(); psBlob->Release();
    return 0;
}
static int l_dx_create_constant_buffer(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    const char* name = g_host->p_luaL_checklstring(L, 2, nullptr);
    int size = (int)g_host->p_luaL_checkinteger(L, 3);
    int alignedSize = (size + 15) & ~15;
    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT; bd.ByteWidth = alignedSize; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Buffer* cb = nullptr;
    if (SUCCEEDED(ctx->device->CreateBuffer(&bd, nullptr, &cb))) {
        if (ctx->constantBuffers[name]) ctx->constantBuffers[name]->Release();
        ctx->constantBuffers[name] = cb;
    }
    return 0;
}
static int l_dx_update_constant_buffer(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    const char* name = g_host->p_luaL_checklstring(L, 2, nullptr);
    if (ctx->constantBuffers.find(name) == ctx->constantBuffers.end()) return 0;
    if (g_host->p_lua_type(L, 3) == 5) {
        std::vector<float> data;
        for (int i = 1; ; ++i) {
            g_host->p_lua_rawgeti(L, 3, i);
            if (g_host->p_lua_type(L, -1) == 0) { g_host->p_lua_settop(L, -2); break; }
            data.push_back((float)g_host->p_lua_tonumberx(L, -1, nullptr));
            g_host->p_lua_settop(L, -2);
        }
        if (!data.empty()) {
            ctx->context->UpdateSubresource(ctx->constantBuffers[name], 0, nullptr, data.data(), 0, 0);
        }
    }
    return 0;
}
static int l_dx_bind_constant_buffer(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int stage = (int)g_host->p_luaL_checkinteger(L, 2);
    int slot = (int)g_host->p_luaL_checkinteger(L, 3);
    const char* name = g_host->p_luaL_checklstring(L, 4, nullptr);
    if (ctx->constantBuffers.find(name) != ctx->constantBuffers.end()) {
        ID3D11Buffer* cb = ctx->constantBuffers[name];
        if (stage == 0) ctx->context->VSSetConstantBuffers(slot, 1, &cb);
        else ctx->context->PSSetConstantBuffers(slot, 1, &cb);
    }
    return 0;
}
static int l_dx_create_texture(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    const char* name = g_host->p_luaL_checklstring(L, 2, nullptr);
    const char* path = g_host->p_luaL_checklstring(L, 3, nullptr);
    Gdiplus::Bitmap bmp(utf8_to_wstring(path).c_str());
    if (bmp.GetLastStatus() != Gdiplus::Ok) return 0;
    int w = bmp.GetWidth(), h = bmp.GetHeight();
    Gdiplus::Rect rect(0, 0, w, h);
    Gdiplus::BitmapData bmpData;
    bmp.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bmpData);
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = bmpData.Scan0; initData.SysMemPitch = bmpData.Stride;
    ID3D11Texture2D* tex = nullptr;
    if (SUCCEEDED(ctx->device->CreateTexture2D(&td, &initData, &tex))) {
        ID3D11ShaderResourceView* srv = nullptr;
        ctx->device->CreateShaderResourceView(tex, nullptr, &srv);
        if (ctx->textures[name]) ctx->textures[name]->Release();
        ctx->textures[name] = srv;
        tex->Release();
    }
    bmp.UnlockBits(&bmpData);
    return 0;
}
static int l_dx_bind_texture(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int slot = (int)g_host->p_luaL_checkinteger(L, 2);
    const char* name = g_host->p_luaL_checklstring(L, 3, nullptr);
    if (ctx->textures.find(name) != ctx->textures.end()) {
        ID3D11ShaderResourceView* srv = ctx->textures[name];
        ctx->context->PSSetShaderResources(slot, 1, &srv);
    }
    return 0;
}
static int l_dx_create_sampler(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    const char* name = g_host->p_luaL_checklstring(L, 2, nullptr);
    int filter = (int)g_host->p_luaL_optinteger(L, 3, 1);
    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = filter == 0 ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP; sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP; sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    ID3D11SamplerState* sampler = nullptr;
    if (SUCCEEDED(ctx->device->CreateSamplerState(&sd, &sampler))) {
        if (ctx->samplers[name]) ctx->samplers[name]->Release();
        ctx->samplers[name] = sampler;
    }
    return 0;
}
static int l_dx_bind_sampler(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int slot = (int)g_host->p_luaL_checkinteger(L, 2);
    const char* name = g_host->p_luaL_checklstring(L, 3, nullptr);
    if (ctx->samplers.find(name) != ctx->samplers.end()) {
        ID3D11SamplerState* samp = ctx->samplers[name];
        ctx->context->PSSetSamplers(slot, 1, &samp);
    }
    return 0;
}
static int l_dx_set_transform(lua_State* L) {
    DXContext* ctx = GetCtx(L);
    if (!ctx) return 0;
    float r = (float)g_host->p_luaL_checknumber(L, 2);
    float g = (float)g_host->p_luaL_checknumber(L, 3);
    float b = (float)g_host->p_luaL_checknumber(L, 4);
    float a = (float)g_host->p_luaL_optnumber(L, 5, 1.0);
    struct TransformData {
        float matWorldViewProj[16];
        float tint[4];
    } cbData;
    cbData.tint[0] = r;
    cbData.tint[1] = g;
    cbData.tint[2] = b;
    cbData.tint[3] = a;
    if (g_host->p_lua_type(L, 6) == 5) {
        for (int i = 0; i < 16; ++i) {
            g_host->p_lua_rawgeti(L, 6, i + 1);
            cbData.matWorldViewProj[i] = (float)g_host->p_lua_tonumberx(L, -1, nullptr);
            g_host->p_lua_settop(L, -2);
        }
    }
    else {
        for (int i = 0; i < 16; i++) {
            cbData.matWorldViewProj[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }
    auto it = ctx->constantBuffers.find("cbTransform");
    if (it != ctx->constantBuffers.end()) {
        ctx->context->UpdateSubresource(it->second, 0, nullptr, &cbData, 0, 0);
    }
    return 0;
}
static int l_dx_draw(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int vertexCount = (int)g_host->p_luaL_checkinteger(L, 2);
    if (ctx->vertexBuffer) {
        ctx->context->OMSetRenderTargets(1, &ctx->renderTargetView, ctx->depthStencilView);
        ctx->context->IASetInputLayout(ctx->inputLayout);
        UINT stride = sizeof(float) * 9;
        UINT offset = 0;
        ctx->context->IASetVertexBuffers(0, 1, &ctx->vertexBuffer, &stride, &offset);
        ctx->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->context->VSSetShader(ctx->vertexShader, nullptr, 0);
        ctx->context->PSSetShader(ctx->pixelShader, nullptr, 0);
        ctx->context->Draw(vertexCount, 0);
    }
    return 0;
}
static int l_dx_present(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    ctx->swapChain->Present(1, 0);
    return 0;
}
static int l_dx_place(lua_State* L) {
    DXContext* ctx = GetCtx(L); if (!ctx) return 0;
    int x = (int)g_host->p_luaL_checknumber(L, 2);
    int y = (int)g_host->p_luaL_checknumber(L, 3);
    SetWindowPos(ctx->hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
    return 0;
}
static void OnPageReset() {
    for (auto& pair : g_dxContexts) CleanupDX11(pair.second);
    g_dxContexts.clear();
}
extern "C" __declspec(dllexport) void lume_plugin_init(lua_State* L, LumeHostAPI* hostAPI) {
    if (!hostAPI) return;
    g_host = hostAPI;
    g_host->register_on_reset(OnPageReset);
    if (L) {
        g_host->p_lua_pushcclosure(L, l_dx_begin, 0);
        g_host->p_lua_setglobal(L, "dx_begin");
        g_host->p_lua_pushcclosure(L, l_dx_clear, 0);
        g_host->p_lua_setglobal(L, "dx_clear");
        g_host->p_lua_pushcclosure(L, l_dx_create_buffer, 0);
        g_host->p_lua_setglobal(L, "dx_create_buffer");
        g_host->p_lua_pushcclosure(L, l_dx_load_shader, 0);
        g_host->p_lua_setglobal(L, "dx_load_shader");
        g_host->p_lua_pushcclosure(L, l_dx_draw, 0);
        g_host->p_lua_setglobal(L, "dx_draw");
        g_host->p_lua_pushcclosure(L, l_dx_present, 0);
        g_host->p_lua_setglobal(L, "dx_present");
        g_host->p_lua_pushcclosure(L, l_dx_place, 0);
        g_host->p_lua_setglobal(L, "dx_place");
        g_host->p_lua_pushcclosure(L, l_dx_create_depth_buffer, 0);
        g_host->p_lua_setglobal(L, "dx_create_depth_buffer");
        g_host->p_lua_pushcclosure(L, l_dx_clear_depth, 0);
        g_host->p_lua_setglobal(L, "dx_clear_depth");
        g_host->p_lua_pushcclosure(L, l_dx_set_blend_mode, 0);
        g_host->p_lua_setglobal(L, "dx_set_blend_mode");
        g_host->p_lua_pushcclosure(L, l_dx_set_cull_mode, 0);
        g_host->p_lua_setglobal(L, "dx_set_cull_mode");
        g_host->p_lua_pushcclosure(L, l_dx_set_depth_test, 0);
        g_host->p_lua_setglobal(L, "dx_set_depth_test");
        g_host->p_lua_pushcclosure(L, l_dx_create_constant_buffer, 0);
        g_host->p_lua_setglobal(L, "dx_create_constant_buffer");
        g_host->p_lua_pushcclosure(L, l_dx_update_constant_buffer, 0);
        g_host->p_lua_setglobal(L, "dx_update_constant_buffer");
        g_host->p_lua_pushcclosure(L, l_dx_bind_constant_buffer, 0);
        g_host->p_lua_setglobal(L, "dx_bind_constant_buffer");
        g_host->p_lua_pushcclosure(L, l_dx_create_texture, 0);
        g_host->p_lua_setglobal(L, "dx_create_texture");
        g_host->p_lua_pushcclosure(L, l_dx_bind_texture, 0);
        g_host->p_lua_setglobal(L, "dx_bind_texture");
        g_host->p_lua_pushcclosure(L, l_dx_create_sampler, 0);
        g_host->p_lua_setglobal(L, "dx_create_sampler");
        g_host->p_lua_pushcclosure(L, l_dx_bind_sampler, 0);
        g_host->p_lua_setglobal(L, "dx_bind_sampler");
        g_host->p_lua_pushcclosure(L, l_dx_set_transform, 0);
        g_host->p_lua_setglobal(L, "dx_set_transform");
    }
}
extern "C" __declspec(dllexport) void lume_plugin_shutdown() {
    OnPageReset();
    g_host = nullptr;
}