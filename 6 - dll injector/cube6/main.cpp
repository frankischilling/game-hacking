// dll injector
// features:
// - multiple injection methods: CreateRemoteThread, NtCreateThreadEx
// - process selection: by name, PID, or window title
// - architecture validation (32/64-bit mismatch prevention)
// - robust error handling and verbose logging
// - interactive and CLI modes
// - educational use only
// - requires elevated privileges for many targets

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include <filesystem>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

#ifndef _NTDEF_
typedef long NTSTATUS;
#define NTAPI __stdcall
#endif

namespace injector {

    // Error handling
    struct InjectResult {
        bool success = false;
        std::string message;
        DWORD lastError = 0;

        static InjectResult Ok() { return { true, "Injection successful.", 0 }; }
        static InjectResult Fail(const std::string& msg, DWORD err = ::GetLastError()) {
            return { false, msg, err };
        }
    };

    // Process enumeration
    struct ProcessInfo {
        DWORD pid;
        std::string name;
        std::string windowTitle;
        bool isWow64 = false;  // 32-bit on 64-bit OS
    };

    std::vector<ProcessInfo> EnumerateProcesses() {
        std::vector<ProcessInfo> out;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE)
            return out;

        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);

        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (pe.th32ProcessID == 0)
                    continue;

                ProcessInfo info{};
                info.pid = pe.th32ProcessID;

                // Narrow string from module name
                char nameBuf[MAX_PATH]{};
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, nameBuf, sizeof(nameBuf), nullptr, nullptr);
                info.name = nameBuf;

                // Try to get window title
                struct EnumData { DWORD pid; std::string title; };
                EnumData ed{ pe.th32ProcessID, {} };

                EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
                    auto* ed = reinterpret_cast<EnumData*>(lp);
                    DWORD wndPid = 0;
                    GetWindowThreadProcessId(hwnd, &wndPid);
                    if (wndPid == ed->pid && IsWindowVisible(hwnd)) {
                        wchar_t buf[256]{};
                        if (GetWindowTextW(hwnd, buf, 256) > 0) {
                            char narrow[256]{};
                            WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
                            ed->title = narrow;
                            return FALSE;  // stop enumeration
                        }
                    }
                    return TRUE;
                    }, reinterpret_cast<LPARAM>(&ed));
                info.windowTitle = ed.title;

                // Check architecture
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    BOOL wow64 = FALSE;
                    if (IsWow64Process(hProc, &wow64))
                        info.isWow64 = (wow64 == TRUE);
                    CloseHandle(hProc);
                }

                out.push_back(info);
            } while (Process32NextW(hSnap, &pe));
        }

        CloseHandle(hSnap);
        return out;
    }

    std::optional<ProcessInfo> GetProcessById(DWORD pid) {
        auto list = EnumerateProcesses();
        for (auto& p : list) {
            if (p.pid == pid)
                return p;
        }
        return std::nullopt;
    }

    std::optional<ProcessInfo> GetProcessByName(const std::string& name) {
        auto list = EnumerateProcesses();
        std::string lowerName = name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        for (auto& p : list) {
            std::string pLower = p.name;
            std::transform(pLower.begin(), pLower.end(), pLower.begin(), ::tolower);
            if (pLower == lowerName || pLower.find(lowerName) != std::string::npos)
                return p;
        }
        return std::nullopt;
    }

    DWORD GetProcessIdByWindowTitle(const std::string& title) {
        struct SearchData {
            std::string title;
            DWORD pid = 0;
        } sd;
        sd.title = title;
        std::string lowerTitle = title;
        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);

        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            auto* sd = reinterpret_cast<SearchData*>(lp);
            wchar_t buf[512]{};
            if (GetWindowTextW(hwnd, buf, 512) == 0)
                return TRUE;
            char narrow[512]{};
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, sizeof(narrow), nullptr, nullptr);
            std::string wndTitle = narrow;
            std::transform(wndTitle.begin(), wndTitle.end(), wndTitle.begin(), ::tolower);
            if (wndTitle.find(sd->title) != std::string::npos) {
                GetWindowThreadProcessId(hwnd, &sd->pid);
                return FALSE;
            }
            return TRUE;
            }, reinterpret_cast<LPARAM>(&sd));
        return sd.pid;
    }

    // Architecture validation
    bool IsCurrentProcess64Bit() {
#ifdef _WIN64
        return true;
#else
        BOOL wow64 = FALSE;
        IsWow64Process(GetCurrentProcess(), &wow64);
        return (wow64 == FALSE);  // 32-bit native
#endif
    }

    bool IsTargetProcess64Bit(DWORD pid) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return false;
        BOOL wow64 = FALSE;
        bool ok = IsWow64Process(h, &wow64);
        CloseHandle(h);
        if (!ok) return false;
        // wow64=TRUE means 32-bit process on 64-bit OS
        // wow64=FALSE on 64-bit process means 64-bit; on 32-bit OS it's 32-bit
