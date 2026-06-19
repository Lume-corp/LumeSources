// cl.exe /O2 /LD /GS- /GR- /EHsc- sound.cpp /link /NODEFAULTLIB /ENTRY:DllMain winmm.lib kernel32.lib user32.lib
#define BUILDING_PLUGIN
#include "../lume_plugin.h"
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#define M_PI 3.14159265358979323846
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
}
void* my_alloc(SIZE_T size) {return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);}
void* my_realloc(void* ptr, SIZE_T size) {return ptr ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ptr, size) : my_alloc(size);}
void my_free(void* ptr) {if (ptr) HeapFree(GetProcessHeap(), 0, ptr);}
int my_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {s1++; s2++;}
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
int my_atoi(const char* str) {
    int res = 0, sign = 1;
    if (*str == '-') {
        sign = -1;
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        res = res * 10 + (*str - '0');
        str++;
    }
    return res * sign;
}
double my_optnumber(lua_State* L, int idx, double def) {
    return (g_api->p_lua_type(L, idx) > 0) ? g_api->p_lua_tonumberx(L, idx, 0) : def;
}
int my_optinteger(lua_State* L, int idx, int def) {
    return (g_api->p_lua_type(L, idx) > 0) ? (int)g_api->p_lua_tonumberx(L, idx, 0) : def;
}
const char* my_optstring(lua_State* L, int idx, const char* def) {
    return (g_api->p_lua_type(L, idx) > 0) ? g_api->p_luaL_checklstring(L, idx, nullptr) : def;
}
static unsigned int g_seed = 12345;
int my_rand() {
    g_seed = g_seed * 1103515245 + 12345;
    return (int)((g_seed >> 16) & 0x7FFF);
}
double fast_sin_norm(double phase) {
    double x = phase - M_PI;
    double abs_x = x < 0 ? -x : x;
    double sin_x = 1.27323954 * x - 0.405284735 * x * abs_x;
    double abs_sin_x = sin_x < 0 ? -sin_x : sin_x;
    return 0.225 * (sin_x * abs_sin_x - sin_x) + sin_x;
}
enum WaveType {WAVE_SQUARE, WAVE_SINE, WAVE_SAW, WAVE_TRIANGLE, WAVE_NOISE, WAVE_PULSE};
WaveType parseWaveType(const char* s) {
    if (!s) return WAVE_SQUARE;
    if (my_strcmp(s, "sine") == 0) return WAVE_SINE;
    if (my_strcmp(s, "saw") == 0) return WAVE_SAW;
    if (my_strcmp(s, "triangle") == 0) return WAVE_TRIANGLE;
    if (my_strcmp(s, "noise") == 0) return WAVE_NOISE;
    if (my_strcmp(s, "pulse") == 0) return WAVE_PULSE;
    return WAVE_SQUARE;
}
struct ToneSegment {
    double baseFreq;
    double endFreq;
    int durationMs;
    int attackMs;
    int releaseMs;
    int volume;
    WaveType type;
    double dutyCycle;
};
int g_numBuffers = 4;
int g_bufferSamples = 2048;
bool g_pendingResize = false;
int g_newBufferSamples = 2048;
#define CHANNELS 2
#define SAMPLE_RATE 44100
#define MAX_VOICES 16
struct Voice {
    short* samples;
    int numSamples;
    double currentPos;
    float dynamicVol;
    double dynamicPitch;
    unsigned int sourceHash;
    bool active;
};
Voice g_voices[MAX_VOICES];
CRITICAL_SECTION g_audioCS;
HWAVEOUT g_hWaveOut = nullptr;
WAVEHDR* g_waveHeaders = nullptr;
short** g_mixBuffers = nullptr;
HANDLE g_hAudioEvent = nullptr;
HANDLE g_hAudioThread = nullptr;
bool g_audioRunning = false;
int g_nextBuffer = 0;
static DWORD g_lastSfxTime = 0;
static char g_lastSfx[256] = {0};
static DWORD g_lastMelodyTime = 0;
static unsigned int g_lastMelodyHash = 0;
short* generateSequence(ToneSegment* segments, int count, int& outLength) {
    long long totalMs = 0;
    for (int i = 0; i < count; i++) totalMs += segments[i].durationMs;
    int totalSamples = (int)(((long long)SAMPLE_RATE * totalMs) / 1000);
    if (totalSamples <= 0) {
        outLength = 0;
        return nullptr;
    }
    short* buf = (short*)my_alloc(totalSamples * sizeof(short));
    outLength = totalSamples;
    double phase = 0.0;
    double currentNoise = 0.0;
    int pos = 0;
    for (int s = 0; s < count; s++) {
        ToneSegment& sp = segments[s];
        int segSamples = (int)(((long long)SAMPLE_RATE * sp.durationMs) / 1000);
        int attSamples = (int)(((long long)SAMPLE_RATE * sp.attackMs) / 1000);
        int relSamples = (int)(((long long)SAMPLE_RATE * sp.releaseMs) / 1000);
        int maxAttRel = segSamples / 2;
        if (attSamples > maxAttRel) attSamples = maxAttRel;
        if (relSamples > maxAttRel) relSamples = maxAttRel;
        for (int i = 0; i < segSamples; i++) {
            if (pos >= totalSamples) break;
            double t = (double)i / (segSamples > 0 ? segSamples : 1);
            double currentFreq = sp.baseFreq + (sp.endFreq - sp.baseFreq) * t;
            double phaseInc = (2.0 * M_PI * currentFreq) / SAMPLE_RATE;
            double val = 0.0;
            if (currentFreq > 1.0) {
                if (phase < phaseInc) currentNoise = (my_rand() * (2.0 / 32767.0)) - 1.0;
                switch (sp.type) {
                case WAVE_SQUARE:
                    val = (phase < M_PI) ? 1.0 : -1.0;
                    break;
                case WAVE_PULSE:
                    val = (phase < (2.0 * M_PI * sp.dutyCycle)) ? 1.0 : -1.0;
                    break;
                case WAVE_SINE:
                    val = fast_sin_norm(phase);
                    break;
                case WAVE_SAW:
                    val = (phase < M_PI) ? (phase / M_PI) : (phase / M_PI - 2.0);
                    break;
                case WAVE_TRIANGLE:
                    if (phase < M_PI * 0.5) val = phase / (M_PI * 0.5);
                    else if (phase < M_PI * 1.5) val = 1.0 - (phase - M_PI * 0.5) / (M_PI * 0.5);
                    else val = -1.0 + (phase - M_PI * 1.5) / (M_PI * 0.5);
                    break;
                case WAVE_NOISE:
                    val = currentNoise;
                    break;
                }
            }
            double env = 1.0;
            if (i < attSamples && attSamples > 0) env = (double)i / attSamples;
            else if (i >= segSamples - relSamples && relSamples > 0) env = (double)(segSamples - 1 - i) / relSamples;
            val = val * env * (sp.volume / 100.0);
            double finalVal = val * 32767.0;
            if (finalVal > 32767.0) finalVal = 32767.0;
            if (finalVal < -32768.0) finalVal = -32768.0;
            buf[pos++] = (short)finalVal;
            phase += phaseInc;
            if (phase >= 2.0 * M_PI) phase -= 2.0 * M_PI;
        }
    }
    return buf;
}
int playPcm(short* data, int length, unsigned int hash = 0) {
    if (!data || length <= 0) {
        if (data) my_free(data);
        return -1;
    }
    EnterCriticalSection(&g_audioCS);
    int freeIdx = -1;
    for (int i = 0; i < MAX_VOICES; i++) {
        if (!g_voices[i].active) {
            freeIdx = i;
            break;
        }
    }
    if (freeIdx != -1) {
        if (g_voices[freeIdx].samples) my_free(g_voices[freeIdx].samples);
        g_voices[freeIdx].samples = data;
        g_voices[freeIdx].numSamples = length;
        g_voices[freeIdx].currentPos = 0.0;
        g_voices[freeIdx].dynamicVol = 1.0f;
        g_voices[freeIdx].dynamicPitch = 1.0;
        g_voices[freeIdx].sourceHash = hash;
        g_voices[freeIdx].active = true;
    }
    else {
        my_free(data);
    }
    LeaveCriticalSection(&g_audioCS);
    return freeIdx;
}
DWORD WINAPI AudioThread(LPVOID) {
    static float filterState = 0.0f;
    while (g_audioRunning) {
        WaitForSingleObject(g_hAudioEvent, INFINITE);
        if (!g_audioRunning) break;
        if (g_pendingResize) {
            EnterCriticalSection(&g_audioCS);
            waveOutReset(g_hWaveOut);
            for (int i = 0; i < g_numBuffers; i++) {
                waveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
                if (g_mixBuffers[i]) my_free(g_mixBuffers[i]);
            }
            g_bufferSamples = g_newBufferSamples;
            for (int i = 0; i < g_numBuffers; i++) {
                g_mixBuffers[i] = (short*)my_alloc(g_bufferSamples * CHANNELS * sizeof(short));
                g_waveHeaders[i].lpData = (LPSTR)g_mixBuffers[i];
                g_waveHeaders[i].dwBufferLength = g_bufferSamples * CHANNELS * sizeof(short);
                g_waveHeaders[i].dwFlags = 0;
                waveOutPrepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
                for (int j = 0; j < (g_bufferSamples * CHANNELS); j++) g_mixBuffers[i][j] = 0;
                waveOutWrite(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
            }
            g_nextBuffer = 0;
            g_pendingResize = false;
            LeaveCriticalSection(&g_audioCS);
            continue;
        }
        while (g_waveHeaders[g_nextBuffer].dwFlags & WHDR_DONE) {
            int i = g_nextBuffer;
            short* buffersToFree[MAX_VOICES];
            int freeCount = 0;
            EnterCriticalSection(&g_audioCS);
            for (int j = 0; j < g_bufferSamples; j++) {
                float fsum = 0.0f;
                for (int v = 0; v < MAX_VOICES; v++) {
                    if (g_voices[v].active && g_voices[v].samples) {
                        double pos = g_voices[v].currentPos;
                        int idx1 = (int)pos;
                        int idx2 = idx1 + 1;
                        if (idx2 >= g_voices[v].numSamples) idx2 = idx1;
                        double frac = pos - (double)idx1;
                        short s1 = g_voices[v].samples[idx1];
                        short s2 = g_voices[v].samples[idx2];
                        float interpSample = s1 + (s2 - s1) * (float)frac;
                        fsum += interpSample * g_voices[v].dynamicVol * 0.2f;
                        g_voices[v].currentPos += g_voices[v].dynamicPitch;
                        if (g_voices[v].currentPos >= g_voices[v].numSamples) {
                            g_voices[v].active = false;
                            buffersToFree[freeCount++] = g_voices[v].samples;
                            g_voices[v].samples = nullptr;
                        }
                    }
                }
                float outSample = fsum;
                float absSum = outSample < 0.0f ? -outSample : outSample;
                if (absSum > 24000.0f) {
                    float excess = absSum - 24000.0f;
                    float compressed = 8767.0f * (excess / (excess + 8767.0f));
                    outSample = outSample > 0.0f ? (24000.0f + compressed) : -(24000.0f + compressed);
                }
                filterState += 0.7f * (outSample - filterState);
                float finalOut = filterState;
                int sum;
                if (finalOut < -32768.0f) sum = -32768;
                else if (finalOut > 32767.0f) sum = 32767;
                else sum = (int)finalOut;
                g_mixBuffers[i][j * CHANNELS] = (short)sum;
                g_mixBuffers[i][j * CHANNELS + 1] = (short)sum;
            }
            LeaveCriticalSection(&g_audioCS);
            for (int f = 0; f < freeCount; f++) my_free(buffersToFree[f]);
            g_waveHeaders[i].dwFlags &= ~WHDR_DONE;
            waveOutWrite(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
            g_nextBuffer = (g_nextBuffer + 1) % g_numBuffers;
        }
    }
    return 0;
}
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2) {
    if (uMsg == WOM_DONE && g_hAudioEvent) {
        SetEvent(g_hAudioEvent);
    }
}
static double g_baseFreqs[12] = {261.626, 277.183, 293.665, 311.127, 329.628, 349.228, 369.994, 391.995, 415.305, 440.000, 466.164, 493.883};
double noteToFreq(const char* noteStr, int octave) {
    if (!noteStr || !noteStr[0]) return 0.0;
    char n = noteStr[0], acc = noteStr[1];
    int semitone = 0;
    if (n == 'C') semitone = 0;
    else if (n == 'D') semitone = 2;
    else if (n == 'E') semitone = 4;
    else if (n == 'F') semitone = 5;
    else if (n == 'G') semitone = 7;
    else if (n == 'A') semitone = 9;
    else if (n == 'B') semitone = 11;
    else return 0.0;
    if (acc == '#') semitone++;
    else if (acc == 'b') semitone--;
    if (semitone < 0) {
        semitone += 12;
        octave--;
    }
    else if (semitone > 11) {
        semitone -= 12;
        octave++;
    }
    double freq = g_baseFreqs[semitone];
    if (octave > 4) for (int i = 0; i < octave - 4; i++) freq *= 2.0;
    if (octave < 4) for (int i = 0; i < 4 - octave; i++) freq /= 2.0;
    return freq;
}
static int l_setAudioBuffer(lua_State* L) {
    int samples = my_optinteger(L, 1, 2048);
    if (samples < 256) samples = 256;
    if (samples > 16384) samples = 16384;
    g_newBufferSamples = samples;
    g_pendingResize = true;
    SetEvent(g_hAudioEvent);
    return 0;
}
static int l_beep(lua_State* L) {
    Beep((DWORD)my_optinteger(L, 1, 440), (DWORD)my_optinteger(L, 2, 200));
    return 0;
}
static int l_stopSound(lua_State* L) {
    short* toFree[MAX_VOICES];
    int count = 0;
    EnterCriticalSection(&g_audioCS);
    for (int v = 0; v < MAX_VOICES; v++) {
        if (g_voices[v].active) {
            g_voices[v].active = false;
            toFree[count++] = g_voices[v].samples;
            g_voices[v].samples = nullptr;
        }
    }
    LeaveCriticalSection(&g_audioCS);
    for (int i = 0; i < count; i++) my_free(toFree[i]);
    return 0;
}
static int l_stopVoice(lua_State* L) {
    int id = my_optinteger(L, 1, -1);
    if (id >= 0 && id < MAX_VOICES) {
        short* toFree = nullptr;
        EnterCriticalSection(&g_audioCS);
        if (g_voices[id].active) {
            g_voices[id].active = false;
            toFree = g_voices[id].samples;
            g_voices[id].samples = nullptr;
        }
        LeaveCriticalSection(&g_audioCS);
        if (toFree) my_free(toFree);
    }
    return 0;
}
static int l_setVolume(lua_State* L) {
    int id = my_optinteger(L, 1, -1);
    if (id >= 0 && id < MAX_VOICES) {
        EnterCriticalSection(&g_audioCS);
        g_voices[id].dynamicVol = (float)my_optnumber(L, 2, 1.0);
        LeaveCriticalSection(&g_audioCS);
    }
    return 0;
}
static int l_setPitch(lua_State* L) {
    int id = my_optinteger(L, 1, -1);
    double pitch = my_optnumber(L, 2, 1.0);
    if (pitch < 0.01) pitch = 0.01;
    if (id >= 0 && id < MAX_VOICES) {
        EnterCriticalSection(&g_audioCS);
        g_voices[id].dynamicPitch = pitch;
        LeaveCriticalSection(&g_audioCS);
    }
    return 0;
}
static int l_isSoundPlaying(lua_State* L) {
    int id = my_optinteger(L, 1, -1);
    bool playing = false;
    if (id >= 0 && id < MAX_VOICES) {
        EnterCriticalSection(&g_audioCS);
        playing = g_voices[id].active;
        LeaveCriticalSection(&g_audioCS);
    }
    g_api->p_lua_pushboolean(L, playing ? 1 : 0);
    return 1;
}
static int l_getNoteFreq(lua_State* L) {
    g_api->p_lua_pushnumber(L, noteToFreq(my_optstring(L, 1, "C"), my_optinteger(L, 2, 4)));
    return 1;
}
static int l_playTone(lua_State* L) {
    ToneSegment t;
    t.baseFreq = my_optnumber(L, 1, 440.0);
    t.durationMs = my_optinteger(L, 2, 200);
    t.volume = my_optinteger(L, 3, 80);
    t.type = parseWaveType(my_optstring(L, 4, "square"));
    t.endFreq = my_optnumber(L, 5, t.baseFreq);
    t.attackMs = my_optinteger(L, 6, 2);
    t.releaseMs = my_optinteger(L, 7, 5);
    t.dutyCycle = my_optnumber(L, 8, 0.5);
    int len = 0, id = -1;
    if (t.durationMs > 0) {
        short* buf = generateSequence(&t, 1, len);
        if (buf) id = playPcm(buf, len, 0);
    }
    g_api->p_lua_pushnumber(L, id);
    return 1;
}
static int l_playNote(lua_State* L) {
    ToneSegment t;
    t.baseFreq = noteToFreq(my_optstring(L, 1, "C"), my_optinteger(L, 2, 4));
    t.durationMs = my_optinteger(L, 3, 200);
    t.volume = my_optinteger(L, 4, 80);
    t.type = parseWaveType(my_optstring(L, 5, "square"));
    t.endFreq = t.baseFreq;
    t.attackMs = 2;
    t.releaseMs = 5;
    t.dutyCycle = 0.5;
    int len = 0, id = -1;
    if (t.baseFreq > 0 && t.durationMs > 0) {
        short* buf = generateSequence(&t, 1, len);
        if (buf) id = playPcm(buf, len, 0);
    }
    g_api->p_lua_pushnumber(L, id);
    return 1;
}
static int l_playSfx(lua_State* L) {
    const char* name = my_optstring(L, 1, "");
    if (!name || !name[0]) { g_api->p_lua_pushnumber(L, -1); return 1; }
    DWORD now = GetTickCount();
    if ((now - g_lastSfxTime) < 15 && my_strcmp(name, g_lastSfx) == 0) {
        g_api->p_lua_pushnumber(L, -1); return 1;
    }
    g_lastSfxTime = now;
    int idx = 0;
    while (name[idx] && idx < 255) { g_lastSfx[idx] = name[idx]; idx++; }
    g_lastSfx[idx] = '\0';
    int v = my_optinteger(L, 2, 80);
    ToneSegment ts[16]; int count = 0;
    if (my_strcmp(name, "coin") == 0) {
        ts[0] = { 987.77, 987.77, 80, 5, 0, v, WAVE_SQUARE, 0.5 };
        ts[1] = { 1318.5, 1318.5, 300, 0, 150, v, WAVE_SQUARE, 0.5 };
        count = 2;
    }
    else if (my_strcmp(name, "jump") == 0) {
        ts[0] = {300, 600, 200, 10, 80, v, WAVE_PULSE, 0.3};
        count = 1;
    }
    else if (my_strcmp(name, "hit") == 0) {
        ts[0] = {100, 50, 100, 2, 60, v, WAVE_NOISE, 0.5};
        count = 1;
    }
    else if (my_strcmp(name, "explosion") == 0) {
        ts[0] = {150, 20, 600, 10, 400, v, WAVE_NOISE, 0.5};
        count = 1;
    }
    else if (my_strcmp(name, "powerup") == 0) {
        for (int i = 0; i < 6; i++) ts[i] = {300.0 + i * 80, 300.0 + i * 80, 40, 5, 5, v, WAVE_PULSE, 0.25};
        ts[6] = {1000, 1000, 400, 10, 200, v, WAVE_PULSE, 0.25};
        count = 7;
    }
    else if (my_strcmp(name, "laser") == 0) {
        ts[0] = {1800, 200, 180, 2, 80, v, WAVE_SAW, 0.5};
        count = 1;
    }
    else if (my_strcmp(name, "blip") == 0) {
        ts[0] = {800, 800, 50, 5, 20, v, WAVE_SINE, 0.5};
        count = 1;
    }
    else {
        g_api->p_lua_pushnumber(L, -1);
        return 1;
    }
    int len, id = -1;
    short* buf = generateSequence(ts, count, len);
    if (buf) id = playPcm(buf, len, 0);
    g_api->p_lua_pushnumber(L, id);
    return 1;
}
static int l_playMelody(lua_State* L) {
    const char* melody = my_optstring(L, 1, "");
    if (!melody || !melody[0]) {
        g_api->p_lua_pushnumber(L, -1);
        return 1;
    }
    unsigned int hash = 5381;
    for (int i = 0; melody[i]; i++) {
        hash = ((hash << 5) + hash) + melody[i];
    }
    DWORD now = GetTickCount();
    if ((now - g_lastMelodyTime) < 30 && g_lastMelodyHash == hash) {
        g_api->p_lua_pushnumber(L, -1);
        return 1;
    }
    g_lastMelodyTime = now;
    g_lastMelodyHash = hash;
    bool alreadyPlaying = false;
    int playingVoice = -1;
    EnterCriticalSection(&g_audioCS);
    for (int i = 0; i < MAX_VOICES; i++) {
        if (g_voices[i].active && g_voices[i].sourceHash == hash) {
            alreadyPlaying = true;
            playingVoice = i;
            break;
        }
    }
    LeaveCriticalSection(&g_audioCS);
    if (alreadyPlaying) {
        g_api->p_lua_pushnumber(L, playingVoice);
        return 1;
    }
    int tempo = my_optinteger(L, 2, 120);
    int vol = my_optinteger(L, 3, 80);
    WaveType type = parseWaveType(my_optstring(L, 4, "square"));
    double wholeNote = (4.0 * 60000.0) / tempo;
    int toneCap = 32, toneCount = 0;
    ToneSegment* tones = (ToneSegment*)my_alloc(toneCap * sizeof(ToneSegment));
    const char* ptr = melody;
    while (*ptr) {
        while (*ptr == ' ' || *ptr == '\n' || *ptr == '\t' || *ptr == '\r') ptr++;
        if (!*ptr) break;
        const char* start = ptr;
        while (*ptr && *ptr != ' ' && *ptr != '\n' && *ptr != '\t' && *ptr != '\r') ptr++;
        int len = (int)(ptr - start);
        double freq = 0.0; int divider = 4;
        if (len == 1 && (start[0] == '-' || start[0] == '.' || start[0] == 'R')) {freq = 0.0;}
        else {
            int slash_idx = -1;
            for (int i = 0; i < len; i++) if (start[i] == '/') {
                slash_idx = i;
                break;
            }
            int note_len = (slash_idx == -1) ? len : slash_idx;
            char noteStr[16] = { 0 };
            for (int i = 0; i < note_len && i < 15; i++) noteStr[i] = start[i];
            int i = 1; if (noteStr[1] == '#' || noteStr[1] == 'b') i = 2;
            int octave = 4; if (noteStr[i] != '\0') octave = my_atoi(noteStr + i);
            freq = noteToFreq(noteStr, octave);
            if (slash_idx != -1 && (slash_idx + 1) < len) {
                char divStr[16] = {0};
                int d_start = slash_idx + 1;
                for (int j = d_start; j < len && (j - d_start) < 15; j++) divStr[j - d_start] = start[j];
                divider = my_atoi(divStr);
                if (divider <= 0) divider = 4;
            }
        }
        if (toneCount >= toneCap) {
            toneCap *= 2; tones = (ToneSegment*)my_realloc(tones, toneCap * sizeof(ToneSegment));
        }
        tones[toneCount].baseFreq = freq;
        tones[toneCount].endFreq = freq;
        tones[toneCount].durationMs = (int)(wholeNote / divider);
        tones[toneCount].volume = (freq > 0.0) ? vol : 0;
        tones[toneCount].type = type;
        tones[toneCount].attackMs = 2;
        tones[toneCount].releaseMs = 5;
        tones[toneCount].dutyCycle = 0.5;
        toneCount++;
    }
    int id = -1;
    if (toneCount > 0) {
        int outLen;
        short* buf = generateSequence(tones, toneCount, outLen);
        if (buf) id = playPcm(buf, outLen, hash);
    }
    my_free(tones);
    g_api->p_lua_pushnumber(L, id);
    return 1;
}
extern "C" {
    __declspec(dllexport) void __cdecl lume_plugin_init(lua_State* L, LumeHostAPI* api) {
        g_api = api;
        g_seed = GetTickCount();
        InitializeCriticalSection(&g_audioCS);
        g_hAudioEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        g_audioRunning = true;
        g_nextBuffer = 0;
        WAVEFORMATEX wfx = { 0 };
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = CHANNELS;
        wfx.nSamplesPerSec = SAMPLE_RATE;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, (DWORD_PTR)WaveOutProc, 0, CALLBACK_FUNCTION);
        g_waveHeaders = (WAVEHDR*)my_alloc(g_numBuffers * sizeof(WAVEHDR));
        g_mixBuffers = (short**)my_alloc(g_numBuffers * sizeof(short*));
        for (int i = 0; i < g_numBuffers; i++) {
            g_mixBuffers[i] = (short*)my_alloc(g_bufferSamples * CHANNELS * sizeof(short));
            g_waveHeaders[i].lpData = (LPSTR)g_mixBuffers[i];
            g_waveHeaders[i].dwBufferLength = g_bufferSamples * CHANNELS * sizeof(short);
            g_waveHeaders[i].dwFlags = 0;
            waveOutPrepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
            for (int j = 0; j < (g_bufferSamples * CHANNELS); j++) g_mixBuffers[i][j] = 0;
            waveOutWrite(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
        }
        g_hAudioThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)AudioThread, NULL, 0, NULL);
        SetThreadPriority(g_hAudioThread, THREAD_PRIORITY_TIME_CRITICAL);
        SetEvent(g_hAudioEvent);
        api->p_lua_pushcclosure(L, l_beep, 0); api->p_lua_setglobal(L, "beep");
        api->p_lua_pushcclosure(L, l_playTone, 0); api->p_lua_setglobal(L, "playTone");
        api->p_lua_pushcclosure(L, l_playNote, 0); api->p_lua_setglobal(L, "playNote");
        api->p_lua_pushcclosure(L, l_playMelody, 0); api->p_lua_setglobal(L, "playMelody");
        api->p_lua_pushcclosure(L, l_playSfx, 0); api->p_lua_setglobal(L, "playSfx");
        api->p_lua_pushcclosure(L, l_stopSound, 0); api->p_lua_setglobal(L, "stopSound");
        api->p_lua_pushcclosure(L, l_getNoteFreq, 0); api->p_lua_setglobal(L, "getNoteFreq");
        api->p_lua_pushcclosure(L, l_stopVoice, 0); api->p_lua_setglobal(L, "stopVoice");
        api->p_lua_pushcclosure(L, l_setVolume, 0); api->p_lua_setglobal(L, "setVolume");
        api->p_lua_pushcclosure(L, l_setPitch, 0); api->p_lua_setglobal(L, "setPitch");
        api->p_lua_pushcclosure(L, l_isSoundPlaying, 0); api->p_lua_setglobal(L, "isSoundPlaying");
        api->p_lua_pushcclosure(L, l_setAudioBuffer, 0); api->p_lua_setglobal(L, "setAudioBuffer");
    }
    __declspec(dllexport) void __cdecl lume_plugin_shutdown() {
        g_audioRunning = false;
        SetEvent(g_hAudioEvent);
        if (g_hAudioThread) {
            WaitForSingleObject(g_hAudioThread, INFINITE);
            CloseHandle(g_hAudioThread);
            g_hAudioThread = nullptr;
        }
        waveOutReset(g_hWaveOut);
        if (g_waveHeaders && g_mixBuffers) {
            for (int i = 0; i < g_numBuffers; i++) {
                waveOutUnprepareHeader(g_hWaveOut, &g_waveHeaders[i], sizeof(WAVEHDR));
                if (g_mixBuffers[i]) my_free(g_mixBuffers[i]);
            }
            my_free(g_mixBuffers);
            my_free(g_waveHeaders);
        }
        waveOutClose(g_hWaveOut);
        CloseHandle(g_hAudioEvent);
        DeleteCriticalSection(&g_audioCS);
        for (int i = 0; i < MAX_VOICES; i++) {
            if (g_voices[i].samples) my_free(g_voices[i].samples);
        }
    }
}