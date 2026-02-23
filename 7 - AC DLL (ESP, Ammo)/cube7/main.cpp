// cube7 - Assault Cube DLL: ESP (boxes, health, names) + infinite ammo toggle
// Inject into ac_client.exe. Uses DirectX 9 overlay. F2 = infinite ammo toggle.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <dwmapi.h>
#include <cstdio>
#include <cmath>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dwmapi.lib")

const char* WINDOW_NAME = "AssaultCube";

// Offsets (ac_client.exe v1.3.0.2)
DWORD offEntityList  = 0x18AC04;
DWORD offEntityCount = 0x18AC0C;
DWORD offLocalPlayer = 0x0017E0A8;
DWORD offViewMatrix = 0x17DFD0;  // offset from module base (0x57DFD0 - 0x400000)
DWORD offPosX = 0x28;
DWORD offPosY = 0x2C;
DWORD offPosZ = 0x30;
DWORD offHealth = 0xEC;
DWORD offName = 0x205;
DWORD offTeam = 0x30C;

// Ammo offsets from local player
DWORD offAmmoPistol = 0x12C;
DWORD offAmmoTMP = 0x130;
DWORD offAmmoShotgun = 0x134;
DWORD offAmmoSMG = 0x138;
DWORD offAmmoSniper = 0x13C;
DWORD offAmmoRifle = 0x140;
DWORD offAmmoGrenade = 0x144;

constexpr float PLAYER_HEIGHT = 5.0f;
constexpr int INFINITE_AMMO_VAL = 999;

HWND gameHwnd = NULL;
HWND myHwnd = NULL;
IDirect3D9* pD3D = NULL;
IDirect3DDevice9* pDevice = NULL;
uintptr_t moduleBase = 0;
D3DPRESENT_PARAMETERS pParams = {};
int WIDTH = 800;
int HEIGHT = 600;
MARGINS margins = { -1, -1, -1, -1 };

volatile bool g_infiniteAmmo = false;
volatile bool g_running = true;

struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

template <class T>
T Read(uintptr_t addr) {
    return *reinterpret_cast<T*>(addr);
}

bool WorldToScreen(Vec3 pos, Vec3& out, float m[4][4], int W, int H) {
    Vec4 clip;
    clip.x = pos.x * m[0][0] + pos.y * m[1][0] + pos.z * m[2][0] + m[3][0];
    clip.y = pos.x * m[0][1] + pos.y * m[1][1] + pos.z * m[2][1] + m[3][1];
    clip.z = pos.x * m[0][2] + pos.y * m[1][2] + pos.z * m[2][2] + m[3][2];
    clip.w = pos.x * m[0][3] + pos.y * m[1][3] + pos.z * m[2][3] + m[3][3];
    if (clip.w < 0.1f) return false;
    float invW = 1.0f / clip.w;
    out.x = (clip.x * invW + 1.0f) * (W / 2.0f);
    out.y = (-clip.y * invW + 1.0f) * (H / 2.0f);
    return true;
}

struct CUSTOMVERTEX { float x, y, z, rhw; DWORD color; };
#define D3DFVF_CUSTOM (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

void DrawLine(float x1, float y1, float x2, float y2, D3DCOLOR col) {
    CUSTOMVERTEX v[2] = {
        { x1, y1, 0.f, 1.f, col },
        { x2, y2, 0.f, 1.f, col }
    };
    pDevice->SetFVF(D3DFVF_CUSTOM);
    pDevice->DrawPrimitiveUP(D3DPT_LINELIST, 1, v, sizeof(CUSTOMVERTEX));
}

void DrawRect(float x, float y, float w, float h, D3DCOLOR col) {
    DrawLine(x, y, x + w, y, col);
    DrawLine(x + w, y, x + w, y + h, col);
    DrawLine(x + w, y + h, x, y + h, col);
    DrawLine(x, y + h, x, y, col);
}