#ifdef _WIN64
        return (wow64 == FALSE);  // 64-bit target
#else
        return false;  // we're 32-bit, target is 32-bit or WoW64
#endif
    }

    // CreateRemoteThread + LoadLibrary
    InjectResult InjectViaCreateRemoteThread(HANDLE hProcess, const std::wstring& dllPath) {
        size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);

        LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remoteMem)
            return InjectResult::Fail("VirtualAllocEx failed", ::GetLastError());

        if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), pathBytes, nullptr)) {
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return InjectResult::Fail("WriteProcessMemory failed", ::GetLastError());
        }

        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!hKernel32)
            return InjectResult::Fail("GetModuleHandle(kernel32) failed", ::GetLastError());

        FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
        if (!pLoadLibrary)
            return InjectResult::Fail("GetProcAddress(LoadLibraryW) failed", ::GetLastError());

        HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(pLoadLibrary), remoteMem, 0, nullptr);
        if (!hThread) {
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return InjectResult::Fail("CreateRemoteThread failed", ::GetLastError());
        }

        WaitForSingleObject(hThread, 10000);
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        CloseHandle(hThread);
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

        if (exitCode == 0)
            return InjectResult::Fail("LoadLibrary returned NULL (DLL may have failed to load)", 0);

        return InjectResult::Ok();
    }

    // Injection: NtCreateThreadEx (alternative, bypasses some hooks)
    using NtCreateThreadEx_t = NTSTATUS(NTAPI*)(
        PHANDLE ThreadHandle, ACCESS_MASK DesiredAccess,
        PVOID ObjectAttributes, HANDLE ProcessHandle,
        PVOID StartRoutine, PVOID Argument,
        ULONG CreateFlags, SIZE_T ZeroBits, SIZE_T StackSize,
        SIZE_T MaximumStackSize, PVOID AttributeList);

