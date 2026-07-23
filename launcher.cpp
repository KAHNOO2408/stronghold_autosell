// Launcher.cpp
// Launches Stronghold Crusader.exe (suspended), injects trade_dll.dll,
// then resumes the game. Both Launcher.exe and trade_dll.dll must sit
// in the same folder as "Stronghold Crusader.exe".

#include <windows.h>
#include <string>

static const wchar_t* GAME_EXE = L"Stronghold Crusader.exe";
static const wchar_t* MOD_DLL  = L"trade_dll.dll";

static std::wstring GetExeDirectory() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring full(path);
    size_t pos = full.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"" : full.substr(0, pos + 1);
}

// Returns 0 on success, otherwise the step number where it failed (used with GetLastError()).
static int InjectDll(HANDLE hProcess, const std::wstring& dllPath, DWORD* outLastError) {
    size_t sizeBytes = (dllPath.size() + 1) * sizeof(wchar_t);

    LPVOID remoteMem = VirtualAllocEx(hProcess, NULL, sizeBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteMem) { *outLastError = GetLastError(); return 1; }

    if (!WriteProcessMemory(hProcess, remoteMem, dllPath.c_str(), sizeBytes, NULL)) {
        *outLastError = GetLastError();
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return 2;
    }

    LPTHREAD_START_ROUTINE loadLibraryAddr =
        (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, loadLibraryAddr, remoteMem, 0, NULL);
    if (!hThread) {
        *outLastError = GetLastError();
        VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
        return 3;
    }

    WaitForSingleObject(hThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode); // this is the HMODULE returned by LoadLibraryW, 0 = failed to load the dll itself

    VirtualFreeEx(hProcess, remoteMem, 0, MEM_RELEASE);
    CloseHandle(hThread);

    if (exitCode == 0) {
        *outLastError = 0xDEAD; // sentinel meaning "LoadLibrary itself failed inside the target process"
        return 4;
    }

    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    std::wstring dir = GetExeDirectory();
    std::wstring gamePath = dir + GAME_EXE;
    std::wstring dllPath  = dir + MOD_DLL;

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

    BOOL created = CreateProcessW(
        gamePath.c_str(), NULL, NULL, NULL, FALSE,
        CREATE_SUSPENDED, NULL, dir.c_str(), &si, &pi
    );

    if (!created) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        wsprintfW(msg, L"Failed to start the game. GetLastError=%lu", err);
        MessageBoxW(NULL, msg, L"Launcher error", MB_ICONERROR);
        return 1;
    }

    DWORD lastError = 0;
    int step = InjectDll(pi.hProcess, dllPath, &lastError);

    if (step != 0) {
        wchar_t msg[256];
        wsprintfW(msg, L"Injection failed at step %d. GetLastError=%lu\n(4 = target process could not load the dll itself)", step, lastError);
        MessageBoxW(NULL, msg, L"Launcher warning", MB_ICONWARNING);
    }

    ResumeThread(pi.hThread);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return 0;
}
