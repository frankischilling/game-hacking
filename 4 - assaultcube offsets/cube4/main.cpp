#include <windows.h>
#include <iostream>
#include <TlHelp32.h>

// Helper: find module base address using a snapshot of the process modules
uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 modEntry;
        modEntry.dwSize = sizeof(modEntry);
        if (Module32First(hSnap, &modEntry)) {
            do {
                if (!_wcsicmp(modEntry.szModule, modName)) {
                    modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
                    break;
                }
            } while (Module32Next(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
}

int main() {
    HWND hWnd = FindWindow(NULL, TEXT("AssaultCube"));
    if (!hWnd) {
        std::cout << "[-] Game not found." << std::endl;
        system("pause");
        return 1;
    }

    DWORD pID = 0;
    GetWindowThreadProcessId(hWnd, &pID);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);

    uintptr_t moduleBase = GetModuleBaseAddress(pID, L"ac_client.exe");

    // ------------------------------------------------------------------
    // Updated offsets for AssaultCube v1.3.0.2
    // ------------------------------------------------------------------

    // Base pointer (same one used for Ammo)
    uintptr_t localPlayerPtrOffset = 0x0017E0A8;

    // Health offset (updated)
    uintptr_t offsetHealth = 0xEC; // was 0xF8 in older versions

    // Player position offsets (X, Y, Z)
    uintptr_t offsetX = 0x2C;
    uintptr_t offsetY = 0x30;
    uintptr_t offsetZ = 0x28;

    // ------------------------------------------------------------------

    std::cout << "[+] AssaultCube v1.3.0.2 Trainer Active" << std::endl;
    std::cout << "[+] Press F1 to set Health to 9999" << std::endl;
    std::cout << "[+] Press END to quit" << std::endl;

    while (true) {
        if (GetAsyncKeyState(VK_END)) break;

        // 1. Read the Local Player Pointer
        uintptr_t localPlayerAddr = 0;
        ReadProcessMemory(hProcess, (LPVOID)(moduleBase + localPlayerPtrOffset), &localPlayerAddr, sizeof(localPlayerAddr), NULL);

        if (localPlayerAddr != 0) {

            // 2. Read Position (Floats)
            float posX = 0, posY = 0, posZ = 0;
            ReadProcessMemory(hProcess, (LPVOID)(localPlayerAddr + offsetX), &posX, sizeof(posX), NULL);
            ReadProcessMemory(hProcess, (LPVOID)(localPlayerAddr + offsetY), &posY, sizeof(posY), NULL);
            ReadProcessMemory(hProcess, (LPVOID)(localPlayerAddr + offsetZ), &posZ, sizeof(posZ), NULL);

            // 3. Read Health (Integer)
            int health = 0;
            ReadProcessMemory(hProcess, (LPVOID)(localPlayerAddr + offsetHealth), &health, sizeof(health), NULL);

            // 4. Print Status
            std::cout << "[*] Pos: " << posX << ", " << posY << ", " << posZ
                << " | Health: " << health << "     \r" << std::flush;

            // 5. Hack: Infinite Health (Press F1)
            if (GetAsyncKeyState(VK_F1) & 1) {
                int godMode = 9999;
                WriteProcessMemory(hProcess, (LPVOID)(localPlayerAddr + offsetHealth), &godMode, sizeof(godMode), NULL);
                std::cout << "[+] Health set to 9999!                                  " << std::endl;
            }
        }

        Sleep(100);
    }

    CloseHandle(hProcess);
    return 0;
}