#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED 0x1

    InjectResult InjectViaNtCreateThreadEx(HANDLE hProcess, const std::wstring& dllPath) {
        size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);

        LPVOID remoteMem = VirtualAllocEx(hProcess, nullptr, pathBytes,
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remoteMem)
            return InjectResult::Fail("VirtualAllocEx failed", ::GetLastError());

        if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), pathBytes, nullptr)) {
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return InjectResult::Fail("WriteProcessMemory failed", ::GetLastError());
        }

        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll)
            return InjectResult::Fail("GetModuleHandle(ntdll) failed", ::GetLastError());

        auto NtCreateThreadEx = reinterpret_cast<NtCreateThreadEx_t>(
            GetProcAddress(hNtdll, "NtCreateThreadEx"));
        if (!NtCreateThreadEx)
            return InjectResult::Fail("GetProcAddress(NtCreateThreadEx) failed", ::GetLastError());

        HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
        FARPROC pLoadLibrary = GetProcAddress(hKernel32, "LoadLibraryW");
        if (!pLoadLibrary) {
            VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
            return InjectResult::Fail("GetProcAddress(LoadLibraryW) failed", ::GetLastError());
        }

        HANDLE hThread = nullptr;
        NTSTATUS status = NtCreateThreadEx(&hThread,
            0x1FFFFF,
            nullptr, hProcess,
            pLoadLibrary, remoteMem,
            0, 0, 0, 0, nullptr);

        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);

        if (!NT_SUCCESS(status) || !hThread)
            return InjectResult::Fail("NtCreateThreadEx failed (0x" + std::to_string(status) + ")", 0);

        WaitForSingleObject(hThread, 10000);
        CloseHandle(hThread);
        return InjectResult::Ok();
    }

    // Main injection entry
    enum class InjectMethod { CreateRemoteThread, NtCreateThreadEx };

    InjectResult Inject(DWORD pid, const std::wstring& dllPath, InjectMethod method) {
        HANDLE hProcess = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
            FALSE, pid);
        if (!hProcess)
            return InjectResult::Fail("OpenProcess failed (ensure running as Admin?)", ::GetLastError());

        InjectResult result;
        if (method == InjectMethod::NtCreateThreadEx)
            result = InjectViaNtCreateThreadEx(hProcess, dllPath);
        else
            result = InjectViaCreateRemoteThread(hProcess, dllPath);

        CloseHandle(hProcess);
        return result;
    }

}

// Console UI and main
void PrintBanner() {
    std::cout << R"(DLL Injector)";
}

void PrintUsage(const char* prog) {
    std::cout << "\nUsage:\n"
        << "  " << prog << " -p <PID> -d <dll_path> [-m crt|nt]   Inject by process ID\n"
        << "  " << prog << " -n <process.exe> -d <dll_path>        Inject by process name\n"
        << "  " << prog << " -w <window_title> -d <dll_path>       Inject by window title\n"
        << "  " << prog << " -l                                  List running processes\n"
        << "  " << prog << "                                     Interactive mode (no args)\n\n"
        << "  -m crt  CreateRemoteThread (default)\n"
        << "  -m nt   NtCreateThreadEx\n\n";
}

void ListProcesses() {
    auto procs = injector::EnumerateProcesses();
    std::cout << "\n PID      | Name                    | Window Title\n";
    std::cout << "----------+-------------------------+--------------------------------\n";
    for (const auto& p : procs) {
        printf(" %-8lu | %-23s | %s\n",
            p.pid,
            p.name.size() > 23 ? (p.name.substr(0, 20) + "...").c_str() : p.name.c_str(),
            p.windowTitle.empty() ? "(none)" : p.windowTitle.c_str());
    }
    std::cout << "\nTotal: " << procs.size() << " processes\n";
}

