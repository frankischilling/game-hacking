// cube7 - ac_client.exe internal cheat
// hooks wglSwapBuffers, renders esp + ammo hack
// f2 = ammo | f3 = esp | end = unload

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <gl/GL.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <MinHook.h>

#pragma comment(lib, "opengl32.lib")

static FILE* g_con = nullptr;

static void Log(const char* fmt, ...) {
    if (!g_con) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_con, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_con, fmt, ap);
    va_end(ap);
    fflush(g_con);
}

static void OpenConsole() {
    AllocConsole();
    freopen_s(&g_con, "CONOUT$", "w", stdout);
    SetConsoleTitleA("cube7");
    Log("ready\n");
}

static void CloseConsole() {
    if (g_con) { fclose(g_con); g_con = nullptr; }
    FreeConsole();
}

// pattern scanner

static size_t GetModuleSize(uintptr_t base) {
    if (!base) return 0;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

// IDA-style sig: "A1 ?? ?? ?? ?? 83 EC 08"
// returns first match address or 0
static uintptr_t FindPattern(uintptr_t base, size_t size, const char* sig) {
    unsigned char pat[128];
    bool          wild[128];
    int           len = 0;

    for (const char* p = sig; *p && len < 128;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (p[0] == '?' && p[1] == '?') {
            pat[len] = 0;
            wild[len] = true;
            len++;
            p += 2;
        } else {
            pat[len] = (unsigned char)strtoul(p, nullptr, 16);
            wild[len] = false;
            len++;
            p += 2;
        }
    }
    if (len == 0) return 0;

    const unsigned char* mem = reinterpret_cast<const unsigned char*>(base);
    for (size_t i = 0; i <= size - len; i++) {
        bool hit = true;
        for (int j = 0; j < len; j++) {
            if (!wild[j] && mem[i + j] != pat[j]) { hit = false; break; }
        }
        if (hit) return base + i;
    }
    return 0;
}

// read 4-byte absolute ptr at match+offset, return as module-relative offset
static uintptr_t ExtractOffset(uintptr_t match, int ptrOff, uintptr_t modBase) {
    uintptr_t absAddr = *reinterpret_cast<uintptr_t*>(match + ptrOff);
    return absAddr - modBase;
}

// check extracted offset is sane: inside module, readable, not wildly far from fallback
static bool ValidateOffset(uintptr_t base, uintptr_t offset, size_t modSize, uintptr_t fallback) {
    if (offset == 0 || offset >= modSize) return false;
    // if the scan result drifted far from where we expect, it's a false positive
    uintptr_t drift = (offset > fallback) ? (offset - fallback) : (fallback - offset);
    if (drift > 0x5000) return false;
    __try {
        volatile uintptr_t probe = *reinterpret_cast<uintptr_t*>(base + offset);
        (void)probe;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

struct SigEntry {
    const char* name;
    const char* pattern;
    int         ptrOff;     // where in the match the 4-byte addr sits
    uintptr_t   fallback;   // hardcoded offset if scan fails
};

// offsets
// scanned (global pointers, may shift between builds)
static uintptr_t offLocalPlayer = 0;
static uintptr_t offEntityList  = 0;
static uintptr_t offEntityCount = 0;
static uintptr_t offViewMatrix  = 0;

// static (struct member offsets, stable within same struct layout)
constexpr DWORD offPosX   = 0x28;
constexpr DWORD offPosY   = 0x2C;
constexpr DWORD offPosZ   = 0x30;
constexpr DWORD offHealth = 0xEC;
constexpr DWORD offName   = 0x205;
constexpr DWORD offTeam   = 0x31C;

constexpr DWORD offAmmoPistol  = 0x12C;
constexpr DWORD offAmmoTMP     = 0x130;
constexpr DWORD offAmmoShotgun = 0x134;
constexpr DWORD offAmmoSMG     = 0x138;
constexpr DWORD offAmmoSniper  = 0x13C;
constexpr DWORD offAmmoRifle   = 0x140;
constexpr DWORD offAmmoGrenade = 0x144;

// update these sigs if targeting a different AC build
// pattern: surrounding instruction bytes, ?? = wildcard over the address operand
// ptrOff: byte index in the match where the 4-byte absolute address starts
static SigEntry g_sigs[] = {
    //                     name           pattern                              ptrOff  fallback (v1.3.0.2)
    { "localPlayer", "8B 0D ?? ?? ?? ?? 56 57 8B 7C 24",                  2,  0x17E0A8 },
    { "entityList",  "A1 ?? ?? ?? ?? 8B 54 24 ?? 8B 4C 24",              1,  0x18AC04 },
    { "entityCount", "8B 0D ?? ?? ?? ?? 89 44 24 ?? 85 C9",              2,  0x18AC0C },
    { "viewMatrix",  "A1 ?? ?? ?? ?? C7 44 24 ?? 00 00 00 00 89 44 24", 1,  0x17DFD0 },
};

static uintptr_t* g_sigTargets[] = {
    &offLocalPlayer, &offEntityList, &offEntityCount, &offViewMatrix
};

static void ResolveOffsets(uintptr_t base) {
    size_t modSize = GetModuleSize(base);
    Log("module size: 0x%X\n", (unsigned)modSize);

    for (int i = 0; i < _countof(g_sigs); i++) {
        auto& s = g_sigs[i];
        uintptr_t match = 0;
        if (modSize > 0)
            match = FindPattern(base, modSize, s.pattern);

        bool usedScan = false;
        if (match) {
            uintptr_t candidate = ExtractOffset(match, s.ptrOff, base);
            if (ValidateOffset(base, candidate, modSize, s.fallback)) {
                *g_sigTargets[i] = candidate;
                usedScan = true;
                Log("%-14s scan hit @ +0x%X -> offset 0x%X\n",
                    s.name, (unsigned)(match - base), (unsigned)candidate);
            } else {
                Log("%-14s scan hit @ +0x%X -> 0x%X INVALID, rejected\n",
                    s.name, (unsigned)(match - base), (unsigned)candidate);
            }
        }
        if (!usedScan) {
            *g_sigTargets[i] = s.fallback;
            Log("%-14s using fallback 0x%X\n", s.name, (unsigned)s.fallback);
        }
    }
}

// globals 

using fn_wglSwapBuffers = BOOL(WINAPI*)(HDC);
static fn_wglSwapBuffers oSwapBuffers = nullptr;

constexpr float PLAYER_HEIGHT     = 5.0f;
constexpr int   INFINITE_AMMO_VAL = 999;
constexpr float FONT_SIZE         = 14.0f;

static uintptr_t moduleBase   = 0;
static HMODULE   g_hModule    = NULL;
static volatile bool g_espEnabled   = true;
static volatile bool g_infiniteAmmo = false;
static volatile bool g_running      = true;
static GLuint g_fontBase  = 0;
static bool   g_fontReady = false;

struct Vec3 { float x, y, z; };

template <class T>
T Mem(uintptr_t addr) {
    return *reinterpret_cast<T*>(addr);
}

// column-major (opengl) mvp multiply
bool WorldToScreen(Vec3 pos, Vec3& out, float m[4][4], int W, int H) {
    float clipx = pos.x*m[0][0] + pos.y*m[1][0] + pos.z*m[2][0] + m[3][0];
    float clipy = pos.x*m[0][1] + pos.y*m[1][1] + pos.z*m[2][1] + m[3][1];
    float clipw = pos.x*m[0][3] + pos.y*m[1][3] + pos.z*m[2][3] + m[3][3];
    if (clipw < 0.1f) return false;
    float invW = 1.0f / clipw;
    out.x = (clipx * invW + 1.0f) * (W / 2.0f);
    out.y = (-clipy * invW + 1.0f) * (H / 2.0f);
    return true;
}

static void GLLine(float x1, float y1, float x2, float y2,
                   float r, float g, float b, float a = 1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_LINES);
    glVertex2f(x1, y1);
    glVertex2f(x2, y2);
    glEnd();
}

static void GLRect(float x, float y, float w, float h,
                   float r, float g, float b, float a = 1.0f) {
    GLLine(x, y, x + w, y, r, g, b, a);
    GLLine(x + w, y, x + w, y + h, r, g, b, a);
    GLLine(x + w, y + h, x, y + h, r, g, b, a);
    GLLine(x, y + h, x, y, r, g, b, a);
}

static void GLFilledRect(float x, float y, float w, float h,
                         float r, float g, float b, float a = 1.0f) {
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

static void GLHealthBar(float x, float y, float boxH, int hp) {
    float barW = 3.0f;
    float barX = x - barW - 2.0f;
    GLFilledRect(barX, y, barW, boxH, 0.16f, 0.16f, 0.16f);
    float fillH = boxH * (hp / 100.0f);
    float r, g, b;
    if (hp > 60)      { r = 0;    g = 1;     b = 0; }
    else if (hp > 30) { r = 1;    g = 0.78f; b = 0; }
    else              { r = 1;    g = 0;     b = 0; }
    GLFilledRect(barX, y + boxH - fillH, barW, fillH, r, g, b);
}

static void InitFont(HDC hdc) {
    g_fontBase = glGenLists(256);
    if (!g_fontBase) { Log("glGenLists failed\n"); return; }
    HFONT font = CreateFontA((int)FONT_SIZE, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    HFONT old = (HFONT)SelectObject(hdc, font);
    BOOL ok = wglUseFontBitmapsA(hdc, 0, 256, g_fontBase);
    SelectObject(hdc, old);
    DeleteObject(font);
    g_fontReady = (ok != FALSE);
    Log("font: %s\n", g_fontReady ? "ok" : "failed");
}

// y is top of text, rasterpos needs baseline so we offset by font size
static void GLText(float x, float y, float r, float g, float b, const char* text) {
    if (!g_fontReady || !text || !*text) return;
    glColor3f(r, g, b);
    glRasterPos2f(x, y + FONT_SIZE);
    glPushAttrib(GL_LIST_BIT);
    glListBase(g_fontBase);
    glCallLists((GLsizei)strlen(text), GL_UNSIGNED_BYTE, (const GLvoid*)text);
    glPopAttrib();
}

static void ApplyInfiniteAmmo(uintptr_t lp) {
    if (!lp) return;
    __try {
        *reinterpret_cast<int*>(lp + offAmmoPistol)  = INFINITE_AMMO_VAL;
        *reinterpret_cast<int*>(lp + offAmmoTMP)     = INFINITE_AMMO_VAL;
        *reinterpret_cast<int*>(lp + offAmmoShotgun) = INFINITE_AMMO_VAL;
        *reinterpret_cast<int*>(lp + offAmmoSMG)     = INFINITE_AMMO_VAL;
        *reinterpret_cast<int*>(lp + offAmmoSniper)  = INFINITE_AMMO_VAL;
        *reinterpret_cast<int*>(lp + offAmmoRifle)   = INFINITE_AMMO_VAL;
        *reinterpret_cast<int*>(lp + offAmmoGrenade) = INFINITE_AMMO_VAL;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void RenderESP(int W, int H) {
    __try {
        uintptr_t localPlayer = Mem<uintptr_t>(moduleBase + offLocalPlayer);

        static int logFrames = 0;
        if (logFrames < 3) {
            logFrames++;
            int ec = Mem<int>(moduleBase + offEntityCount);
            uintptr_t el = Mem<uintptr_t>(moduleBase + offEntityList);
            Log("frame %d: lp=0x%X el=0x%X ec=%d\n",
                logFrames, (unsigned)localPlayer, (unsigned)el, ec);
        }

        if (!localPlayer) return;

        if (g_infiniteAmmo) ApplyInfiniteAmmo(localPlayer);
        if (!g_espEnabled) return;

        int entityCount      = Mem<int>(moduleBase + offEntityCount);
        uintptr_t entityList = Mem<uintptr_t>(moduleBase + offEntityList);
        if (!entityList || entityCount <= 0 || entityCount > 256) return;

        float viewMatrix[4][4];
        memcpy(&viewMatrix, reinterpret_cast<void*>(moduleBase + offViewMatrix), sizeof(viewMatrix));

        for (int i = 0; i < entityCount; i++) {
            uintptr_t ent = Mem<uintptr_t>(entityList + (i * 4));
            if (!ent || ent == localPlayer) continue;

            int hp = Mem<int>(ent + offHealth);
            if (hp <= 0 || hp > 100) continue;

            Vec3 feet = { Mem<float>(ent + offPosX), Mem<float>(ent + offPosY), Mem<float>(ent + offPosZ) };
            Vec3 head = feet;
            head.z += PLAYER_HEIGHT;

            Vec3 sFeet, sHead;
            if (!WorldToScreen(feet, sFeet, viewMatrix, W, H)) continue;
            if (!WorldToScreen(head, sHead, viewMatrix, W, H)) continue;
            if (sFeet.x < -50 || sFeet.x > W + 50 || sFeet.y < -50 || sFeet.y > H + 50) continue;

            float boxH = fabsf(sHead.y - sFeet.y);
            if (boxH < 2.0f) continue;
            float boxW = boxH * 0.4f;
            float bx = sHead.x - boxW / 2.0f;
            float by = sHead.y;

            GLRect(bx, by, boxW, boxH, 1, 1, 1);
            GLHealthBar(bx, by, boxH, hp);

            char name[32] = { 0 };
            memcpy(name, reinterpret_cast<void*>(ent + offName), sizeof(name) - 1);
            GLText(bx, by - FONT_SIZE - 2, 1, 1, 1, name);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int exCount = 0;
        if (exCount++ < 5)
            Log("RenderESP exception #%d, offsets probably wrong\n", exCount);
    }
}

static void RenderHUD(int H) {
    char buf[64];
    sprintf_s(buf, "esp:%s  ammo:%s  [end=unload]",
              g_espEnabled   ? "on" : "off",
              g_infiniteAmmo ? "on" : "off");
    GLText(10, (float)H - 24, 0, 1, 0, buf);
}

static BOOL WINAPI hkSwapBuffers(HDC hdc) {
    if (!g_fontReady) InitFont(hdc);

    if (GetAsyncKeyState(VK_F2) & 1) {
        g_infiniteAmmo = !g_infiniteAmmo;
        Log("ammo: %s\n", g_infiniteAmmo ? "on" : "off");
    }
    if (GetAsyncKeyState(VK_F3) & 1) {
        g_espEnabled = !g_espEnabled;
        Log("esp: %s\n", g_espEnabled ? "on" : "off");
    }
    if (GetAsyncKeyState(VK_END) & 1) {
        Log("unloading\n");
        g_running = false;
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int W = vp[2], H = vp[3];
    glOrtho(0.0, (double)W, (double)H, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);

    RenderESP(W, H);
    RenderHUD(H);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopClientAttrib();
    glPopAttrib();

    return oSwapBuffers(hdc);
}

static void* g_pSwapBuffers = nullptr;

static bool InstallHooks() {
    MH_STATUS st = MH_Initialize();
    Log("MH_Initialize: %s\n", MH_StatusToString(st));
    if (st != MH_OK) return false;

    HMODULE hGL = GetModuleHandleA("opengl32.dll");
    Log("opengl32 handle: 0x%p\n", hGL);
    if (!hGL) return false;

    g_pSwapBuffers = (void*)GetProcAddress(hGL, "wglSwapBuffers");
    Log("wglSwapBuffers: 0x%p\n", g_pSwapBuffers);
    if (!g_pSwapBuffers) return false;

    st = MH_CreateHook(g_pSwapBuffers, &hkSwapBuffers, reinterpret_cast<LPVOID*>(&oSwapBuffers));
    Log("MH_CreateHook: %s\n", MH_StatusToString(st));
    if (st != MH_OK) return false;

    st = MH_EnableHook(g_pSwapBuffers);
    Log("MH_EnableHook: %s\n", MH_StatusToString(st));
    return st == MH_OK;
}

static void RemoveHooks() {
    Log("removing hooks\n");
    if (g_fontReady && g_fontBase) {
        glDeleteLists(g_fontBase, 256);
        g_fontReady = false;
    }
    MH_DisableHook(g_pSwapBuffers);
    MH_RemoveHook(g_pSwapBuffers);
    MH_Uninitialize();
}

static DWORD WINAPI MainThread(LPVOID param) {
    g_hModule = static_cast<HMODULE>(param);

    OpenConsole();
    Log("injected, waiting for window\n");

    while (!FindWindowA(NULL, "AssaultCube")) Sleep(200);
    Log("window found\n");
    Sleep(500);

    moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("ac_client.exe"));
    Log("base: 0x%X\n", (unsigned)moduleBase);
    if (!moduleBase) {
        Log("base not found, bailing\n");
        CloseConsole();
        FreeLibraryAndExitThread(g_hModule, 1);
        return 1;
    }

    ResolveOffsets(moduleBase);

    if (!InstallHooks()) {
        Log("hook install failed\n");
        MH_Uninitialize();
        CloseConsole();
        FreeLibraryAndExitThread(g_hModule, 1);
        return 1;
    }

    Log("running  f2=ammo  f3=esp  end=unload\n");

    while (g_running) Sleep(100);

    RemoveHooks();
    Sleep(200);
    CloseConsole();
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, MainThread, hModule, 0, NULL);
    }
    return TRUE;
}