void DrawFilledRect(float x, float y, float w, float h, D3DCOLOR col) {
    CUSTOMVERTEX v[4] = {
        { x, y, 0.f, 1.f, col },
        { x + w, y, 0.f, 1.f, col },
        { x + w, y + h, 0.f, 1.f, col },
        { x, y + h, 0.f, 1.f, col }
    };
    pDevice->SetFVF(D3DFVF_CUSTOM);
    pDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(CUSTOMVERTEX));
}

void DrawHealthBar(float x, float y, float boxH, int hp) {
    float barW = 3.0f;
    float barX = x - barW - 2.0f;
    float barH = boxH;
    DrawFilledRect(barX, y, barW, barH, D3DCOLOR_XRGB(40, 40, 40));
    float fillH = barH * (hp / 100.0f);
    D3DCOLOR hpCol = hp > 60 ? D3DCOLOR_XRGB(0, 255, 0) :
                    hp > 30 ? D3DCOLOR_XRGB(255, 200, 0) :
                    D3DCOLOR_XRGB(255, 0, 0);
    DrawFilledRect(barX, y + barH - fillH, barW, fillH, hpCol);
}

struct ESPTextEntry { int x, y; DWORD color; char text[32]; };
ESPTextEntry g_textQueue[64];
int g_textCount = 0;

void QueueText(int x, int y, DWORD color, const char* text) {
    if (g_textCount >= 64) return;
    g_textQueue[g_textCount].x = x;
    g_textQueue[g_textCount].y = y;
    g_textQueue[g_textCount].color = color;
    strncpy_s(g_textQueue[g_textCount].text, text, _TRUNCATE);
    g_textCount++;
}

void RenderESPText() {
    if (g_textCount == 0) return;
    HDC hdc = GetDC(myHwnd);
    if (!hdc) return;
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < g_textCount; i++) {
        auto& e = g_textQueue[i];
        SetTextColor(hdc, RGB((e.color >> 16) & 0xFF, (e.color >> 8) & 0xFF, e.color & 0xFF));
        TextOutA(hdc, e.x, e.y, e.text, (int)strlen(e.text));
    }
    ReleaseDC(myHwnd, hdc);
}

void ApplyInfiniteAmmo(uintptr_t localPlayer) {
    if (!localPlayer) return;
    *reinterpret_cast<int*>(localPlayer + offAmmoPistol) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(localPlayer + offAmmoTMP) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(localPlayer + offAmmoShotgun) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(localPlayer + offAmmoSMG) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(localPlayer + offAmmoSniper) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(localPlayer + offAmmoRifle) = INFINITE_AMMO_VAL;
    *reinterpret_cast<int*>(localPlayer + offAmmoGrenade) = INFINITE_AMMO_VAL;
}