int InteractiveMode() {
    while (true) {
        std::cout << "\n[1] List processes  [2] Inject  [3] Exit\n> ";
        int choice = 0;
        std::cin >> choice;
        if (std::cin.fail()) { std::cin.clear(); std::cin.ignore(10000, '\n'); continue; }

        if (choice == 1) {
            ListProcesses();
            continue;
        }
        if (choice == 3)
            return 0;

        if (choice != 2) continue;

        std::cout << "Process (name or PID): ";
        std::string procInput;
        std::cin >> std::ws;
        std::getline(std::cin, procInput);
        if (procInput.empty()) continue;

        std::cout << "DLL path: ";
        std::string dllInput;
        std::getline(std::cin, dllInput);
        if (dllInput.empty()) continue;

        DWORD pid = 0;
        if (procInput.find_first_not_of("0123456789") == std::string::npos)
            pid = static_cast<DWORD>(std::stoul(procInput));
        else {
            auto p = injector::GetProcessByName(procInput);
            if (!p) {
                std::cout << "Process not found: " << procInput << "\n";
                continue;
            }
            pid = p->pid;
        }

        std::filesystem::path dllPath(dllInput);
        if (!std::filesystem::exists(dllPath)) {
            std::cout << "DLL file not found: " << dllInput << "\n";
            continue;
        }
        dllPath = std::filesystem::absolute(dllPath);

        auto targetInfo = injector::GetProcessById(pid);
        if (targetInfo) {
            bool target64 = injector::IsTargetProcess64Bit(pid);
            bool self64 = injector::IsCurrentProcess64Bit();
            if (target64 != self64) {
                std::cout << "Architecture mismatch: target is " << (target64 ? "64" : "32")
                    << "-bit, injector is " << (self64 ? "64" : "32") << "-bit.\n";
                std::cout << "  Build cube6 as " << (target64 ? "x64" : "Win32/x86") << " to inject into " << (target64 ? "64" : "32") << "-bit processes.\n";
                continue;
            }
        }

        std::wstring dllPathW = dllPath.wstring();
        auto result = injector::Inject(pid, dllPathW, injector::InjectMethod::CreateRemoteThread);

        if (result.success)
            std::cout << "Injection successful.\n";
        else {
            std::cout << "Injection failed: " << result.message;
            if (result.lastError)
                std::cout << " (GetLastError=" << result.lastError << ")";
            std::cout << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    PrintBanner();

    if (argc == 1)
        return InteractiveMode();

    std::string procSpec, dllPath;
    injector::InjectMethod method = injector::InjectMethod::CreateRemoteThread;
    bool listOnly = false;
    bool byWindow = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-p" && i + 1 < argc) { procSpec = argv[++i]; }
        else if (arg == "-n" && i + 1 < argc) { procSpec = argv[++i]; }
        else if (arg == "-w" && i + 1 < argc) { procSpec = argv[++i]; byWindow = true; }
        else if (arg == "-d" && i + 1 < argc) { dllPath = argv[++i]; }
        else if (arg == "-m" && i + 1 < argc) {
            std::string m = argv[++i];
            if (m == "nt") method = injector::InjectMethod::NtCreateThreadEx;
        }
        else if (arg == "-l") { listOnly = true; }
        else if (arg == "-h" || arg == "--help") { PrintUsage(argv[0]); return 0; }
    }

    if (listOnly) {
        ListProcesses();
        return 0;
    }

    if (procSpec.empty() || dllPath.empty()) {
        std::cerr << "Error: -p/-n/-w and -d are required. Use -h for help.\n";
        return 1;
    }

    DWORD pid = 0;
    if (byWindow)
        pid = injector::GetProcessIdByWindowTitle(procSpec);
    else if (procSpec.find_first_not_of("0123456789") == std::string::npos)
        pid = static_cast<DWORD>(std::stoul(procSpec));
    else {
        auto p = injector::GetProcessByName(procSpec);
        if (!p) {
            std::cerr << "Process not found: " << procSpec << "\n";
            return 1;
        }
        pid = p->pid;
    }

    if (pid == 0) {
        std::cerr << "Process not found.\n";
        return 1;
    }

    std::filesystem::path pathObj(dllPath);
    if (!std::filesystem::exists(pathObj)) {
        std::cerr << "DLL not found: " << dllPath << "\n";
        return 1;
    }
    std::wstring dllPathW = std::filesystem::absolute(pathObj).wstring();

    bool target64 = injector::IsTargetProcess64Bit(pid);
    bool self64 = injector::IsCurrentProcess64Bit();
    if (target64 != self64) {
        std::cerr << "Architecture mismatch: target is " << (target64 ? "64" : "32") << "-bit, injector is " << (self64 ? "64" : "32") << "-bit.\n";
        std::cerr << "  Build cube6 as Win32/x86 for 32-bit targets (e.g. Assault Cube).\n";
        return 1;
    }

    auto result = injector::Inject(pid, dllPathW, method);
    if (result.success) {
        std::cout << "Injection successful.\n";
        return 0;
    }
    std::cerr << "Injection failed: " << result.message;
    if (result.lastError) std::cerr << " (GetLastError=" << result.lastError << ")";
    std::cerr << "\n";
    return 1;
}
