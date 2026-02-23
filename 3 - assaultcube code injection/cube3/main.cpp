// Patches AssaultCube process memory to toggle unlimited ammo.
#include <windows.h>
#include <iostream>
#include <TlHelp32.h>

// Get the base address of a loaded module in the target process.
uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32 modEntry;
        modEntry.dwSize = sizeof(modEntry);
        if (Module32First(hSnap, &modEntry)) {
            do {
                if (!_wcsicmp(modEntry.szModule, modName)) {
                    modBaseAddr = reinterpret_cast<uintptr_t>(modEntry.modBaseAddr);
                    break;
                }
            } while (Module32Next(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
}

int main() {
    // Find the game window and process
    HWND hWnd = FindWindow(NULL, TEXT("AssaultCube"));
    if (!hWnd) {
        std::cout << "Game not found." << std::endl;
        return 1;
    }

    DWORD pID = 0;
    GetWindowThreadProcessId(hWnd, &pID);
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
    if (!hProcess) {
        std::cout << "Failed to open target process." << std::endl;
        return 1;
    }

    // Target module and offset (from external analysis)
    uintptr_t moduleBase = GetModuleBaseAddress(pID, L"ac_client.exe");
    uintptr_t codeOffset = 0x00C73EF; // ac_client.exe + 0xC73EF
    unsigned char nopCode[] = { 0x90, 0x90 }; // replace 2 bytes with NOPs

    uintptr_t codeAddress = moduleBase + codeOffset;

    std::cout << "Code Address: 0x" << std::hex << codeAddress << std::dec << std::endl;
    std::cout << "Press F1 to enable unlimited ammo, F2 to disable, END to exit." << std::endl;

    // Read and store original bytes so we can restore them later
    BYTE originalBytes[sizeof(nopCode)];
    if (!ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(codeAddress), originalBytes, sizeof(originalBytes), NULL)) {
        std::cout << "Failed to read target memory." << std::endl;
        CloseHandle(hProcess);
        return 1;
    }

    bool hackEnabled = false;

    while (true) {
        if (GetAsyncKeyState(VK_END) & 1) break;

        if (GetAsyncKeyState(VK_F1) & 1) {
            WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(codeAddress), nopCode, sizeof(nopCode), NULL);
            std::cout << "Unlimited Ammo: ON " << std::endl;
            hackEnabled = true;
        }

        if (GetAsyncKeyState(VK_F2) & 1) {
            WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(codeAddress), originalBytes, sizeof(originalBytes), NULL);
            std::cout << "Unlimited Ammo: OFF" << std::endl;
            hackEnabled = false;
        }

        Sleep(100);
    }

    // Restore original bytes if necessary and clean up
    if (hackEnabled) {
        WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(codeAddress), originalBytes, sizeof(originalBytes), NULL);
    }

    CloseHandle(hProcess);
    return 0;
}
