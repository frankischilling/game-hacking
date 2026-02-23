// cube7 - ac_client.exe internal cheat
// hooks wglSwapBuffers, renders esp + ammo hack + aimbot
// f2 = ammo | f3 = esp | f4 = aimbot | end = unload

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

// find first 4-byte occurrence of absAddr in [base, base+size)
static uintptr_t FindXRef(uintptr_t base, size_t size, uintptr_t absAddr) {
    const BYTE* mem = reinterpret_cast<const BYTE*>(base);
    for (size_t i = 0; i + 4 <= size; i++) {
        if (*reinterpret_cast<const uintptr_t*>(mem + i) == absAddr)
            return base + i;
    }
    return 0;
}

struct SigPattern { const char* pat; int ptrOff; };

struct SigEntry {
    const char*  name;
    SigPattern   sigs[5];
    int          numSigs;
    uintptr_t    fallback;
    uintptr_t*   target;
};

// offsets (resolved at runtime)
static uintptr_t offLocalPlayer = 0;
static uintptr_t offEntityList  = 0;
static uintptr_t offEntityCount = 0;
static uintptr_t offViewMatrix  = 0;

// player struct offsets
constexpr DWORD offPosX   = 0x28;
constexpr DWORD offPosY   = 0x2C;
constexpr DWORD offPosZ   = 0x30;
constexpr DWORD offYaw    = 0x34;
constexpr DWORD offPitch  = 0x38;
constexpr DWORD offHealth = 0xEC;
constexpr DWORD offArmor  = 0xF0;
constexpr DWORD offName   = 0x205;
constexpr DWORD offTeam   = 0x30C;

constexpr DWORD offAmmoPistol  = 0x12C;
constexpr DWORD offAmmoTMP     = 0x130;
constexpr DWORD offAmmoShotgun = 0x134;
constexpr DWORD offAmmoSMG     = 0x138;
constexpr DWORD offAmmoSniper  = 0x13C;
constexpr DWORD offAmmoRifle   = 0x140;
constexpr DWORD offAmmoGrenade = 0x144;

constexpr float AIM_FOV      = 30.0f;
constexpr float AIM_SMOOTH   = 5.0f;
constexpr float PI           = 3.14159265f;
constexpr float DEG2RAD      = PI / 180.0f;
constexpr float RAD2DEG      = 180.0f / PI;

// multiple patterns per target, tried in order
// pattern = instruction bytes with ?? for the 4-byte address operand
// ptrOff  = byte offset within the match where the address sits
static SigEntry g_sigs[] = {
    { "localPlayer", {
        { "8B 0D ?? ?? ?? ?? 8B 01 50",             2 },
        { "8B 0D ?? ?? ?? ?? 85 C9 74",             2 },
        { "8B 0D ?? ?? ?? ?? 56 57 8B 7C 24",       2 },
        { "A1 ?? ?? ?? ?? 85 C0 74",                1 },
    }, 4, 0x18AC00, &offLocalPlayer },

    { "entityList", {
        { "A1 ?? ?? ?? ?? 8B 04 B8",                1 },
        { "A1 ?? ?? ?? ?? 8B 0C B0",                1 },
        { "A1 ?? ?? ?? ?? 83 C4 ?? 8B 04 B8",       1 },
        { "8B 0D ?? ?? ?? ?? 8B 14 B1",             2 },
    }, 4, 0x18AC04, &offEntityList },

    { "entityCount", {
        { "3B 05 ?? ?? ?? ?? 7C",                   2 },
        { "A1 ?? ?? ?? ?? 48 85 C0",                1 },
        { "8B 0D ?? ?? ?? ?? 49",                   2 },
        { "8B 0D ?? ?? ?? ?? 89 44 24 ?? 85 C9",    2 },
    }, 4, 0x18AC0C, &offEntityCount },

    { "viewMatrix", {
        { "A1 ?? ?? ?? ?? C7 44 24 ?? 00 00 00 00 89 44 24", 1 },
        { "A1 ?? ?? ?? ?? 89 44 24 ?? D9",                   1 },
        { "68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 83 C4",             1 },
    }, 3, 0x17DFD0, &offViewMatrix },
};

