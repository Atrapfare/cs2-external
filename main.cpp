/*
This is a stand alone bypass made by Apxaey. Feel free to use this in your cheats but credit me for the bypass as i put alot of time into this.
If you have some brain cells you will be able to incorporate this into your cheats and remain undetected by user-mode anticheats.
Obviously standard cheat 'recommendations' still apply:
1.) Use self-written or not signatured code
2.) Dont write impossible values
3.) If your going internal use a manual map injector

If you follow the guidelines above and use this bypass you will be safe from usermode anticheats like VAC.
Obviously you can build and adapt upon my code to suit your needs.
If I was to make a cheat for myself i would put this bypass into something i call an 'external internal' cheat.
Whereby you make a cheat and inject into a legitimate program like discord and add a check to the this bypass to only hijack a handle from the process you inject into, giving the appearence that nothing is out of the ordinary
However you can implement this bypass into any form of cheat, its your decision.
If you need want some more info i recommend you watch my YT video on this bypass.
Anyways if you want to see more of my stuff feel free to join my discord server https://discord.gg/GVyENvk. Here's my YT as well https://www.youtube.com/channel/UCPN6OOLxn1OaBP5jPThIiog.
*/
#include <iostream>
#include <Windows.h>
#include <TlHelp32.h>
#include <vector>
#include <winternl.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <io.h>
#include <string>

#include "offsets.hpp"
#include "client_dll.hpp"

// ----------------------------------------------------------------
// NTAPI definitions
// ----------------------------------------------------------------

#define SeDebugPriv 20
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004)
#define NtCurrentProcess ( (HANDLE)(LONG_PTR) -1 )
#define ProcessHandleType 0x7
#define SystemHandleInformation 16



typedef struct _MY_UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWCH   Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef struct _MY_OBJECT_ATTRIBUTES {
    ULONG           Length;
    HANDLE          RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG           Attributes;
    PVOID           SecurityDescriptor;
    PVOID           SecurityQualityOfService;
}  OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;

typedef struct _MY_CLIENT_ID {
    PVOID UniqueProcess;
    PVOID UniqueThread;
} CLIENT_ID, * PCLIENT_ID;

typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO {
    ULONG ProcessId;
    BYTE ObjectTypeNumber;
    BYTE Flags;
    USHORT Handle;
    PVOID Object;
    ACCESS_MASK GrantedAccess;
} SYSTEM_HANDLE, * PSYSTEM_HANDLE;

typedef struct _SYSTEM_HANDLE_INFORMATION {
    ULONG HandleCount;
    SYSTEM_HANDLE Handles[1];
} SYSTEM_HANDLE_INFORMATION, * PSYSTEM_HANDLE_INFORMATION;

typedef NTSTATUS(NTAPI* _NtDuplicateObject)(
    HANDLE SourceProcessHandle,
    HANDLE SourceHandle,
    HANDLE TargetProcessHandle,
    PHANDLE TargetHandle,
    ACCESS_MASK DesiredAccess,
    ULONG Attributes,
    ULONG Options
);

typedef NTSTATUS(NTAPI* _RtlAdjustPrivilege)(
    ULONG Privilege,
    BOOLEAN Enable,
    BOOLEAN CurrentThread,
    PBOOLEAN Enabled
);

typedef NTSYSAPI NTSTATUS(NTAPI* _NtOpenProcess)(
    PHANDLE            ProcessHandle,
    ACCESS_MASK        DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PCLIENT_ID         ClientId
);

typedef NTSTATUS(NTAPI* _NtQuerySystemInformation)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

// ----------------------------------------------------------------
// Helper and Core Functions
// ----------------------------------------------------------------

OBJECT_ATTRIBUTES InitObjectAttributes(PUNICODE_STRING name, ULONG attributes, HANDLE hRoot, PSECURITY_DESCRIPTOR security) {
    OBJECT_ATTRIBUTES object;
    object.Length = sizeof(OBJECT_ATTRIBUTES);
    object.ObjectName = name;
    object.Attributes = attributes;
    object.RootDirectory = hRoot;
    object.SecurityDescriptor = security;
    object.SecurityQualityOfService = NULL;
    return object;
}

bool IsHandleValid(HANDLE handle) {
    return handle && handle != INVALID_HANDLE_VALUE;
}

