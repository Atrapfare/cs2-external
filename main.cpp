#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <winternl.h>
#include "offsets.hpp" // Include the generated offsets

// This project is for educational purposes only, to demonstrate the concept of handle hijacking.

// Define constants for NTAPI functions, as they are not in standard headers.
#define NT_SUCCESS(x) ((x) >= 0)
#define STATUS_INFO_LENGTH_MISMATCH 0xC0000004

// Structures for NtQuerySystemInformation
typedef struct _SYSTEM_HANDLE {
    ULONG ProcessId;
    BYTE ObjectTypeNumber;
    BYTE Flags;
    USHORT Handle;
    PVOID Object;
    ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, *PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
    ULONG HandleCount;
    SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, *PSYSTEM_HANDLE_INFORMATION;

// Function pointer for NtQuerySystemInformation
typedef NTSTATUS(WINAPI *pfnNtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

// Function to get the Process ID (PID) from a process name
DWORD GetProcessIdByName(const wchar_t* processName) {
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (wcscmp(entry.szExeFile, processName) == 0) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return 0;
}

// Function to get the base address of a module in a process
uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W modEntry;
        modEntry.dwSize = sizeof(modEntry);
        if (Module32FirstW(hSnap, &modEntry)) {
            do {
                if (wcscmp(modEntry.szModule, modName) == 0) {
                    modBaseAddr = (uintptr_t)modEntry.modBaseAddr;
                    break;
                }
            } while (Module32NextW(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
}

// The core function to perform handle hijacking
HANDLE HijackExistingHandle(DWORD targetPid) {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) {
        std::cerr << "[-] Failed to get handle for ntdll.dll" << std::endl;
        return NULL;
    }

    pfnNtQuerySystemInformation NtQuerySystemInformation = (pfnNtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    if (!NtQuerySystemInformation) {
        std::cerr << "[-] Failed to get address of NtQuerySystemInformation" << std::endl;
        return NULL;
    }

    ULONG bufferSize = 0;
    // Call with null buffer to get the required size
    NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(16), NULL, 0, &bufferSize);

    if (bufferSize == 0) {
        std::cerr << "[-] NtQuerySystemInformation failed to return buffer size." << std::endl;
        return NULL;
    }

    PSYSTEM_HANDLE_INFORMATION handleInfo = (PSYSTEM_HANDLE_INFORMATION)malloc(bufferSize);
    if (!handleInfo) {
        std::cerr << "[-] Failed to allocate memory for handle information." << std::endl;
        return NULL;
    }

    NTSTATUS status = NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(16), handleInfo, bufferSize, NULL);
    if (!NT_SUCCESS(status)) {
        std::cerr << "[-] NtQuerySystemInformation failed with status (2): " << std::hex << status << std::endl;
        free(handleInfo);
        return NULL;
    }

    std::cout << "[+] Found " << handleInfo->HandleCount << " system handles." << std::endl;

    for (ULONG i = 0; i < handleInfo->HandleCount; i++) {
        SYSTEM_HANDLE handle = handleInfo->Handles[i];

        if (handle.ProcessId == GetCurrentProcessId() || handle.ProcessId == targetPid) {
            continue;
        }

        if ((handle.GrantedAccess & PROCESS_VM_READ) != PROCESS_VM_READ) {
            continue;
        }

        HANDLE hSourceProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, handle.ProcessId);
        if (!hSourceProcess) {
            continue;
        }

        HANDLE hDup = NULL;
        status = DuplicateHandle(hSourceProcess, (HANDLE)handle.Handle, GetCurrentProcess(), &hDup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        CloseHandle(hSourceProcess);

        if (!NT_SUCCESS(status)) {
            continue;
        }

        if (GetProcessId(hDup) == targetPid) {
            std::cout << "[+] Success! Hijacked a handle to CS2 (PID: " << targetPid << ") from process with PID: " << handle.ProcessId << std::endl;
            std::cout << "[+] Handle value: 0x" << std::hex << (DWORD_PTR)hDup << std::endl;
            free(handleInfo);
            return hDup;
        }

        CloseHandle(hDup);
    }

    std::cerr << "[-] Could not find a suitable handle to hijack." << std::endl;
    free(handleInfo);
    return NULL;
}

int main() {
    std::cout << "--- CS2 Educational Handle Hijacking Tool ---" << std::endl;

    // 1. Find the Process ID of CS2
    const wchar_t* processName = L"cs2.exe";
    DWORD pid = GetProcessIdByName(processName);

    if (pid == 0) {
        std::cerr << "[-] Could not find process: " << processName << ". Is the game running?" << std::endl;
        system("pause");
        return 1;
    }
    std::cout << "[+] Found CS2 process with PID: " << pid << std::endl;

    // 2. Find and hijack a handle to the CS2 process
    HANDLE hCs2 = HijackExistingHandle(pid);

    if (!hCs2) {
        std::cerr << "[-] Failed to get a handle to CS2 via hijacking." << std::endl;
        system("pause");
        return 1;
    }

    // 3. Get the base address of client.dll
    const wchar_t* clientDllName = L"client.dll";
    uintptr_t clientBase = GetModuleBaseAddress(pid, clientDllName);
    if (clientBase == 0) {
        std::cerr << "[-] Could not find " << clientDllName << " in the process." << std::endl;
        CloseHandle(hCs2);
        system("pause");
        return 1;
    }
    std::cout << "[+] Found " << clientDllName << " at address: 0x" << std::hex << clientBase << std::endl;

    // 4. Use the hijacked handle and offsets to read memory
    // Calculate the address of the entity list
    uintptr_t entityListAddr = clientBase + cs2_dumper::offsets::client_dll::dwEntityList;
    std::cout << "[*] Calculated EntityList address: 0x" << std::hex << entityListAddr << std::endl;

    std::cout << "[*] Attempting to read from EntityList address using the hijacked handle..." << std::endl;

    // We'll try to read the first pointer in the entity list.
    uintptr_t entityListContent = 0;
    SIZE_T bytesRead = 0;

    BOOL success = ReadProcessMemory(hCs2, (LPCVOID)entityListAddr, &entityListContent, sizeof(entityListContent), &bytesRead);

    if (success && bytesRead > 0) {
        std::cout << "[+] Successfully read " << bytesRead << " bytes from EntityList." << std::endl;
        std::cout << "[+] Value read (pointer to first entity?): 0x" << std::hex << entityListContent << std::endl;
    } else {
        std::cerr << "[-] Failed to read memory from EntityList address." << std::endl;
        std::cerr << "[-] GetLastError(): " << GetLastError() << std::endl;
    }

    std::cout << "[*] The main purpose (demonstrating handle hijacking and memory reading with offsets) is complete." << std::endl;

    // 5. Clean up
    CloseHandle(hCs2);
    std::cout << "[+] Closed hijacked handle. Program finished." << std::endl;

    system("pause");
    return 0;
}
