// pointer ammo hack for assaultcube
#include <windows.h>
#include <iostream>
#include <vector>
#include <TlHelp32.h>

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
        std::cout << "[-] Could not find game window." << std::endl;
        system("pause");
        return 1;
    }
    std::cout << "[+] Found Window!" << std::endl;

    DWORD pID = 0;
    GetWindowThreadProcessId(hWnd, &pID);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
    if (!hProcess) {
        std::cout << "[-] Could not open process. Run as Admin!" << std::endl;
        system("pause");
        return 1;
    }

    uintptr_t moduleBase = GetModuleBaseAddress(pID, L"ac_client.exe");
    std::cout << "[+] Base: 0x" << std::hex << moduleBase << std::endl;

    // Pointer base and offsets
    // Base pointer: ac_client.exe + 0x0017E0A8
    uintptr_t staticOffset = 0x0017E0A8;

    // Single offset to reach the ammo value
    std::vector<DWORD> offsets = { 0x140 };

    uintptr_t ptrAddress = moduleBase + staticOffset;

    int newValue = 9999;
    int currentValue = 0;

    std::cout << "[+] Pointer Hack Active... Press F1 to quit." << std::endl;

    while (true) {
        if (GetAsyncKeyState(VK_F1)) { break; }

        // Read the base pointer
        uintptr_t dynamicAddress = 0;
        ReadProcessMemory(hProcess, (LPVOID)ptrAddress, &dynamicAddress, sizeof(dynamicAddress), NULL);

        // Resolve the pointer (single offset)
        dynamicAddress += offsets[0];

        // Read current value and optionally write new value
        ReadProcessMemory(hProcess, (LPVOID)dynamicAddress, &currentValue, sizeof(currentValue), NULL);

        // Validate address and value
        if (dynamicAddress < 0x10000 || currentValue > 100000) {
            std::cout << "[-] Invalid address or value. Retrying...     \r" << std::flush;
            Sleep(100);
            continue;
        }

        if (currentValue != newValue) {
            WriteProcessMemory(hProcess, (LPVOID)dynamicAddress, &newValue, sizeof(newValue), NULL);
        }

        std::cout << "[*] Address: 0x" << std::hex << dynamicAddress
            << " | Ammo: " << std::dec << currentValue << "    \r" << std::flush;

        Sleep(10);
    }

    std::cout << "\n[+] Exiting..." << std::endl;
    CloseHandle(hProcess);
    return 0;
}