// dllmain.cpp
#include "pch.h"
#include "interfaces.hpp"
#include <windows.h>
#include <iostream>
#include <cstdint>

// Matches Source 2's Color struct layout (R, G, B, A)
struct Color
{
    uint8_t r, g, b, a;
};

// tier0.dll exports these functions globally — no vtable index needed
using ConColorMsg_t = void(__cdecl*)(const Color& clr, const char* pMsg, ...);
using Msg_t         = void(__cdecl*)(const char* pMsg, ...);

void SetupInGameConsole()
{
    // --- Debug window so we can track every step ---
    AllocConsole();
    FILE* stream = nullptr;
    freopen_s(&stream, "CONIN$",  "r", stdin);
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);
    SetConsoleTitleA("CS2 Hack Console");

    std::cout << "[*] DLL injected. Locating ICvar in tier0.dll...\n";

    // ICvar (VEngineCvar007) lives in tier0.dll, not client.dll
    HMODULE hTier0 = GetModuleHandleA("tier0.dll");
    if (!hTier0)
    {
        std::cout << "[-] tier0.dll not found! Is CS2 running?\n";
        std::cin.get();
        return;
    }
    std::cout << "[+] tier0.dll base: 0x" << std::hex << (uintptr_t)hTier0 << "\n";

    // --- Method 1: exported functions (simplest, most reliable) ---
    // ConColorMsg and Msg are exported directly from tier0.dll
    auto pfnConColorMsg = reinterpret_cast<ConColorMsg_t>(GetProcAddress(hTier0, "ConColorMsg"));
    auto pfnMsg         = reinterpret_cast<Msg_t>        (GetProcAddress(hTier0, "Msg"));

    if (pfnConColorMsg)
    {
        std::cout << "[+] ConColorMsg export @ 0x" << std::hex << (uintptr_t)pfnConColorMsg << "\n";

        Color white{ 255, 255, 255, 255 };
        Color cyan { 0,   255, 255, 255 };

        // These lines appear in the CS2 in-game console (press ~ / tilde to open it)
        pfnConColorMsg(white, "[HAX] Hello CS2 Console! DLL injected successfully.\n");
        pfnConColorMsg(cyan,  "[HAX] Open the console with ~ (tilde) to read this.\n");

        std::cout << "[+] Messages sent to the in-game console via ConColorMsg!\n";
    }
    else if (pfnMsg)
    {
        std::cout << "[+] Msg export @ 0x" << std::hex << (uintptr_t)pfnMsg << "\n";
        pfnMsg("[HAX] Hello CS2 Console! Open with ~ (tilde).\n");
        std::cout << "[+] Message sent via Msg!\n";
    }
    else
    {
        std::cout << "[-] Neither ConColorMsg nor Msg found as exports.\n";
    }

    // --- Method 2: Resolve ICvar* from interfaces.hpp offset (for reference) ---
    // VEngineCvar007 = 0x3A33B0 in tier0.dll
    // The offset points to a location in tier0.dll's data section that holds the ICvar pointer
    uintptr_t pCvarSlot = (uintptr_t)hTier0 + cs2_dumper::interfaces::tier0_dll::VEngineCvar007;
    void*     pCvar     = *reinterpret_cast<void**>(pCvarSlot);
    std::cout << "[*] ICvar slot  @ 0x" << std::hex << pCvarSlot << "\n";
    std::cout << "[*] ICvar*      = 0x" << std::hex << (uintptr_t)pCvar << "\n";

    std::cout << "\n[*] Press Enter to detach...\n";
    std::cin.get();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)SetupInGameConsole, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        FreeConsole();
        break;
    }
    return TRUE;
}

