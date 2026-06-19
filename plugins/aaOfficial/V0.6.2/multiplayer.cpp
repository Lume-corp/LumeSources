// cl.exe /O2 /LD /GS- /GR- /EHsc- multiplayer.cpp /link /NODEFAULTLIB /ENTRY:DllMain ws2_32.lib kernel32.lib user32.lib
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#ifndef LUA_TNUMBER
#define LUA_TNUMBER 3
#endif
extern "C" {
    int _fltused = 0;
    void __std_terminate() { while(1); }
    int __CxxFrameHandler4() { return 0; }
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
        if (d < s) {while (count--) *d++ = *s++;}
        else {d += count; s += count; while (count--) *--d = *--s;}
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
LumeHostAPI* g_api = nullptr;
enum PktType : uint8_t { PKT_DATA = 0, PKT_PING = 1, PKT_PONG = 2 };
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t length;
    uint8_t type;
    uint32_t channel;
};
#pragma pack(pop)
enum class NetEvent {CONNECTED, DISCONNECTED, DATA, CONNECT_ERR};
struct NetMessage {
    NetEvent eventType;
    int clientId;
    char* data;
    size_t dataLen;
    uint32_t channel;
    NetMessage* next;
};
struct ClientInfo {
    SOCKET sock;
    int id;
    bool active;
    volatile LONG64 bytesSent;
    volatile LONG64 bytesRecv;
    uint32_t pingMs;
    char* rxBuf;
    int rxCap;
    int rxLen;
};
enum class NetRole {NONE, SERVER, CLIENT};
NetRole g_role = NetRole::NONE;
SOCKET g_serverSock = INVALID_SOCKET;
ClientInfo* g_clients = nullptr;
int g_maxClients = 0;
int g_nextClientId = 1;
HANDLE g_netThread = NULL;
HANDLE g_connectThread = NULL;
volatile LONG g_runThread = 0;
CRITICAL_SECTION g_queueCS;
NetMessage* g_queueHead = nullptr;
NetMessage* g_queueTail = nullptr;
CRITICAL_SECTION g_netCS;
void EnqueueMessage(NetEvent type, int clientId, const char* data, size_t dataLen, uint32_t channel = 0) {
    NetMessage* msg = new NetMessage();
    msg->eventType = type;
    msg->clientId = clientId;
    msg->channel = channel;
    if (data && dataLen > 0) {
        msg->data = (char*)HeapAlloc(GetProcessHeap(), 0, dataLen + 1);
        if (msg->data) {
            memcpy(msg->data, data, dataLen);
            msg->data[dataLen] = '\0';
        }
        msg->dataLen = dataLen;
    }
    else {
        msg->data = nullptr;
        msg->dataLen = 0;
    }
    msg->next = nullptr;
    EnterCriticalSection(&g_queueCS);
    if (!g_queueHead) g_queueHead = g_queueTail = msg;
    else { g_queueTail->next = msg; g_queueTail = msg; }
    LeaveCriticalSection(&g_queueCS);
}
NetMessage* DequeueMessage() {
    EnterCriticalSection(&g_queueCS);
    NetMessage* msg = g_queueHead;
    if (msg) {
        g_queueHead = msg->next;
        if (!g_queueHead) g_queueTail = nullptr;
    }
    LeaveCriticalSection(&g_queueCS);
    return msg;
}
void SendPacket(ClientInfo& client, uint8_t type, uint32_t channel, const char* data, int len) {
    if (!client.active || client.sock == INVALID_SOCKET) return;
    int totalSize = sizeof(PacketHeader) + len;
    char* buf = (char*)HeapAlloc(GetProcessHeap(), 0, totalSize);
    if (!buf) return;
    PacketHeader* hdr = (PacketHeader*)buf;
    hdr->length = len;
    hdr->type = type;
    hdr->channel = channel;
    if (len > 0 && data) memcpy(buf + sizeof(PacketHeader), data, len);
    int totalSent = 0;
    while (totalSent < totalSize) {
        int s = send(client.sock, buf + totalSent, totalSize - totalSent, 0);
        if (s == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEWOULDBLOCK) {
                fd_set writeSet; FD_ZERO(&writeSet); FD_SET(client.sock, &writeSet);
                timeval tv = { 0, 10000 };
                select(0, NULL, &writeSet, NULL, &tv);
                continue;
            }
            break;
        }
        totalSent += s;
        InterlockedExchangeAdd64(&client.bytesSent, s);
    }
    HeapFree(GetProcessHeap(), 0, buf);
}
void CleanClient(ClientInfo& client) {
    if (client.sock != INVALID_SOCKET) {
        closesocket(client.sock);
        client.sock = INVALID_SOCKET;
    }
    if (client.rxBuf) {
        HeapFree(GetProcessHeap(), 0, client.rxBuf);
        client.rxBuf = nullptr;
    }
    client.rxCap = 0;
    client.rxLen = 0;
    client.active = false;
}
void DisconnectAll() {
    InterlockedExchange(&g_runThread, 0);
    if (g_serverSock != INVALID_SOCKET) {
        closesocket(g_serverSock);
        g_serverSock = INVALID_SOCKET;
    }
    if (g_clients) {
        for (int i = 0; i < g_maxClients; ++i) {
            if (g_clients[i].sock != INVALID_SOCKET) closesocket(g_clients[i].sock);
        }
    }
    if (g_netThread) {
        WaitForSingleObject(g_netThread, INFINITE);
        CloseHandle(g_netThread);
        g_netThread = NULL;
    }
    if (g_connectThread) {
        WaitForSingleObject(g_connectThread, INFINITE);
        CloseHandle(g_connectThread);
        g_connectThread = NULL;
    }
    EnterCriticalSection(&g_netCS);
    if (g_clients) {
        for (int i = 0; i < g_maxClients; ++i) CleanClient(g_clients[i]);
        HeapFree(GetProcessHeap(), 0, g_clients);
        g_clients = nullptr;
    }
    g_role = NetRole::NONE;
    g_maxClients = 0;
    g_nextClientId = 1;
    LeaveCriticalSection(&g_netCS);
    NetMessage* msg;
    while ((msg = DequeueMessage()) != nullptr) {
        if (msg->data) HeapFree(GetProcessHeap(), 0, msg->data);
        delete msg;
    }
}
DWORD WINAPI NetworkThread(LPVOID) {
    uint64_t lastPingBroadcast = GetTickCount64();
    const int TEMP_BUF_SIZE = 4096;
    char* tempBuf = (char*)HeapAlloc(GetProcessHeap(), 0, TEMP_BUF_SIZE);
    if (!tempBuf) return 1;
    while (InterlockedCompareExchange(&g_runThread, 0, 0) == 1) {
        fd_set readSet;
        FD_ZERO(&readSet);
        EnterCriticalSection(&g_netCS);
        SOCKET srvSock = g_serverSock;
        if (g_role == NetRole::SERVER && srvSock != INVALID_SOCKET) {
            FD_SET(srvSock, &readSet);
        }
        for (int i = 0; i < g_maxClients; ++i) {
            if (g_clients[i].active && g_clients[i].sock != INVALID_SOCKET) {
                FD_SET(g_clients[i].sock, &readSet);
            }
        }
        LeaveCriticalSection(&g_netCS);
        timeval tv = {0, 50000};
        int selectRes = select(0, &readSet, NULL, NULL, &tv);
        EnterCriticalSection(&g_netCS);
        uint64_t now = GetTickCount64();
        if (now - lastPingBroadcast > 3000) {
            for (int i = 0; i < g_maxClients; ++i) {
                if (g_clients[i].active) {
                    uint64_t pingTimestamp = GetTickCount64();
                    SendPacket(g_clients[i], PKT_PING, 0, (char*)&pingTimestamp, sizeof(uint64_t));
                }
            }
            lastPingBroadcast = now;
        }
        if (selectRes > 0) {
            if (g_role == NetRole::SERVER && srvSock != INVALID_SOCKET && FD_ISSET(srvSock, &readSet)) {
                SOCKET newClient = accept(srvSock, NULL, NULL);
                if (newClient != INVALID_SOCKET) {
                    u_long mode = 1; ioctlsocket(newClient, FIONBIO, &mode);
                    bool added = false;
                    for (int i = 0; i < g_maxClients; ++i) {
                        if (!g_clients[i].active) {
                            g_clients[i].sock = newClient;
                            g_clients[i].id = g_nextClientId++;
                            g_clients[i].active = true;
                            g_clients[i].bytesSent = g_clients[i].bytesRecv = g_clients[i].pingMs = 0;
                            g_clients[i].rxCap = g_clients[i].rxLen = 0;
                            g_clients[i].rxBuf = nullptr;
                            added = true;
                            EnqueueMessage(NetEvent::CONNECTED, g_clients[i].id, nullptr, 0);
                            break;
                        }
                    }
                    if (!added) closesocket(newClient);
                }
            }
            for (int i = 0; i < g_maxClients; ++i) {
                ClientInfo& cl = g_clients[i];
                if (cl.active && cl.sock != INVALID_SOCKET && FD_ISSET(cl.sock, &readSet)) {
                    int r = recv(cl.sock, tempBuf, TEMP_BUF_SIZE, 0);
                    if (r > 0) {
                        InterlockedExchangeAdd64((volatile LONG64*)&cl.bytesRecv, r);
                        if (cl.rxLen + r > cl.rxCap) {
                            int newCap = cl.rxCap == 0 ? 8192 : cl.rxCap * 2;
                            while (newCap < cl.rxLen + r) newCap *= 2;
                            char* newBuf = (char*)HeapAlloc(GetProcessHeap(), 0, newCap);
                            if (newBuf) {
                                if (cl.rxBuf) {
                                    memcpy(newBuf, cl.rxBuf, cl.rxLen);
                                    HeapFree(GetProcessHeap(), 0, cl.rxBuf);
                                }
                                cl.rxBuf = newBuf;
                                cl.rxCap = newCap;
                            }
                        }
                        if (cl.rxBuf) {
                            memcpy(cl.rxBuf + cl.rxLen, tempBuf, r);
                            cl.rxLen += r;
                            while (cl.rxLen >= sizeof(PacketHeader)) {
                                PacketHeader* hdr = (PacketHeader*)cl.rxBuf;
                                if (hdr->length > 10 * 1024 * 1024) {
                                    r = 0; break;
                                }
                                int pktSize = sizeof(PacketHeader) + hdr->length;
                                if (cl.rxLen >= pktSize) {
                                    uint8_t type = hdr->type;
                                    uint32_t channel = hdr->channel;
                                    char* payload = nullptr;
                                    if (hdr->length > 0) {
                                        payload = (char*)HeapAlloc(GetProcessHeap(), 0, hdr->length + 1);
                                        if (payload) {
                                            memcpy(payload, cl.rxBuf + sizeof(PacketHeader), hdr->length);
                                            payload[hdr->length] = '\0';
                                        }
                                    }
                                    if (type == PKT_DATA) {
                                        EnqueueMessage(NetEvent::DATA, cl.id, payload, hdr->length, channel);
                                    }
                                    else if (type == PKT_PING) {
                                        SendPacket(cl, PKT_PONG, 0, payload, hdr->length);
                                    }
                                    else if (type == PKT_PONG && hdr->length == sizeof(uint64_t) && payload) {
                                        uint64_t origin = *(uint64_t*)payload;
                                        cl.pingMs = (uint32_t)(GetTickCount64() - origin);
                                    }
                                    if (payload) HeapFree(GetProcessHeap(), 0, payload);
                                    cl.rxLen -= pktSize;
                                    if (cl.rxLen > 0) memmove(cl.rxBuf, cl.rxBuf + pktSize, cl.rxLen);
                                }
                                else {
                                    break;
                                }
                            }
                        }
                    }
                    if (r <= 0) {
                        int err = WSAGetLastError();
                        if (r == 0 || (r == SOCKET_ERROR && err != WSAEWOULDBLOCK)) {
                            EnqueueMessage(NetEvent::DISCONNECTED, cl.id, nullptr, 0);
                            CleanClient(cl);
                            if (g_role == NetRole::CLIENT) InterlockedExchange(&g_runThread, 0);
                        }
                    }
                }
            }
        }
        LeaveCriticalSection(&g_netCS);
    }
    HeapFree(GetProcessHeap(), 0, tempBuf);
    return 0;
}
struct ConnectArgs {
    char host[256];
    int port;
};
DWORD WINAPI ConnectThread(LPVOID param) {
    ConnectArgs* args = (ConnectArgs*)param;
    addrinfo hints = {0}, * res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    char portStr[16];
    wsprintfA(portStr, "%d", args->port);
    if (getaddrinfo(args->host, portStr, &hints, &res) != 0) {
        EnqueueMessage(NetEvent::CONNECT_ERR, 0, nullptr, 0);
        HeapFree(GetProcessHeap(), 0, args);
        return 0;
    }
    SOCKET cSock = INVALID_SOCKET;
    for (addrinfo* ptr = res; ptr != nullptr; ptr = ptr->ai_next) {
        if (InterlockedCompareExchange(&g_runThread, 0, 0) == 0) break;
        cSock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (cSock == INVALID_SOCKET) continue;
        if (connect(cSock, ptr->ai_addr, (int)ptr->ai_addrlen) != SOCKET_ERROR) break;
        closesocket(cSock);
        cSock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (cSock != INVALID_SOCKET && InterlockedCompareExchange(&g_runThread, 0, 0) == 1) {
        u_long mode = 1;
        ioctlsocket(cSock, FIONBIO, &mode);
        EnterCriticalSection(&g_netCS);
        g_role = NetRole::CLIENT;
        g_maxClients = 1;
        g_clients = (ClientInfo*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ClientInfo));
        if (g_clients) {
            g_clients[0].sock = cSock;
            g_clients[0].id = 0;
            g_clients[0].active = true;
            g_clients[0].bytesSent = g_clients[0].bytesRecv = g_clients[0].pingMs = 0;
            g_clients[0].rxCap = g_clients[0].rxLen = 0;
            g_clients[0].rxBuf = nullptr;
        }
        LeaveCriticalSection(&g_netCS);
        HANDLE hNet = CreateThread(NULL, 0, NetworkThread, NULL, 0, NULL);
        if (hNet) {
            g_netThread = hNet;
            EnqueueMessage(NetEvent::CONNECTED, 0, nullptr, 0);
        }
    }
    else {
        if (cSock != INVALID_SOCKET) closesocket(cSock);
        EnqueueMessage(NetEvent::CONNECT_ERR, 0, nullptr, 0);
        InterlockedExchange(&g_runThread, 0);
    }
    HeapFree(GetProcessHeap(), 0, args);
    return 0;
}
int l_mp_host(lua_State* L) {
    EnterCriticalSection(&g_netCS);
    if (g_role != NetRole::NONE) {
        LeaveCriticalSection(&g_netCS);
        return 0;
    }
    if (g_netThread) {
        WaitForSingleObject(g_netThread, INFINITE);
        CloseHandle(g_netThread);
        g_netThread = NULL;
    }
    int port = (int)g_api->p_luaL_checkinteger(L, 1);
    g_maxClients = (int)g_api->p_luaL_optinteger(L, 2, 64);
    g_serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long mode = 1; ioctlsocket(g_serverSock, FIONBIO, &mode);
    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(g_serverSock, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR &&
        listen(g_serverSock, SOMAXCONN) != SOCKET_ERROR) {
        g_role = NetRole::SERVER;
        g_clients = (ClientInfo*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ClientInfo) * g_maxClients);
        InterlockedExchange(&g_runThread, 1);
        LeaveCriticalSection(&g_netCS);
        g_netThread = CreateThread(NULL, 0, NetworkThread, NULL, 0, NULL);
        g_api->p_lua_pushboolean(L, 1);
    }
    else {
        closesocket(g_serverSock);
        g_serverSock = INVALID_SOCKET;
        LeaveCriticalSection(&g_netCS);
        g_api->p_lua_pushboolean(L, 0);
    }
    return 1;
}
int l_mp_connect(lua_State* L) {
    EnterCriticalSection(&g_netCS);
    if (g_role != NetRole::NONE) {
        LeaveCriticalSection(&g_netCS);
        return 0;
    }
    if (g_connectThread) {
        if (WaitForSingleObject(g_connectThread, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_connectThread);
            g_connectThread = NULL;
        }
        else {
            LeaveCriticalSection(&g_netCS);
            return 0;
        }
    }
    if (g_netThread) {
        WaitForSingleObject(g_netThread, INFINITE);
        CloseHandle(g_netThread);
        g_netThread = NULL;
    }
    size_t hostLen;
    const char* host = g_api->p_luaL_checklstring(L, 1, &hostLen);
    if (hostLen > 255) {
        LeaveCriticalSection(&g_netCS);
        return 0;
    }
    int port = (int)g_api->p_luaL_checkinteger(L, 2);
    ConnectArgs* args = (ConnectArgs*)HeapAlloc(GetProcessHeap(), 0, sizeof(ConnectArgs));
    if (!args) {
        LeaveCriticalSection(&g_netCS);
        return 0;
    }
    memcpy(args->host, host, hostLen);
    args->host[hostLen] = '\0';
    args->port = port;
    InterlockedExchange(&g_runThread, 1);
    LeaveCriticalSection(&g_netCS);
    g_connectThread = CreateThread(NULL, 0, ConnectThread, args, 0, NULL);
    g_api->p_lua_pushboolean(L, 1);
    return 1;
}
int l_mp_send(lua_State* L) {
    size_t dataLen;
    const char* data = g_api->p_luaL_checklstring(L, 1, &dataLen);
    int targetId = -1;
    if (g_api->p_lua_type(L, 2) == LUA_TNUMBER) targetId = (int)g_api->p_lua_tonumberx(L, 2, NULL);
    uint32_t channel = 0;
    if (g_api->p_lua_type(L, 3) == LUA_TNUMBER) channel = (uint32_t)g_api->p_lua_tonumberx(L, 3, NULL);
    EnterCriticalSection(&g_netCS);
    if (g_role == NetRole::CLIENT && g_clients && g_clients[0].active) {
        SendPacket(g_clients[0], PKT_DATA, channel, data, (int)dataLen);
    }
    else if (g_role == NetRole::SERVER && g_clients) {
        for (int i = 0; i < g_maxClients; ++i) {
            if (g_clients[i].active && (targetId == -1 || g_clients[i].id == targetId)) {
                SendPacket(g_clients[i], PKT_DATA, channel, data, (int)dataLen);
            }
        }
    }
    LeaveCriticalSection(&g_netCS);
    return 0;
}
int l_mp_poll(lua_State* L) {
    NetMessage* msg = DequeueMessage();
    if (!msg) return 0;
    switch (msg->eventType) {
    case NetEvent::CONNECTED:    g_api->p_lua_pushstring(L, "connected"); break;
    case NetEvent::DISCONNECTED: g_api->p_lua_pushstring(L, "disconnected"); break;
    case NetEvent::CONNECT_ERR:  g_api->p_lua_pushstring(L, "error"); break;
    case NetEvent::DATA:         g_api->p_lua_pushstring(L, "data"); break;
    }
    g_api->p_lua_pushnumber(L, (lua_Number)msg->clientId);
    if (msg->eventType == NetEvent::DATA) {
        if (msg->data) {
            g_api->p_lua_pushstring(L, msg->data);
            HeapFree(GetProcessHeap(), 0, msg->data);
        }
        else g_api->p_lua_pushstring(L, "");
        g_api->p_lua_pushnumber(L, (lua_Number)msg->channel);
        delete msg;
        return 4;
    }
    delete msg;
    return 2;
}
int l_mp_metrics(lua_State* L) {
    int targetId = (int)g_api->p_luaL_checkinteger(L, 1);
    uint32_t p = 0;
    uint64_t s = 0, r = 0;
    EnterCriticalSection(&g_netCS);
    for (int i = 0; i < g_maxClients; ++i) {
        if (g_clients && g_clients[i].active && g_clients[i].id == targetId) {
            p = g_clients[i].pingMs;
            s = InterlockedCompareExchange64(&g_clients[i].bytesSent, 0, 0);
            r = InterlockedCompareExchange64(&g_clients[i].bytesRecv, 0, 0);
            break;
        }
    }
    LeaveCriticalSection(&g_netCS);
    g_api->p_lua_pushnumber(L, (lua_Number)p);
    g_api->p_lua_pushnumber(L, (lua_Number)s);
    g_api->p_lua_pushnumber(L, (lua_Number)r);
    return 3;
}
int l_mp_disconnect(lua_State* L) {
    DisconnectAll();
    return 0;
}
extern "C" LUME_PLUGIN_EXPORT int lume_plugin_init(lua_State* L, LumeHostAPI* api) {
    g_api = api;
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    InitializeCriticalSection(&g_queueCS);
    InitializeCriticalSection(&g_netCS);
    g_api->p_lua_pushcclosure(L, l_mp_host, 0);
    g_api->p_lua_setglobal(L, "mp_host");
    g_api->p_lua_pushcclosure(L, l_mp_connect, 0);
    g_api->p_lua_setglobal(L, "mp_connect");
    g_api->p_lua_pushcclosure(L, l_mp_send, 0);
    g_api->p_lua_setglobal(L, "mp_send");
    g_api->p_lua_pushcclosure(L, l_mp_poll, 0);
    g_api->p_lua_setglobal(L, "mp_poll");
    g_api->p_lua_pushcclosure(L, l_mp_metrics, 0);
    g_api->p_lua_setglobal(L, "mp_metrics");
    g_api->p_lua_pushcclosure(L, l_mp_disconnect, 0);
    g_api->p_lua_setglobal(L, "mp_disconnect");
    return 0;
}
extern "C" LUME_PLUGIN_EXPORT void lume_plugin_shutdown() {
    DisconnectAll();
    DeleteCriticalSection(&g_queueCS);
    DeleteCriticalSection(&g_netCS);
    WSACleanup();
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {return TRUE;}