// hello world of game hacking
#include <windows.h>
#include <iostream>

int main() {
    // Find the game window by title ("AssaultCube" or "Unchanged").
    HWND hWnd = FindWindow(NULL, TEXT("AssaultCube"));

    if (!hWnd) {
        std::cout << "[-] Could not find game window. Is it open?" << std::endl;
        system("pause");
        return 1;
    }
    std::cout << "[+] Found Window!" << std::endl;

    // Get the process ID and open the process with the required access.
    DWORD pID = 0;
    GetWindowThreadProcessId(hWnd, &pID);
    std::cout << "[+] Process ID: " << pID << std::endl;

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pID);
    if (!hProcess) {
        std::cout << "[-] Could not open process. Run as Administrator!" << std::endl;
        system("pause");
        return 1;
    }
    std::cout << "[+] Process opened successfully." << std::endl;

    // Address of the ammo value (replace with the address from your memory tool).
    DWORD ammoAddress = 0x008ADE70; // change as needed

    int newValue = 9999;
    int currentValue = 0;

    std::cout << "[+] Hacking... Press F1 to quit." << std::endl;

    // Main loop: read current ammo and overwrite it with the desired value.
    while (true) {
        if (GetAsyncKeyState(VK_F1)) { break; }

        ReadProcessMemory(hProcess, (LPVOID)ammoAddress, &currentValue, sizeof(currentValue), NULL);
        WriteProcessMemory(hProcess, (LPVOID)ammoAddress, &newValue, sizeof(newValue), NULL);

        std::cout << "[*] Current Ammo: " << currentValue << "\r" << std::flush;

        Sleep(100);
    }

    std::cout << "\n[+] Exiting..." << std::endl;
    CloseHandle(hProcess);
    return 0;
}
