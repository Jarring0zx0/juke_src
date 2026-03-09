#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>

DWORD FindProcessId(const std::string& processName) {
    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    if (processesSnapshot == INVALID_HANDLE_VALUE) {
        return 0;
    }

    Process32First(processesSnapshot, &processInfo);
    if (processName.compare(processInfo.szExeFile) == 0) {
        CloseHandle(processesSnapshot);
        return processInfo.th32ProcessID;
    }

    while (Process32Next(processesSnapshot, &processInfo)) {
        if (processName.compare(processInfo.szExeFile) == 0) {
            CloseHandle(processesSnapshot);
            return processInfo.th32ProcessID;
        }
    }

    CloseHandle(processesSnapshot);
    return 0;
}

bool InjectDLL(DWORD processId, const std::string& dllPath) {

    HANDLE hProcess = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                 PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                                 FALSE, processId);
    if (hProcess == NULL) {
        return false;
    }

    LPVOID pDllPath = VirtualAllocEx(hProcess, NULL, dllPath.size() + 1,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (pDllPath == NULL) {
        CloseHandle(hProcess);
        return false;
    }

    if (!WriteProcessMemory(hProcess, pDllPath, dllPath.c_str(), dllPath.size() + 1, NULL)) {
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HMODULE hKernel32 = GetModuleHandleA("kernel32.dll");
    if (hKernel32 == NULL) {
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    LPVOID pLoadLibraryA = GetProcAddress(hKernel32, "LoadLibraryA");
    if (pLoadLibraryA == NULL) {
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
                                       (LPTHREAD_START_ROUTINE)pLoadLibraryA,
                                       pDllPath, 0, NULL);
    if (hThread == NULL) {
        VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);

    VirtualFreeEx(hProcess, pDllPath, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);

    return true;
}

std::string FindFirstDLL() {
    WIN32_FIND_DATAA findFileData;
    HANDLE hFind = FindFirstFileA("*.dll", &findFileData);

    if (hFind != INVALID_HANDLE_VALUE) {
        std::string dllName = findFileData.cFileName;
        FindClose(hFind);
        return dllName;
    }

    return "";
}

int main() {
    std::cout << "frosty dll injector" << std::endl;

    DWORD processId = FindProcessId("RocketLeague.exe");

    if (processId == 0) {
        std::cout << "rocket league not found" << std::endl;
        system("pause");
        return 1;
    }

    std::string dllName = FindFirstDLL();
    if (dllName.empty()) {
        std::cout << "no dll found in current directory" << std::endl;
        system("pause");
        return 1;
    }

    char fullPath[MAX_PATH];
    GetFullPathNameA(dllName.c_str(), MAX_PATH, fullPath, NULL);

    if (InjectDLL(processId, std::string(fullPath))) {
        std::cout << "mod successfully injected" << std::endl;
    } else {
        std::cout << "injection failed" << std::endl;
    }

    system("pause");
    return 0;
}