void RenderESP() {
    g_textCount = 0;

    if (GetAsyncKeyState(VK_F2) & 1) g_infiniteAmmo = !g_infiniteAmmo;

    uintptr_t localPlayer = Read<uintptr_t>(moduleBase + offLocalPlayer);
    if (!localPlayer) return;

    if (g_infiniteAmmo) ApplyInfiniteAmmo(localPlayer);

    int entityCount = Read<int>(moduleBase + offEntityCount);
    uintptr_t entityList = Read<uintptr_t>(moduleBase + offEntityList);

    float viewMatrix[4][4];
    memcpy(&viewMatrix, reinterpret_cast<void*>(moduleBase + offViewMatrix), sizeof(viewMatrix));

    for (int i = 0; i < entityCount; i++) {
        uintptr_t ent = Read<uintptr_t>(entityList + (i * 4));
        if (!ent || ent == localPlayer) continue;

        int hp = Read<int>(ent + offHealth);
        if (hp <= 0 || hp > 100) continue;

        Vec3 feet = { Read<float>(ent + offPosX), Read<float>(ent + offPosY), Read<float>(ent + offPosZ) };
        Vec3 head = feet;
        head.z += PLAYER_HEIGHT;

        Vec3 sFeet, sHead;
        if (!WorldToScreen(feet, sFeet, viewMatrix, WIDTH, HEIGHT)) continue;
        if (!WorldToScreen(head, sHead, viewMatrix, WIDTH, HEIGHT)) continue;
        if (sFeet.x < -50 || sFeet.x > WIDTH + 50 || sFeet.y < -50 || sFeet.y > HEIGHT + 50) continue;

        float boxH = fabsf(sHead.y - sFeet.y);
        if (boxH < 2.0f) continue;
        float boxW = boxH * 0.4f;

        float bx = sHead.x - boxW / 2.0f;
        float by = sHead.y;

        DrawRect(bx, by, boxW, boxH, D3DCOLOR_XRGB(255, 255, 255));
        DrawHealthBar(bx, by, boxH, hp);

        char name[32] = { 0 };
        memcpy(name, reinterpret_cast<void*>(ent + offName), sizeof(name) - 1);
        QueueText((int)bx, (int)by - 14, D3DCOLOR_XRGB(255, 255, 255), name);
    }

    if (g_infiniteAmmo) {
        QueueText(10, HEIGHT - 24, D3DCOLOR_XRGB(0, 255, 0), "Infinite Ammo: ON (F2)");
    }
}

LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void RunOverlay() {
    gameHwnd = FindWindowA(NULL, WINDOW_NAME);
    if (!gameHwnd) return;

    moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("ac_client.exe"));
    if (!moduleBase) return;

    RECT rect;
    GetWindowRect(gameHwnd, &rect);
    WIDTH = rect.right - rect.left;
    HEIGHT = rect.bottom - rect.top;

    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WinProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "CUBE7ESP";
    RegisterClassExA(&wc);

    myHwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        "CUBE7ESP", "Overlay", WS_POPUP,
        rect.left, rect.top, WIDTH, HEIGHT,
        NULL, NULL, GetModuleHandleA(NULL), NULL);

    SetLayeredWindowAttributes(myHwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
    DwmExtendFrameIntoClientArea(myHwnd, &margins);
    ShowWindow(myHwnd, SW_SHOWDEFAULT);

    pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return;

    ZeroMemory(&pParams, sizeof(pParams));
    pParams.Windowed = TRUE;
    pParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pParams.BackBufferFormat = D3DFMT_A8R8G8B8;
    pParams.BackBufferWidth = WIDTH;
    pParams.BackBufferHeight = HEIGHT;
    pParams.EnableAutoDepthStencil = TRUE;
    pParams.AutoDepthStencilFormat = D3DFMT_D16;

    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, myHwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pParams, &pDevice);
    if (FAILED(hr)) { pD3D->Release(); return; }

    pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
    pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    MSG msg;
    while (g_running && FindWindowA(NULL, WINDOW_NAME)) {
        GetWindowRect(gameHwnd, &rect);
        WIDTH = rect.right - rect.left;
        HEIGHT = rect.bottom - rect.top;
        SetWindowPos(myHwnd, HWND_TOPMOST, rect.left, rect.top, WIDTH, HEIGHT,
            SWP_NOACTIVATE | SWP_NOOWNERZORDER);

        pDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
        if (SUCCEEDED(pDevice->BeginScene())) {
            RenderESP();
            pDevice->EndScene();
        }
        pDevice->Present(NULL, NULL, NULL, NULL);
        RenderESPText();

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(8);
    }

    pDevice->Release();
    pD3D->Release();
}

DWORD WINAPI MainThread(LPVOID param) {
    HMODULE hMod = static_cast<HMODULE>(param);
    while (!FindWindowA(NULL, WINDOW_NAME)) Sleep(500);
    RunOverlay();
    g_running = false;
    FreeLibraryAndExitThread(hMod, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, MainThread, hModule, 0, NULL);
    }
    return TRUE;
}
