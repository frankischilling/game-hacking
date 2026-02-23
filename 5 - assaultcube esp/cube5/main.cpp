#include <windows.h>
#include <iostream>
#include <d3d9.h>
#include <d3d9types.h>
#include <string.h>
#include <dwmapi.h>
#include <TlHelp32.h>
#include <vector>
#include <math.h>
#include <cstdio>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dwmapi.lib")

const char* WINDOW_NAME = "AssaultCube";

// offsets (ac_client.exe v1.3.0.2)
DWORD     offEntityList  = 0x18AC04;
DWORD     offEntityCount = 0x18AC0C;
DWORD     offLocalPlayer = 0x0017E0A8;
uintptr_t offViewMatrix  = 0x57DFD0;  // absolute static address, AC has no ASLR
DWORD offPosX   = 0x28;
DWORD offPosY   = 0x2C;
DWORD offPosZ   = 0x30;
DWORD offHealth = 0xEC;
DWORD offName   = 0x205;
DWORD offTeam   = 0x31C;  // 0=FFA 1=CLA 2=RVSF

// tune this if the box height looks wrong
constexpr float PLAYER_HEIGHT = 5.0f;

HWND               gameHwnd   = NULL;
HWND               myHwnd     = NULL;
IDirect3D9*        pD3D       = NULL;
IDirect3DDevice9*  pDevice    = NULL;
HANDLE             hProcess   = 0;
uintptr_t          moduleBase = 0;
D3DPRESENT_PARAMETERS pParams;
int WIDTH  = 800;
int HEIGHT = 600;
MARGINS margins = { -1, -1, -1, -1 };

void Log(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("[ESP] %s\n", buf);
}

uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    uintptr_t base = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;
    MODULEENTRY32 mod;
    mod.dwSize = sizeof(mod);
    if (Module32First(hSnap, &mod)) {
        do {
            if (!_wcsicmp(mod.szModule, modName)) { base = (uintptr_t)mod.modBaseAddr; break; }
        } while (Module32Next(hSnap, &mod));
    }
    CloseHandle(hSnap);
    return base;
}

template <class T>
T RPM(uintptr_t address) {
    T val = T();
    ReadProcessMemory(hProcess, (LPVOID)address, &val, sizeof(T), NULL);
    return val;
}

struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

// AC uses OpenGL column-major matrices: m[col][row]
bool WorldToScreen(Vec3 pos, Vec3& out, float m[4][4], int W, int H) {
    Vec4 clip;
    clip.x = pos.x*m[0][0] + pos.y*m[1][0] + pos.z*m[2][0] + m[3][0];
    clip.y = pos.x*m[0][1] + pos.y*m[1][1] + pos.z*m[2][1] + m[3][1];
    clip.z = pos.x*m[0][2] + pos.y*m[1][2] + pos.z*m[2][2] + m[3][2];
    clip.w = pos.x*m[0][3] + pos.y*m[1][3] + pos.z*m[2][3] + m[3][3];
    if (clip.w < 0.1f) return false;
    float invW = 1.0f / clip.w;
    out.x =  (clip.x * invW + 1.0f) * (W / 2.0f);
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
    DrawLine(x,   y,   x+w, y,   col);
    DrawLine(x+w, y,   x+w, y+h, col);
    DrawLine(x+w, y+h, x,   y+h, col);
    DrawLine(x,   y+h, x,   y,   col);
}

// name labels are queued here and flushed via GDI after Present
struct ESPTextEntry { int x, y; DWORD color; char text[32]; };
std::vector<ESPTextEntry> g_textQueue;

void RenderESPText() {
    if (g_textQueue.empty()) return;
    HDC hdc = GetDC(myHwnd);
    if (!hdc) return;
    SetBkMode(hdc, TRANSPARENT);
    for (auto& e : g_textQueue) {
        SetTextColor(hdc, RGB((e.color >> 16) & 0xFF, (e.color >> 8) & 0xFF, e.color & 0xFF));
        TextOutA(hdc, e.x, e.y, e.text, (int)strlen(e.text));
    }
    ReleaseDC(myHwnd, hdc);
}

