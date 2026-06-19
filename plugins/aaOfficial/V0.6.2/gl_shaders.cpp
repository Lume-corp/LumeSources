// cl.exe /O2 /LD /GS- /GR- /EHsc- gl_shaders.cpp /link /NODEFAULTLIB /ENTRY:DllMain opengl32.lib kernel32.lib user32.lib
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#include <windows.h>
#include <GL/gl.h>
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
extern "C" const int _fltused = 0;
LumeHostAPI* g_api = nullptr;
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
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
typedef GLuint(APIENTRY* PFNGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRY* PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
typedef void (APIENTRY* PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef void (APIENTRY* PFNGLGETSHADERIVPROC) (GLuint shader, GLenum pname, GLint* param);
typedef void (APIENTRY* PFNGLGETSHADERINFOLOGPROC) (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLuint(APIENTRY* PFNGLCREATEPROGRAMPROC) (void);
typedef void (APIENTRY* PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRY* PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRY* PFNGLGETPROGRAMIVPROC) (GLuint program, GLenum pname, GLint* param);
typedef void (APIENTRY* PFNGLGETPROGRAMINFOLOGPROC) (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef void (APIENTRY* PFNGLUSEPROGRAMPROC) (GLuint program);
typedef void (APIENTRY* PFNGLDELETESHADERPROC) (GLuint shader);
typedef void (APIENTRY* PFNGLDELETEPROGRAMPROC) (GLuint program);
typedef GLint(APIENTRY* PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar* name);
typedef void (APIENTRY* PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (APIENTRY* PFNGLUNIFORM2FPROC) (GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRY* PFNGLUNIFORM3FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRY* PFNGLUNIFORM4FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRY* PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (APIENTRY* PFNGLUNIFORMMATRIX4FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PFNGLCREATESHADERPROC glCreateShader = nullptr;
PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
PFNGLATTACHSHADERPROC glAttachShader = nullptr;
PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
PFNGLDELETESHADERPROC glDeleteShader = nullptr;
PFNGLDELETEPROGRAMPROC glDeleteProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
PFNGLUNIFORM1FPROC glUniform1f = nullptr;
PFNGLUNIFORM2FPROC glUniform2f = nullptr;
PFNGLUNIFORM3FPROC glUniform3f = nullptr;
PFNGLUNIFORM4FPROC glUniform4f = nullptr;
PFNGLUNIFORM1IPROC glUniform1i = nullptr;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv = nullptr;
bool g_extLoaded = false;
bool g_extFailed = false;
void LoadGLExtensions() {
    if (g_extLoaded || g_extFailed) return;
    glCreateShader = (PFNGLCREATESHADERPROC)wglGetProcAddress("glCreateShader");
    if (!glCreateShader) {
        g_extFailed = true;
        return;
    }
    glShaderSource = (PFNGLSHADERSOURCEPROC)wglGetProcAddress("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC)wglGetProcAddress("glCompileShader");
    glGetShaderiv = (PFNGLGETSHADERIVPROC)wglGetProcAddress("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)wglGetProcAddress("glGetShaderInfoLog");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC)wglGetProcAddress("glCreateProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC)wglGetProcAddress("glAttachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC)wglGetProcAddress("glLinkProgram");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog");
    glUseProgram = (PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram");
    glDeleteShader = (PFNGLDELETESHADERPROC)wglGetProcAddress("glDeleteShader");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC)wglGetProcAddress("glDeleteProgram");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)wglGetProcAddress("glGetUniformLocation");
    glUniform1f = (PFNGLUNIFORM1FPROC)wglGetProcAddress("glUniform1f");
    glUniform2f = (PFNGLUNIFORM2FPROC)wglGetProcAddress("glUniform2f");
    glUniform3f = (PFNGLUNIFORM3FPROC)wglGetProcAddress("glUniform3f");
    glUniform4f = (PFNGLUNIFORM4FPROC)wglGetProcAddress("glUniform4f");
    glUniform1i = (PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)wglGetProcAddress("glUniformMatrix4fv");
    if (!glShaderSource || !glCompileShader || !glCreateProgram || !glUseProgram || !glUniformMatrix4fv) {
        g_extFailed = true;
        return;
    }
    g_extLoaded = true;
}
double my_optnumber(lua_State* L, int idx, double def) {
    if (!g_api || g_api->p_lua_type(L, idx) <= 0) return def;
    return g_api->p_lua_tonumberx(L, idx, 0);
}
int my_optinteger(lua_State* L, int idx, int def) {
    if (!g_api || g_api->p_lua_type(L, idx) <= 0) return def;
    return (int)g_api->p_lua_tonumberx(L, idx, 0);
}
const char* my_optstring(lua_State* L, int idx, const char* def) {
    if (!g_api || g_api->p_lua_type(L, idx) <= 0) return def;
    return g_api->p_luaL_checklstring(L, idx, nullptr);
}
void CustomAlertWithPrefix(const char* prefix, const char* msg) {
    if (!g_api || !msg) return;
    int len = lstrlenA(prefix) + lstrlenA(msg) + 10;
    char* fullMsg = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
    if (fullMsg) {
        wsprintfA(fullMsg, "%s: %s", prefix ? prefix : "Error", msg);
        g_api->alert(fullMsg);
        HeapFree(GetProcessHeap(), 0, fullMsg);
    }
}
bool CheckShaderLog(GLuint shader, const char* prefix) {
    GLint logLen = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1) {
        char* log = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, logLen + 1);
        if (log) {
            glGetShaderInfoLog(shader, logLen, NULL, log);
            CustomAlertWithPrefix(prefix, log);
            HeapFree(GetProcessHeap(), 0, log);
        }
        return false;
    }
    return true;
}
static int l_gl_create_shader(lua_State* L) {
    if (!g_api) return 0;
    LoadGLExtensions();
    if (!glCreateShader) {
        g_api->alert("GL Shader error: Extensions not loaded!");
        g_api->p_lua_pushnumber(L, 0);
        return 1;
    }
    const char* v_src = my_optstring(L, 1, nullptr);
    const char* f_src = my_optstring(L, 2, nullptr);
    if (!v_src || !f_src || v_src[0] == '\0' || f_src[0] == '\0') {
        g_api->alert("GL Shader error: Shader source cannot be empty.");
        g_api->p_lua_pushnumber(L, 0);
        return 1;
    }
    GLint status;
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &v_src, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    if (!status) {
        CheckShaderLog(vs, "Vertex Shader");
        glDeleteShader(vs);
        g_api->p_lua_pushnumber(L, 0);
        return 1;
    }
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &f_src, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    if (!status) {
        CheckShaderLog(fs, "Fragment Shader");
        glDeleteShader(vs);
        glDeleteShader(fs);
        g_api->p_lua_pushnumber(L, 0);
        return 1;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        GLint logLen = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 1) {
            char* log = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, logLen + 1);
            if (log) {
                glGetProgramInfoLog(prog, logLen, NULL, log);
                CustomAlertWithPrefix("Program Link", log);
                HeapFree(GetProcessHeap(), 0, log);
            }
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
        glDeleteProgram(prog);
        g_api->p_lua_pushnumber(L, 0);
        return 1;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    g_api->p_lua_pushnumber(L, prog);
    return 1;
}
static int l_gl_use_program(lua_State* L) {
    if (!g_api) return 0;
    LoadGLExtensions();
    if (glUseProgram) {
        glUseProgram(my_optinteger(L, 1, 0));
    }
    return 0;
}
static int l_gl_delete_program(lua_State* L) {
    if (!g_api) return 0;
    LoadGLExtensions();
    if (glDeleteProgram) {
        glDeleteProgram(my_optinteger(L, 1, 0));
    }
    return 0;
}
static int l_gl_get_uniform_location(lua_State* L) {
    if (!g_api) return 0;
    LoadGLExtensions();
    if (!glGetUniformLocation) { 
        g_api->p_lua_pushnumber(L, -1); 
        return 1; 
    }
    g_api->p_lua_pushnumber(L, glGetUniformLocation(my_optinteger(L, 1, 0), my_optstring(L, 2, "")));
    return 1;
}
static int l_gl_uniform1f(lua_State* L) {
    if (!g_api) return 0; LoadGLExtensions();
    if (glUniform1f) { glUniform1f(my_optinteger(L, 1, -1), (float)my_optnumber(L, 2, 0.0)); }
    return 0;
}
static int l_gl_uniform2f(lua_State* L) {
    if (!g_api) return 0; LoadGLExtensions();
    if (glUniform2f) { glUniform2f(my_optinteger(L, 1, -1), (float)my_optnumber(L, 2, 0.0), (float)my_optnumber(L, 3, 0.0)); }
    return 0;
}
static int l_gl_uniform3f(lua_State* L) {
    if (!g_api) return 0; LoadGLExtensions();
    if (glUniform3f) { glUniform3f(my_optinteger(L, 1, -1), (float)my_optnumber(L, 2, 0.0), (float)my_optnumber(L, 3, 0.0), (float)my_optnumber(L, 4, 0.0)); }
    return 0;
}
static int l_gl_uniform4f(lua_State* L) {
    if (!g_api) return 0; LoadGLExtensions();
    if (glUniform4f) { glUniform4f(my_optinteger(L, 1, -1), (float)my_optnumber(L, 2, 0.0), (float)my_optnumber(L, 3, 0.0), (float)my_optnumber(L, 4, 0.0), (float)my_optnumber(L, 5, 0.0)); }
    return 0;
}
static int l_gl_uniform1i(lua_State* L) {
    if (!g_api) return 0; LoadGLExtensions();
    if (glUniform1i) { glUniform1i(my_optinteger(L, 1, -1), my_optinteger(L, 2, 0)); }
    return 0;
}
static int l_gl_uniform_matrix4fv(lua_State* L) {
    if (!g_api) return 0;
    LoadGLExtensions();
    if (!glUniformMatrix4fv) return 0;
    int loc = my_optinteger(L, 1, -1);
    if (g_api->p_lua_type(L, 2) != LUA_TTABLE) {
        return 0;
    }
    float mat[16] = {0}; 
    for (int i = 0; i < 16; ++i) {
        g_api->p_lua_rawgeti(L, 2, i + 1);
        mat[i] = (float)g_api->p_lua_tonumberx(L, -1, 0);
        g_api->p_lua_settop(L, -2);
    }
    glUniformMatrix4fv(loc, 1, GL_FALSE, mat);
    return 0;
}
extern "C" {
    __declspec(dllexport) void __cdecl lume_plugin_init(lua_State* L, LumeHostAPI* api) {
        if (!api) return;
        g_api = api;
        g_api->p_lua_pushcclosure(L, l_gl_create_shader, 0); g_api->p_lua_setglobal(L, "gl_create_shader");
        g_api->p_lua_pushcclosure(L, l_gl_use_program, 0);   g_api->p_lua_setglobal(L, "gl_use_program");
        g_api->p_lua_pushcclosure(L, l_gl_delete_program, 0);g_api->p_lua_setglobal(L, "gl_delete_program");
        g_api->p_lua_pushcclosure(L, l_gl_get_uniform_location, 0); g_api->p_lua_setglobal(L, "gl_get_uniform_location");
        g_api->p_lua_pushcclosure(L, l_gl_uniform1f, 0); g_api->p_lua_setglobal(L, "gl_uniform1f");
        g_api->p_lua_pushcclosure(L, l_gl_uniform2f, 0); g_api->p_lua_setglobal(L, "gl_uniform2f");
        g_api->p_lua_pushcclosure(L, l_gl_uniform3f, 0); g_api->p_lua_setglobal(L, "gl_uniform3f");
        g_api->p_lua_pushcclosure(L, l_gl_uniform4f, 0); g_api->p_lua_setglobal(L, "gl_uniform4f");
        g_api->p_lua_pushcclosure(L, l_gl_uniform1i, 0); g_api->p_lua_setglobal(L, "gl_uniform1i");
        g_api->p_lua_pushcclosure(L, l_gl_uniform_matrix4fv, 0); g_api->p_lua_setglobal(L, "gl_uniform_matrix4fv");
    }
    __declspec(dllexport) void __cdecl lume_plugin_shutdown() {
        g_api = nullptr;
        g_extLoaded = false;
        g_extFailed = false;
    }
}