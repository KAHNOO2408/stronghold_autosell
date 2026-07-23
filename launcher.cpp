// Launcher.cpp
// Launches Stronghold Crusader.exe (suspended), injects trade_dll.dll,
// then resumes the game. Both Launcher.exe and trade_dll.dll must sit
// in the same folder as "Stronghold Crusader.exe".

#include <windows.h>
#include <string>
#include <tlhelp32.h>

// Name of the game executable (must match exactly, case-insensitive on Windows)
static const wchar_t* GAME_EXE = L"Stronghold Crusader.exe";
static const wchar_t* MOD_DLL  = L"trade_dll.dll";

static std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring full(path);
    size_t pos = full.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"" : full.substr(0, pos + 1);
}

static bool InjectDll(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return false;

    size_t sizeBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remoteMem = VirtualAllocEx(hProcess, NULL, sizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { CloseHandle(hProcess); return false; }

    WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), sizeBytes, NULL);

    LPTHREAD_START_ROUTINE loadLibraryAddr =
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, loadLibraryAddr, remoteMem, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return false;
    }

    WaitForSingleObject(hThread, INFINITE);

    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return true;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    std::wstring dir = GetExeDirectory();
    std::wstring gamePath = dir + GAME_EXE;
    std::wstring dllPath  = dir + MOD_DLL;

    // Verify files exist
    if (GetFileAttributesW(gamePath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(NULL, (L"Game exe not found:\n" + gamePath).c_str(), L"Launcher error", MB_ICONERROR);
        return 1;
    }
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        MessageBoxW(NULL, (L"trade_dll.dll not found:\n" + dllPath).c_str(), L"Launcher error", MB_ICONERROR);
        return 1;
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    // Start the game suspended so we can inject before it runs any code
    BOOL created = CreateProcessW(
        gamePath.c_str(),
        NULL,
        NULL,
        NULL,
        FALSE,
        CREATE_SUSPENDED,
        NULL,
        dir.c_str(),
        &si,
        &pi
    );

    if (!created) {
        MessageBoxW(NULL, L"Failed to start the game.", L"Launcher error", MB_ICONERROR);
        return 1;
    }

    bool ok = InjectDll(pi.dwProcessId, dllPath);

    if (!ok) {
        MessageBoxW(NULL, L"Failed to inject trade_dll.dll. The game will still start.", L"Launcher warning", MB_ICONWARNING);
    }

    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 0;
}
