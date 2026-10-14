#include "common/log.h"

#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hw2vr::log {
namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
std::mutex g_mutex;
std::atomic<bool> g_verbose{false};

}  // namespace

void init(const wchar_t* path) {
    std::lock_guard lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) return;
    g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
}

void set_verbose(bool on) { g_verbose = on; }
bool verbose() { return g_verbose; }

void write(const char* fmt, ...) {
    char line[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = std::snprintf(line, sizeof line, "%02u:%02u:%02u.%03u [%5lu] ", t.wHour, t.wMinute, t.wSecond,
                          t.wMilliseconds, GetCurrentThreadId());
    va_list args;
    va_start(args, fmt);
    int m = std::vsnprintf(line + n, sizeof line - n - 2, fmt, args);
    va_end(args);
    n = (m < 0) ? n : (n + m < static_cast<int>(sizeof line) - 2 ? n + m : static_cast<int>(sizeof line) - 3);
    line[n++] = '\r';
    line[n++] = '\n';

    std::lock_guard lock(g_mutex);
    if (g_file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(g_file, line, static_cast<DWORD>(n), &written, nullptr);
}

}  // namespace hw2vr::log