void RenderESP() {
    g_textQueue.clear();

    uintptr_t localPlayer = RPM<uintptr_t>(moduleBase + offLocalPlayer);
    if (!localPlayer) { Log("local player pointer is null"); return; }

    int entityCount      = RPM<int>(moduleBase + offEntityCount);
    uintptr_t entityList = RPM<uintptr_t>(moduleBase + offEntityList);

    float viewMatrix[4][4];
    ReadProcessMemory(hProcess, (LPVOID)offViewMatrix, &viewMatrix, sizeof(viewMatrix), NULL);

    int myTeam   = RPM<int>(localPlayer + offTeam);
    int rendered = 0;

    for (int i = 0; i < entityCount; i++) {
        uintptr_t ent = RPM<uintptr_t>(entityList + (i * 4));
        if (!ent || ent == localPlayer) continue;

        int hp = RPM<int>(ent + offHealth);
        if (hp <= 0 || hp > 100) continue;

        Vec3 feet = { RPM<float>(ent + offPosX), RPM<float>(ent + offPosY), RPM<float>(ent + offPosZ) };
        Vec3 head = feet;
        head.z += PLAYER_HEIGHT;

        Vec3 sFeet, sHead;
        if (!WorldToScreen(feet, sFeet, viewMatrix, WIDTH, HEIGHT)) continue;
        if (!WorldToScreen(head, sHead, viewMatrix, WIDTH, HEIGHT)) continue;
        if (sFeet.x < 0 || sFeet.x > WIDTH || sFeet.y < 0 || sFeet.y > HEIGHT) continue;

        float boxH = fabsf(sHead.y - sFeet.y);
        if (boxH < 2.0f) continue;
        float boxW = boxH * 0.4f;

        int eTeam    = RPM<int>(ent + offTeam);
        D3DCOLOR col = (myTeam != 0 && myTeam == eTeam)
                       ? D3DCOLOR_XRGB(0, 255, 0)    // teammate = green
                       : D3DCOLOR_XRGB(255, 0, 0);   // enemy    = red

        float bx = sHead.x - boxW / 2.0f;
        float by = sHead.y;
        DrawRect(bx, by, boxW, boxH, col);

        char name[32] = { 0 };
        ReadProcessMemory(hProcess, (LPVOID)(ent + offName), name, sizeof(name) - 1, NULL);

        ESPTextEntry entry;
        entry.x = (int)bx;
        entry.y = (int)by - 14;
        entry.color = col;
        strncpy_s(entry.text, name, _TRUNCATE);
        g_textQueue.push_back(entry);

        Log("entity[%d] %-12s hp=%d feet=(%.1f,%.1f,%.1f) screen=(%.0f,%.0f)",
            i, name, hp, feet.x, feet.y, feet.z, sFeet.x, sFeet.y);
        rendered++;
    }

    if (rendered == 0) Log("no visible entities this frame");
}

LRESULT CALLBACK WinProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    AllocConsole();
    SetConsoleTitleA("ESP Console");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    Log("starting...");

    gameHwnd = FindWindowA(NULL, WINDOW_NAME);
    if (!gameHwnd) {
        Log("game window not found");
        MessageBoxA(NULL, "Game not found! Start AssaultCube first.", "Error", MB_ICONERROR);
        return 1;
    }
    Log("game window: 0x%p", (void*)gameHwnd);

    DWORD pID = 0;
    GetWindowThreadProcessId(gameHwnd, &pID);
    hProcess   = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
    moduleBase = GetModuleBaseAddress(pID, L"ac_client.exe");
    Log("PID=%lu moduleBase=0x%llX", pID, (unsigned long long)moduleBase);

    RECT rect;
    GetWindowRect(gameHwnd, &rect);
    WIDTH  = rect.right  - rect.left;
    HEIGHT = rect.bottom - rect.top;
    Log("window %dx%d", WIDTH, HEIGHT);

    WNDCLASSEXA wc = { 0 };
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WinProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = "ESPOVERLAY";
    RegisterClassExA(&wc);

    myHwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED,
        "ESPOVERLAY", "Overlay", WS_POPUP,
        rect.left, rect.top, WIDTH, HEIGHT,
        NULL, NULL, hInstance, NULL);

    SetLayeredWindowAttributes(myHwnd, RGB(0,0,0), 255, LWA_ALPHA);
    DwmExtendFrameIntoClientArea(myHwnd, &margins);
    ShowWindow(myHwnd, SW_SHOWDEFAULT);
    Log("overlay ready");

    pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    ZeroMemory(&pParams, sizeof(pParams));
    pParams.Windowed               = TRUE;
    pParams.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    pParams.BackBufferFormat       = D3DFMT_A8R8G8B8;
    pParams.BackBufferWidth        = WIDTH;
    pParams.BackBufferHeight       = HEIGHT;
    pParams.EnableAutoDepthStencil = TRUE;
    pParams.AutoDepthStencilFormat = D3DFMT_D16;

    HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, myHwnd,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pParams, &pDevice);
    if (FAILED(hr)) { Log("D3D device failed (0x%lX)", hr); return 1; }
    Log("D3D ready");

    MSG msg;
    while (true) {
        GetWindowRect(gameHwnd, &rect);
        MoveWindow(myHwnd, rect.left, rect.top,
                   rect.right - rect.left, rect.bottom - rect.top, TRUE);

        pDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0,0,0,0), 1.0f, 0);
        pDevice->BeginScene();
        RenderESP();
        pDevice->EndScene();
        pDevice->Present(NULL, NULL, NULL, NULL);
        RenderESPText();

        if (!FindWindowA(NULL, WINDOW_NAME)) { Log("game closed"); break; }

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    pDevice->Release();
    pD3D->Release();
    CloseHandle(hProcess);
    return 0;
}

int main() {
    return WinMain(GetModuleHandleA(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT);
}
