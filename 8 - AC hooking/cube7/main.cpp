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

using fn_wglSwapBuffers = BOOL(WINAPI*)(HDC);
static fn_wglSwapBuffers oSwapBuffers = nullptr;

// v1.3.0.2
DWORD offEntityList  = 0x18AC04;
DWORD offEntityCount = 0x18AC0C;
DWORD offLocalPlayer = 0x0017E0A8;
uintptr_t offViewMatrix = 0x17DFD0;
DWORD offPosX   = 0x28;
DWORD offPosY   = 0x2C;
DWORD offPosZ   = 0x30;
DWORD offHealth = 0xEC;
DWORD offName   = 0x205;
DWORD offTeam   = 0x31C;

DWORD offAmmoPistol  = 0x12C;
DWORD offAmmoTMP     = 0x130;
DWORD offAmmoShotgun = 0x134;
DWORD offAmmoSMG     = 0x138;
DWORD offAmmoSniper  = 0x13C;
DWORD offAmmoRifle   = 0x140;
DWORD offAmmoGrenade = 0x144;

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
    *reinterpret_cast<int*>(lp + offAmmoPistol)  = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(lp + offAmmoTMP)     = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(lp + offAmmoShotgun) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(lp + offAmmoSMG)     = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(lp + offAmmoSniper)  = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(lp + offAmmoRifle)   = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(lp + offAmmoGrenade) = INFINITE_AMMO_VAL;
}

static void RenderESP(int W, int H) {
    uintptr_t localPlayer = Mem<uintptr_t>(moduleBase + offLocalPlayer);
    if (!localPlayer) return;

    if (g_infiniteAmmo) ApplyInfiniteAmmo(localPlayer);
    if (!g_espEnabled) return;

    int entityCount      = Mem<int>(moduleBase + offEntityCount);
    uintptr_t entityList = Mem<uintptr_t>(moduleBase + offEntityList);

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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.0f);

    RenderESP(W, H);
    RenderHUD(H);

    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
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