// A corrected function to find a process ID by its name, using wide characters.
DWORD GetProcessIdByName(const wchar_t* processName) {
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (!IsHandleValid(snapshot)) {
        std::wcerr << L"[-] CreateToolhelp32Snapshot failed." << std::endl;
        return 0;
    }

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

// The refactored and corrected handle hijacking function.
HANDLE HijackExistingHandle(DWORD dwTargetProcessId) {
    HMODULE ntdll = GetModuleHandleA("ntdll");
    if (!ntdll) {
        std::wcerr << L"[-] Failed to get handle for ntdll.dll" << std::endl;
        return NULL;
    }

    // Get NTAPI function pointers
    auto RtlAdjustPrivilege = (_RtlAdjustPrivilege)GetProcAddress(ntdll, "RtlAdjustPrivilege");
    auto NtQuerySystemInformation = (_NtQuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation");
    auto NtDuplicateObject = (_NtDuplicateObject)GetProcAddress(ntdll, "NtDuplicateObject");
    auto NtOpenProcess = (_NtOpenProcess)GetProcAddress(ntdll, "NtOpenProcess");

    if (!RtlAdjustPrivilege || !NtQuerySystemInformation || !NtDuplicateObject || !NtOpenProcess) {
        std::wcerr << L"[-] Failed to get address of one or more NTAPI functions" << std::endl;
        return NULL;
    }

    // Enable debug privilege
    boolean oldPriv;
    RtlAdjustPrivilege(SeDebugPriv, TRUE, FALSE, &oldPriv);

    // Query all system handles
    NTSTATUS ntRet;
    ULONG size = sizeof(SYSTEM_HANDLE_INFORMATION);
    SYSTEM_HANDLE_INFORMATION* hInfo = (SYSTEM_HANDLE_INFORMATION*)new(std::nothrow) byte[size];

    if (!hInfo) {
        std::wcerr << L"[-] Initial memory allocation failed for handle information." << std::endl;
        return NULL;
    }

    while ((ntRet = NtQuerySystemInformation((SYSTEM_INFORMATION_CLASS)SystemHandleInformation, hInfo, size, &size)) == STATUS_INFO_LENGTH_MISMATCH) {
        delete[] hInfo;
        hInfo = (PSYSTEM_HANDLE_INFORMATION)new(std::nothrow) byte[size];
        if (!hInfo) {
            std::wcerr << L"[-] Bad Heap Allocation while querying system handles." << std::endl;
            return NULL;
        }
    }

    if (!NT_SUCCESS(ntRet)) {
        std::wcerr << L"[-] NtQuerySystemInformation failed with status: " << std::hex << ntRet << std::endl;
        delete[] hInfo;
        return NULL;
    }

    // Iterate through handles to find a suitable one
    HANDLE procHandle = NULL;
    HANDLE hijackedHandle = NULL;
    OBJECT_ATTRIBUTES obj_Attribute = InitObjectAttributes(NULL, 0, NULL, NULL);
    CLIENT_ID clientID = { 0 };

    for (unsigned int i = 0; i < hInfo->HandleCount; ++i) {
        if (!IsHandleValid((HANDLE)hInfo->Handles[i].Handle) || hInfo->Handles[i].ObjectTypeNumber != ProcessHandleType) {
            continue;
        }

        // We don't need to open our own process
        if (hInfo->Handles[i].ProcessId == GetCurrentProcessId()) {
            continue;
        }

        // Correctly set the client ID to the process ID of the handle's owner
        clientID.UniqueProcess = (PVOID)(ULONG_PTR)hInfo->Handles[i].ProcessId;

        if (IsHandleValid(procHandle)) {
            CloseHandle(procHandle);
        }

        ntRet = NtOpenProcess(&procHandle, PROCESS_DUP_HANDLE, &obj_Attribute, &clientID);
        if (!IsHandleValid(procHandle) || !NT_SUCCESS(ntRet)) {
            continue;
        }

        ntRet = NtDuplicateObject(procHandle, (HANDLE)hInfo->Handles[i].Handle, NtCurrentProcess, &hijackedHandle, PROCESS_ALL_ACCESS, 0, 0);
        if (!IsHandleValid(hijackedHandle) || !NT_SUCCESS(ntRet)) {
            continue;
        }

        if (GetProcessId(hijackedHandle) == dwTargetProcessId) {
            std::wcout << L"[+] Success! Hijacked a handle to process " << dwTargetProcessId << " from process " << hInfo->Handles[i].ProcessId << std::endl;
            // Found the handle, clean up and return it
            CloseHandle(procHandle);
            delete[] hInfo;
            return hijackedHandle;
        }

        CloseHandle(hijackedHandle);
    }

    // Cleanup if no handle was found
    if (IsHandleValid(procHandle)) CloseHandle(procHandle);
    delete[] hInfo;
    std::wcerr << L"[-] Could not find a suitable handle to hijack." << std::endl;
    return NULL;
}


// ----------------------------------------------------------------
// Original code from main.cpp
// ----------------------------------------------------------------

struct Vector3 {
    float x, y, z;
};

namespace game_offsets {
    constexpr std::ptrdiff_t m_iHealth = 0x34C;
    constexpr std::ptrdiff_t m_pGameSceneNode = 0x330;
    constexpr std::ptrdiff_t m_vecAbsOrigin = 0xD0;
}

uintptr_t GetModuleBaseAddress(DWORD procId, const wchar_t* modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (IsHandleValid(hSnap)) {
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

template <typename T>
T Read(HANDLE hProcess, uintptr_t address) {
    T value;
    ReadProcessMemory(hProcess, (LPCVOID)address, &value, sizeof(T), NULL);
    return value;
}

int main() {
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    std::wcout << L"--- CS2 Player Information Tool ---" << std::endl;
    std::wcout << L"[*] Searching for CS2 process..." << std::endl;

    const wchar_t* processName = L"cs2.exe";
    DWORD pid = GetProcessIdByName(processName);

    if (pid == 0) {
        std::wcerr << L"[-] Could not find process: " << processName << L". Is the game running?" << std::endl;
        return 1;
    }
    std::wcout << L"[+] Found CS2 process with PID: " << pid << std::endl;

    std::wcout << L"[*] Attempting to hijack a handle to CS2..." << std::endl;
    HANDLE hCs2 = HijackExistingHandle(pid);

    if (!hCs2) {
        std::wcerr << L"[-] Failed to get a handle to CS2 via hijacking." << std::endl;
        return 1;
    }

    std::wcout << L"[*] Searching for client.dll module..." << std::endl;
    const wchar_t* clientDllName = L"client.dll";
    uintptr_t clientBase = GetModuleBaseAddress(pid, clientDllName);
    if (clientBase == 0) {
        std::wcerr << L"[-] Could not find " << clientDllName << L" in the process." << std::endl;
        CloseHandle(hCs2);
        return 1;
    }
    std::wcout << L"[+] Found " << clientDllName << L" at address: 0x" << std::hex << clientBase << std::endl;

    uintptr_t entityListAddr = clientBase + cs2_dumper::offsets::client_dll::dwEntityList;
    std::wcout << L"[*] EntityList address: 0x" << std::hex << entityListAddr << std::endl;

    std::wcout << L"\n--- Starting Player Data Loop ---" << std::endl;

    while (true) {
        system("cls");
        std::wcout << L"--- Player Information ---" << std::endl;
        std::wcout << L"Name\t\tHealth\t\tPosition (X, Y, Z)" << std::endl;
        std::wcout << L"--------------------------------------------------------" << std::endl;

        for (int i = 1; i <= 64; ++i) {
            uintptr_t listEntry = Read<uintptr_t>(hCs2, entityListAddr + (8 * (i & 0x1FF)));
            if (!listEntry) continue;

            uintptr_t playerController = Read<uintptr_t>(hCs2, listEntry + 0x10);
            if (!playerController) continue;

            uint32_t playerPawnHandle = Read<uint32_t>(hCs2, playerController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn);
            if (playerPawnHandle == 0xFFFFFFFF) continue;

            uintptr_t listEntry2 = Read<uintptr_t>(hCs2, entityListAddr + (0x8 * ((playerPawnHandle & 0x7FFF) >> 9)) + 0x10);
            if (!listEntry2) continue;

            uintptr_t playerPawn = Read<uintptr_t>(hCs2, listEntry2 + (0x78 * (playerPawnHandle & 0x1FF)));
            if (!playerPawn) continue;

            int health = Read<int>(hCs2, playerPawn + game_offsets::m_iHealth);
            if (health <= 0 || health > 100) continue;

            uintptr_t sceneNode = Read<uintptr_t>(hCs2, playerPawn + game_offsets::m_pGameSceneNode);
            if (!sceneNode) continue;

            Vector3 position = Read<Vector3>(hCs2, sceneNode + game_offsets::m_vecAbsOrigin);

            char playerName[128];
            ReadProcessMemory(hCs2, (LPCVOID)(playerController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName), &playerName, sizeof(playerName), NULL);

            wchar_t widePlayerName[128];
            MultiByteToWideChar(CP_UTF8, 0, playerName, -1, widePlayerName, 128);

            std::wcout << widePlayerName << L"\t\t"
                << health << L"\t\t"
                << position.x << L", " << position.y << L", " << position.z << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    CloseHandle(hCs2);
    return 0;
}
