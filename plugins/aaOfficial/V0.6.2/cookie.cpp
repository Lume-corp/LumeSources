// cl.exe /O2 /LD /GS- /GR- /EHsc- cookie.cpp /link /NODEFAULTLIB /ENTRY:DllMain advapi32.lib crypt32.lib user32.lib kernel32.lib
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#include <windows.h>
#include <wincrypt.h>
#include <stdint.h>
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
extern "C" {
    int _fltused = 0;
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
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {return TRUE;}
LumeHostAPI* g_api = nullptr;
void* my_alloc(SIZE_T size) {return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);}
void* my_realloc(void* ptr, SIZE_T size) {
    if (!ptr) return my_alloc(size);
    if (size == 0) {
        HeapFree(GetProcessHeap(), 0, ptr);
        return nullptr;
    }
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, size);
}
void my_free(void* ptr) {if (ptr) HeapFree(GetProcessHeap(), 0, ptr);}
int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
size_t my_strlen(const char* str) {
    size_t len = 0;
    while (str && str[len]) len++;
    return len;
}
void ExtractDomain(const char* url, char* outDomain) {
    const char* p = url;
    const char* schemeEnd = p;
    for (int i = 0; p[i] && p[i + 1] && p[i + 2]; i++) {
        if (p[i] == ':' && p[i + 1] == '/' && p[i + 2] == '/') {
            schemeEnd = p + i + 3;
            break;
        }
    }
    p = schemeEnd;
    int i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && p[i] != '?' && i < 255) {
        outDomain[i] = p[i];
        i++;
    }
    outDomain[i] = '\0';
    if (my_strlen(outDomain) == 0) {
        outDomain[0] = 'l';
        outDomain[1] = 'o';
        outDomain[2] = 'c';
        outDomain[3] = 'a';
        outDomain[4] = 'l';
        outDomain[5] = '\0';
    }
}
uint64_t HashDomain(const char* domain) {
    uint64_t hash = 14695981039346656037ULL;
    while (*domain) {
        hash ^= (unsigned char)(*domain++);
        hash *= 1099511628211ULL;
    }
    return hash;
}
bool EncryptValue(const char* inData, DWORD inLen, BYTE** outData, DWORD* outLen) {
    if (inLen == 0) {
        *outData = nullptr;
        *outLen = 0;
        return true;
    }
    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = (BYTE*)inData; dataIn.cbData = inLen;
    if (CryptProtectData(&dataIn, L"LumeVault", NULL, NULL, NULL, 0, &dataOut)) {
        *outData = (BYTE*)my_alloc(dataOut.cbData);
        memcpy(*outData, dataOut.pbData, dataOut.cbData);
        *outLen = dataOut.cbData;
        LocalFree(dataOut.pbData);
        return true;
    }
    return false;
}
bool DecryptValue(const BYTE* inData, DWORD inLen, char** outData, DWORD* outLen) {
    if (inLen == 0) { *outData = nullptr; *outLen = 0; return true; }
    DATA_BLOB dataIn, dataOut;
    dataIn.pbData = (BYTE*)inData; dataIn.cbData = inLen;
    if (CryptUnprotectData(&dataIn, NULL, NULL, NULL, NULL, 0, &dataOut)) {
        *outData = (char*)my_alloc(dataOut.cbData + 1);
        memcpy(*outData, dataOut.pbData, dataOut.cbData);
        (*outData)[dataOut.cbData] = '\0';
        *outLen = dataOut.cbData;
        LocalFree(dataOut.pbData);
        return true;
    }
    return false;
}
#pragma pack(push, 1)
struct VaultRecordHeader {
    char magic[2];
    uint8_t type;
    uint64_t domainHash;
    uint16_t keyLen;
    uint32_t valLen;
};
#pragma pack(pop)
static const char* VAULT_FILE = "lume_vault.bin";
static const char* VAULT_TEMP = "lume_vault.tmp";
void GetCurrentSafeDomain(char* outDomain) {
    HWND hMain = g_api->get_main_hwnd();
    HWND hAddr = GetDlgItem(hMain, 1001);
    char urlBuf[2048] = { 0 };
    GetWindowTextA(hAddr, urlBuf, 2048);
    ExtractDomain(urlBuf, outDomain);
}
bool InternalVaultSet(int type, const char* key, const char* value) {
    if (!key) return false;
    size_t kLenRaw = my_strlen(key);
    if (kLenRaw > 0xFFFF) return false;
    char safeDomain[256] = {0};
    GetCurrentSafeDomain(safeDomain);
    DWORD valLen = value ? (DWORD)my_strlen(value) : 0;
    BYTE* encVal = nullptr;
    DWORD encLen = 0;
    if (valLen > 0 && !EncryptValue(value, valLen, &encVal, &encLen)) {
        return false;
    }
    VaultRecordHeader hdr;
    hdr.magic[0] = 'L'; hdr.magic[1] = 'V';
    hdr.type = (uint8_t)type;
    hdr.domainHash = HashDomain(safeDomain);
    hdr.keyLen = (uint16_t)kLenRaw;
    hdr.valLen = encLen;
    HANDLE hFile = CreateFileA(VAULT_FILE, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, &hdr, sizeof(hdr), &written, NULL);
        WriteFile(hFile, key, hdr.keyLen, &written, NULL);
        if (encLen > 0 && encVal) {
            WriteFile(hFile, encVal, hdr.valLen, &written, NULL);
        }
        CloseHandle(hFile);
        my_free(encVal);
        return true;
    }
    my_free(encVal);
    return false;
}
static int l_vault_set(lua_State* L) {
    if (!g_api) return 0;
    int type = (int)g_api->p_luaL_checkinteger(L, 1);
    const char* key = g_api->p_luaL_checklstring(L, 2, NULL);
    const char* value = (g_api->p_lua_type(L, 3) > 0) ? g_api->p_luaL_checklstring(L, 3, NULL) : "";
    bool res = InternalVaultSet(type, key, value);
    g_api->p_lua_pushboolean(L, res ? 1 : 0);
    return 1;
}
static int l_vault_get(lua_State* L) {
    if (!g_api) return 0;
    int type = (int)g_api->p_luaL_checkinteger(L, 1);
    const char* key = g_api->p_luaL_checklstring(L, 2, NULL);
    if (!key) return 0;
    char safeDomain[256] = {0};
    GetCurrentSafeDomain(safeDomain);
    uint64_t targetHash = HashDomain(safeDomain);
    size_t targetKeyLen = my_strlen(key);
    HANDLE hFile = CreateFileA(VAULT_FILE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;
    char* latestValue = nullptr;
    VaultRecordHeader hdr;
    DWORD read;
    while (ReadFile(hFile, &hdr, sizeof(hdr), &read, NULL) && read == sizeof(hdr)) {
        if (hdr.magic[0] != 'L' || hdr.magic[1] != 'V') break;
        if (hdr.type != type || hdr.domainHash != targetHash || hdr.keyLen != targetKeyLen) {
            LARGE_INTEGER liSkip;
            liSkip.QuadPart = (LONGLONG)hdr.keyLen + hdr.valLen;
            SetFilePointerEx(hFile, liSkip, NULL, FILE_CURRENT);
            continue;
        }
        char* fileKey = (char*)my_alloc(hdr.keyLen + 1);
        if (ReadFile(hFile, fileKey, hdr.keyLen, &read, NULL) && read == hdr.keyLen) {
            fileKey[hdr.keyLen] = '\0';
            if (my_strcmp(fileKey, key) == 0) {
                if (hdr.valLen == 0) {
                    if (latestValue) { my_free(latestValue); latestValue = nullptr; }
                }
                else {
                    BYTE* encVal = (BYTE*)my_alloc(hdr.valLen);
                    if (ReadFile(hFile, encVal, hdr.valLen, &read, NULL) && read == hdr.valLen) {
                        char* decVal = nullptr; DWORD decLen = 0;
                        if (DecryptValue(encVal, hdr.valLen, &decVal, &decLen)) {
                            if (latestValue) my_free(latestValue);
                            latestValue = decVal;
                        }
                    }
                    my_free(encVal);
                }
            }
            else {
                LARGE_INTEGER liSkip; liSkip.QuadPart = hdr.valLen;
                SetFilePointerEx(hFile, liSkip, NULL, FILE_CURRENT);
            }
        }
        my_free(fileKey);
    }
    CloseHandle(hFile);
    if (latestValue) {
        g_api->p_lua_pushstring(L, latestValue);
        my_free(latestValue);
        return 1;
    }
    return 0;
}
static int l_vault_delete(lua_State* L) {
    if (!g_api) return 0;
    int type = (int)g_api->p_luaL_checkinteger(L, 1);
    const char* key = g_api->p_luaL_checklstring(L, 2, NULL);
    bool res = InternalVaultSet(type, key, "");
    g_api->p_lua_pushboolean(L, res ? 1 : 0);
    return 1;
}
static int l_vault_clear(lua_State* L) {
    if (!g_api) return 0;
    int type = (int)g_api->p_luaL_checkinteger(L, 1);
    char safeDomain[256] = {0};
    GetCurrentSafeDomain(safeDomain);
    uint64_t targetHash = HashDomain(safeDomain);
    HANDLE hFile = CreateFileA(VAULT_FILE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        g_api->p_lua_pushboolean(L, 1);
        return 1;
    }
    struct KeyNode {char* key; KeyNode* next;};
    KeyNode* head = nullptr;
    VaultRecordHeader hdr;
    DWORD read;
    while (ReadFile(hFile, &hdr, sizeof(hdr), &read, NULL) && read == sizeof(hdr)) {
        if (hdr.magic[0] != 'L' || hdr.magic[1] != 'V') break;
        if (hdr.type != type || hdr.domainHash != targetHash) {
            LARGE_INTEGER liSkip; liSkip.QuadPart = (LONGLONG)hdr.keyLen + hdr.valLen;
            SetFilePointerEx(hFile, liSkip, NULL, FILE_CURRENT);
            continue;
        }
        char* fileKey = (char*)my_alloc(hdr.keyLen + 1);
        if (ReadFile(hFile, fileKey, hdr.keyLen, &read, NULL) && read == hdr.keyLen) {
            fileKey[hdr.keyLen] = '\0';
            bool exists = false;
            for (KeyNode* k = head; k; k = k->next) {
                if (my_strcmp(k->key, fileKey) == 0) { exists = true; break; }
            }
            if (!exists && hdr.valLen > 0) {
                KeyNode* n = (KeyNode*)my_alloc(sizeof(KeyNode));
                if (n) {
                    n->key = fileKey;
                    n->next = head;
                    head = n;
                    fileKey = nullptr;
                }
            }
            LARGE_INTEGER liSkip; liSkip.QuadPart = hdr.valLen;
            SetFilePointerEx(hFile, liSkip, NULL, FILE_CURRENT);
        }
        if (fileKey) my_free(fileKey);
    }
    CloseHandle(hFile);
    bool ok = true;
    while (head) {
        KeyNode* next = head->next;
        if (!InternalVaultSet(type, head->key, "")) ok = false;
        my_free(head->key);
        my_free(head);
        head = next;
    }
    g_api->p_lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}
struct CRecord {
    uint8_t type;
    uint64_t domainHash;
    char* key;
    BYTE* encVal;
    uint32_t valLen;
};
static int l_vault_compact(lua_State* L) {
    if (!g_api) return 0;
    HANDLE hFile = CreateFileA(VAULT_FILE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        g_api->p_lua_pushboolean(L, 1);
        return 1;
    }
    CRecord* records = nullptr;
    size_t recCount = 0;
    size_t recCap = 0;
    bool oom = false;
    VaultRecordHeader hdr;
    DWORD read;
    while (ReadFile(hFile, &hdr, sizeof(hdr), &read, NULL) && read == sizeof(hdr)) {
        if (hdr.magic[0] != 'L' || hdr.magic[1] != 'V') break;
        char* key = (char*)my_alloc(hdr.keyLen + 1);
        if (!ReadFile(hFile, key, hdr.keyLen, &read, NULL) || read != hdr.keyLen) { my_free(key); break; }
        key[hdr.keyLen] = '\0';
        BYTE* encVal = nullptr;
        if (hdr.valLen > 0) {
            encVal = (BYTE*)my_alloc(hdr.valLen);
            if (!ReadFile(hFile, encVal, hdr.valLen, &read, NULL) || read != hdr.valLen) {
                my_free(key); my_free(encVal); break;
            }
        }
        bool found = false;
        for (size_t i = 0; i < recCount; i++) {
            if (records[i].type == hdr.type && records[i].domainHash == hdr.domainHash && my_strcmp(records[i].key, key) == 0) {
                my_free(records[i].encVal);
                records[i].encVal = encVal;
                records[i].valLen = hdr.valLen;
                found = true;
                my_free(key);
                break;
            }
        }
        if (!found) {
            if (recCount == recCap) {
                size_t newCap = recCap ? recCap * 2 : 16;
                CRecord* newRecs = (CRecord*)my_realloc(records, newCap * sizeof(CRecord));
                if (!newRecs) {
                    oom = true;
                    my_free(key);
                    my_free(encVal);
                    break;
                }
                records = newRecs;
                recCap = newCap;
            }
            records[recCount].type = hdr.type;
            records[recCount].domainHash = hdr.domainHash;
            records[recCount].key = key;
            records[recCount].encVal = encVal;
            records[recCount].valLen = hdr.valLen;
            recCount++;
        }
    }
    CloseHandle(hFile);
    if (oom) {
        for (size_t i = 0; i < recCount; i++) {
            my_free(records[i].key); my_free(records[i].encVal);
        }
        my_free(records);
        g_api->p_lua_pushboolean(L, 0);
        return 1;
    }
    HANDLE hTemp = CreateFileA(VAULT_TEMP, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTemp != INVALID_HANDLE_VALUE) {
        for (size_t i = 0; i < recCount; i++) {
            if (records[i].valLen > 0) {
                VaultRecordHeader newHdr;
                newHdr.magic[0] = 'L'; newHdr.magic[1] = 'V';
                newHdr.type = records[i].type;
                newHdr.domainHash = records[i].domainHash;
                newHdr.keyLen = (uint16_t)my_strlen(records[i].key);
                newHdr.valLen = records[i].valLen;
                DWORD written;
                WriteFile(hTemp, &newHdr, sizeof(newHdr), &written, NULL);
                WriteFile(hTemp, records[i].key, newHdr.keyLen, &written, NULL);
                WriteFile(hTemp, records[i].encVal, newHdr.valLen, &written, NULL);
            }
            my_free(records[i].key);
            my_free(records[i].encVal);
        }
        CloseHandle(hTemp);
        my_free(records);
        MoveFileExA(VAULT_TEMP, VAULT_FILE, MOVEFILE_REPLACE_EXISTING);
        g_api->p_lua_pushboolean(L, 1);
    }
    else {
        for (size_t i = 0; i < recCount; i++) {
            my_free(records[i].key); my_free(records[i].encVal);
        }
        my_free(records);
        g_api->p_lua_pushboolean(L, 0);
    }
    return 1;
}
extern "C" {
    __declspec(dllexport) void __cdecl lume_plugin_init(lua_State* L, LumeHostAPI* api) {
        g_api = api;
        g_api->p_lua_pushcclosure(L, l_vault_set, 0);
        g_api->p_lua_setglobal(L, "vault_set");
        g_api->p_lua_pushcclosure(L, l_vault_get, 0);
        g_api->p_lua_setglobal(L, "vault_get");
        g_api->p_lua_pushcclosure(L, l_vault_delete, 0);
        g_api->p_lua_setglobal(L, "vault_delete");
        g_api->p_lua_pushcclosure(L, l_vault_clear, 0);
        g_api->p_lua_setglobal(L, "vault_clear");
        g_api->p_lua_pushcclosure(L, l_vault_compact, 0);
        g_api->p_lua_setglobal(L, "vault_compact");
    }
    __declspec(dllexport) void __cdecl lume_plugin_shutdown() {
        g_api = nullptr;
    }
}