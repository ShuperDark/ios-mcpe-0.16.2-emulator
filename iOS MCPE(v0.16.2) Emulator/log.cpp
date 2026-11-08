#include "common.h"
#include "cpu.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdarg>
#include <mutex>

LogLevel g_log_level = LogLevel::Info;

namespace {
std::mutex g_log_lock;
FILE* g_log_file = nullptr;
LARGE_INTEGER g_t0 = [] { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t; }();
}  // namespace

void log_open_file(const std::string& path) {
    std::lock_guard lk(g_log_lock);
    g_log_file = _wfopen(widen(path).c_str(), L"w");
}

void log_write(LogLevel lv, const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    double sec = double(t.QuadPart - g_t0.QuadPart) / double(f.QuadPart);
    static const char* tags[] = {"T", "D", "I", "W", "E"};
    auto* th = threads::current();
    std::lock_guard lk(g_log_lock);
    fprintf(stderr, "[%9.3f][%s][t%u] %s\n", sec, tags[(int)lv], th ? th->id : 0, buf);
    if (g_log_file) {
        fprintf(g_log_file, "[%9.3f][%s][t%u] %s\n", sec, tags[(int)lv], th ? th->id : 0, buf);
        fflush(g_log_file);
    }
}

[[noreturn]] void fatal(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log_write(LogLevel::Error, "FATAL: %s", buf);
    fflush(stderr);
    MessageBoxA(nullptr, buf, "iOS MCPE Emulator - fatal error", MB_ICONERROR | MB_OK);
    ExitProcess(1);
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
