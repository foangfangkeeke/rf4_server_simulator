#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

static auto realGetProc = ::GetProcAddress;
static std::atomic<bool> stopping{false};
static std::wstring logPath;

static void log(const std::string& message) {
    auto file = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const auto line = message + "\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

#include "remote-assistant.h"

static DWORD WINAPI worker(void*) {
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return 0;
    std::wstring executable(path);
    const auto slash = executable.find_last_of(L"\\/");
    if (slash == std::wstring::npos || _wcsicmp(executable.c_str() + slash + 1, L"rf4_x64.exe")) return 0;
    const auto folder = executable.substr(0, slash + 1);
    const auto ini = folder + L"rf4-remote-access.ini";
    if (GetPrivateProfileIntW(L"remote_access", L"enabled", 0, ini.c_str()) != 1) return 0;
    logPath = folder + L"rf4-remote-access.log";
    for (int attempt = 0; attempt < 3000 && !stopping; ++attempt) {
        if (auto game = GetModuleHandleW(L"GameAssembly.dll")) {
            remote_assistant::start(game);
            return 0;
        }
        Sleep(100);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        if (auto thread = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr)) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        stopping = true;
    }
    return TRUE;
}