static void ResolveOffsets(uintptr_t base) {
    size_t modSize = GetModuleSize(base);
    Log("module size: 0x%X\n", (unsigned)modSize);

    for (int i = 0; i < _countof(g_sigs); i++) {
        auto& s = g_sigs[i];
        bool resolved = false;

        // method 1: xref scan — find any instruction that contains base+fallback as an operand
        // most reliable: confirms the exact address exists in code
        if (modSize > 0) {
            uintptr_t ref = FindXRef(base, modSize, base + s.fallback);
            if (ref) {
                *s.target = s.fallback;
                resolved = true;
                Log("%-14s xref @ +0x%X confirmed offset 0x%X\n",
                    s.name, (unsigned)(ref - base), (unsigned)s.fallback);
            }
        }

        // method 2: pattern scan (for different builds where fallback address shifted)
        // drift-checked against fallback to reject false positives
        for (int p = 0; p < s.numSigs && !resolved; p++) {
            uintptr_t match = (modSize > 0) ? FindPattern(base, modSize, s.sigs[p].pat) : 0;
            if (!match) continue;

            uintptr_t candidate = ExtractOffset(match, s.sigs[p].ptrOff, base);
            if (candidate == 0 || candidate >= modSize) continue;

            uintptr_t drift = (candidate > s.fallback)
                ? (candidate - s.fallback) : (s.fallback - candidate);
            if (drift > 0x3000) {
                Log("%-14s sig[%d] +0x%X -> 0x%X (drift 0x%X, skip)\n",
                    s.name, p, (unsigned)(match - base), (unsigned)candidate, (unsigned)drift);
                continue;
            }

            __try {
                volatile uintptr_t probe = *reinterpret_cast<uintptr_t*>(base + candidate);
                (void)probe;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            *s.target = candidate;
            resolved = true;
            Log("%-14s sig[%d] +0x%X -> offset 0x%X\n",
                s.name, p, (unsigned)(match - base), (unsigned)candidate);
        }

        // method 3: hardcoded fallback
        if (!resolved) {
            *s.target = s.fallback;
            Log("%-14s fallback 0x%X\n", s.name, (unsigned)s.fallback);
        }
    }
}

// globals 

using fn_wglSwapBuffers = BOOL(WINAPI*)(HDC);
static fn_wglSwapBuffers oSwapBuffers = nullptr;

constexpr int   INFINITE_AMMO_VAL = 999;
constexpr float FONT_SIZE         = 14.0f;

static uintptr_t moduleBase   = 0;
static HMODULE   g_hModule    = NULL;
static volatile bool g_espEnabled   = true;
static volatile bool g_infiniteAmmo = false;
static volatile bool g_aimbotOn     = false;
static volatile bool g_running      = true;
static uintptr_t     g_aimTarget    = 0;       // entity ptr of current lock
static float         g_aimTargetFov = 0;       // fov angle to current target
static GLuint g_fontBase   = 0;
static bool   g_fontReady  = false;
static bool   g_teamGame   = false;

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

static void GLCircle(float cx, float cy, float radius,
                     float r, float g, float b, float a = 1.0f, int segs = 48) {
    glColor4f(r, g, b, a);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < segs; i++) {
        float theta = 2.0f * PI * (float)i / (float)segs;
        glVertex2f(cx + cosf(theta) * radius, cy + sinf(theta) * radius);
    }
    glEnd();
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

// returns true if multiple teams exist in the entity list (team mode, not FFA)
static bool IsTeamGame() {
    __try {
        uintptr_t lp = Mem<uintptr_t>(moduleBase + offLocalPlayer);
        if (!lp) return false;
        int myTeam = Mem<int>(lp + offTeam);
        int ec = Mem<int>(moduleBase + offEntityCount);
        uintptr_t el = Mem<uintptr_t>(moduleBase + offEntityList);
        if (!el || ec <= 0 || ec > 256) return false;
        for (int i = 0; i < ec; i++) {
            uintptr_t ent = Mem<uintptr_t>(el + (i * 4));
            if (!ent || ent == lp) continue;
            if (Mem<int>(ent + offTeam) != myTeam) return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// AC convention: forward = (sin(yaw), -cos(yaw), -sin(pitch))
static Vec3 CalcAngle(Vec3 from, Vec3 to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    float dz = to.z - from.z;
    float dist = sqrtf(dx*dx + dy*dy);
    Vec3 ang;
    ang.x = atan2f(dz, dist) * RAD2DEG;
    ang.y = atan2f(dx, -dy) * RAD2DEG;
    ang.z = 0;
    return ang;
}

static float AngleDist(float a, float b) {
    float d = fmodf(a - b + 180.0f, 360.0f);
    if (d < 0) d += 360.0f;
    return d - 180.0f;
}

static void RunAimbot() {
    if (!g_aimbotOn) { g_aimTarget = 0; return; }
    __try {
        uintptr_t localPlayer = Mem<uintptr_t>(moduleBase + offLocalPlayer);
        if (!localPlayer) { g_aimTarget = 0; return; }

        constexpr float EYE_HEIGHT = 4.5f;
        Vec3 myPos = {
            Mem<float>(localPlayer + offPosX),
            Mem<float>(localPlayer + offPosY),
            Mem<float>(localPlayer + offPosZ)
        };
        myPos.z += EYE_HEIGHT;

        float myYaw   = Mem<float>(localPlayer + offYaw);
        float myPitch = Mem<float>(localPlayer + offPitch);
        int myTeam    = Mem<int>(localPlayer + offTeam);

        int entityCount      = Mem<int>(moduleBase + offEntityCount);
        uintptr_t entityList = Mem<uintptr_t>(moduleBase + offEntityList);
        if (!entityList || entityCount <= 0 || entityCount > 256) { g_aimTarget = 0; return; }

        static bool loggedAim = false;
        if (!loggedAim) {
            loggedAim = true;
            Log("aim debug: myPos=(%.1f,%.1f,%.1f) yaw=%.1f pitch=%.1f team=%d ec=%d teamgame=%d\n",
                myPos.x, myPos.y, myPos.z, myYaw, myPitch, myTeam, entityCount, (int)g_teamGame);
        }

        float     bestFov   = AIM_FOV;
        Vec3      bestAngle = { myPitch, myYaw, 0 };
        uintptr_t bestEnt   = 0;

        for (int i = 0; i < entityCount; i++) {
            uintptr_t ent = Mem<uintptr_t>(entityList + (i * 4));
            if (!ent || ent == localPlayer) continue;

            int hp = Mem<int>(ent + offHealth);
            if (hp <= 0 || hp > 100) continue;

            int team = Mem<int>(ent + offTeam);
            if (g_teamGame && team == myTeam) continue;

            Vec3 targetHead = {
                Mem<float>(ent + offPosX),
                Mem<float>(ent + offPosY),
                Mem<float>(ent + offPosZ)
            };
            targetHead.z += EYE_HEIGHT;

            Vec3 ang = CalcAngle(myPos, targetHead);

            float dYaw   = fabsf(AngleDist(ang.y, myYaw));
            float dPitch = fabsf(AngleDist(ang.x, myPitch));
            float fov = sqrtf(dYaw*dYaw + dPitch*dPitch);

            if (fov < bestFov) {
                bestFov   = fov;
                bestAngle = ang;
                bestEnt   = ent;
            }
        }

        uintptr_t prevTarget = g_aimTarget;

        if (bestEnt) {
            g_aimTarget    = bestEnt;
            g_aimTargetFov = bestFov;

            float newYaw   = myYaw   + AngleDist(bestAngle.y, myYaw)   / AIM_SMOOTH;
            float newPitch = myPitch + AngleDist(bestAngle.x, myPitch) / AIM_SMOOTH;
            *reinterpret_cast<float*>(localPlayer + offYaw)   = newYaw;
            *reinterpret_cast<float*>(localPlayer + offPitch) = newPitch;

            if (prevTarget != bestEnt) {
                char tname[32] = { 0 };
                memcpy(tname, reinterpret_cast<void*>(bestEnt + offName), sizeof(tname) - 1);
                Log("aim locked: %s (fov=%.1f)\n", tname, bestFov);
            }
        } else {
            g_aimTarget    = 0;
            g_aimTargetFov = 0;
            if (prevTarget != 0)
                Log("aim lost target\n");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_aimTarget = 0;
        static int exCount = 0;
        if (exCount++ < 5)
            Log("aimbot exception #%d\n", exCount);
    }
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

        int myTeam           = Mem<int>(localPlayer + offTeam);
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

            int team = Mem<int>(ent + offTeam);
            bool friendly = (g_teamGame && team == myTeam);

            Vec3 feet = { Mem<float>(ent + offPosX), Mem<float>(ent + offPosY), Mem<float>(ent + offPosZ) };
            Vec3 head = feet;
            head.z += 5.0f;

            Vec3 sFeet, sHead;
            if (!WorldToScreen(feet, sFeet, viewMatrix, W, H)) continue;
            if (!WorldToScreen(head, sHead, viewMatrix, W, H)) continue;
            if (sFeet.x < -50 || sFeet.x > W + 50 || sFeet.y < -50 || sFeet.y > H + 50) continue;

            float boxH = fabsf(sHead.y - sFeet.y);
            if (boxH < 2.0f) continue;
            float boxW = boxH * 0.4f;
            float bx = sHead.x - boxW / 2.0f;
            float by = sHead.y;

            bool isAimTarget = (g_aimbotOn && ent == g_aimTarget);

            float br, bg, bb;
            if (isAimTarget)       { br = 1;    bg = 0.2f; bb = 0.2f; }
            else if (friendly)     { br = 0.2f; bg = 0.6f; bb = 1;    }
            else                   { br = 1;    bg = 1;    bb = 1;    }

            GLRect(bx, by, boxW, boxH, br, bg, bb);
            if (isAimTarget)
                GLLine((float)(W / 2), (float)(H / 2), sHead.x, sHead.y, 1, 0.2f, 0.2f, 0.6f);
            GLHealthBar(bx, by, boxH, hp);

            char name[32] = { 0 };
            memcpy(name, reinterpret_cast<void*>(ent + offName), sizeof(name) - 1);
            float nr = br, ng = bg, nb = bb;
            GLText(bx, by - FONT_SIZE - 2, nr, ng, nb, name);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        static int exCount = 0;
        if (exCount++ < 5)
            Log("RenderESP exception #%d, offsets probably wrong\n", exCount);
    }
}

static void RenderHUD(int W, int H) {
    char buf[128];

    if (g_aimbotOn) {
        // fov circle at screen center
        // approximate: AIM_FOV degrees mapped to pixels
        // rough conversion: (fov_deg / game_hfov) * screen_width/2
        // ~90 deg hfov assumed, so 1 degree ~ W/180 pixels
        float fovRadius = AIM_FOV * ((float)W / 180.0f);
        GLCircle((float)(W / 2), (float)(H / 2), fovRadius, 1, 0.2f, 0.2f, 0.35f);

        // crosshair dot
        GLFilledRect((float)(W/2) - 2, (float)(H/2) - 2, 4, 4, 1, 0.2f, 0.2f, 0.8f);

        if (g_aimTarget) {
            char tname[32] = { 0 };
            __try { memcpy(tname, reinterpret_cast<void*>(g_aimTarget + offName), sizeof(tname) - 1); }
            __except(EXCEPTION_EXECUTE_HANDLER) { strcpy_s(tname, "???"); }
            sprintf_s(buf, "aim: %s  fov:%.1f", tname, g_aimTargetFov);
            GLText(10, (float)H - 42, 1, 0.3f, 0.3f, buf);
        } else {
            GLText(10, (float)H - 42, 1, 0.3f, 0.3f, "aim: no target");
        }
    }

    sprintf_s(buf, "esp:%s  ammo:%s  aim:%s  [end=unload]",
              g_espEnabled   ? "on" : "off",
              g_infiniteAmmo ? "on" : "off",
              g_aimbotOn     ? "on" : "off");
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
    if (GetAsyncKeyState(VK_F4) & 1) {
        g_aimbotOn = !g_aimbotOn;
        Log("aimbot: %s\n", g_aimbotOn ? "on" : "off");
    }
    if (GetAsyncKeyState(VK_END) & 1) {
        Log("unloading\n");
        g_running = false;
    }

    g_teamGame = IsTeamGame();
    RunAimbot();

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
    RenderHUD(W, H);

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

    Log("running  f2=ammo  f3=esp  f4=aim  end=unload\n");

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
