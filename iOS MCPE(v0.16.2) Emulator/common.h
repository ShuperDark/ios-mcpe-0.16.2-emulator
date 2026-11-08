// Common definitions for the host side of the emulator.
//
// Design note: guest (arm64 iOS) virtual addresses are identical to host
// virtual addresses. Every guest pointer can be dereferenced directly by host
// code and every host allocation handed to the guest is valid guest memory.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <atomic>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using gaddr = std::uint64_t;  // guest address (== host address)

template <class T = void>
inline T* gptr(gaddr a) { return reinterpret_cast<T*>(static_cast<uintptr_t>(a)); }
inline gaddr gaddr_of(const void* p) { return static_cast<gaddr>(reinterpret_cast<uintptr_t>(p)); }

enum class LogLevel { Trace, Debug, Info, Warn, Error };
extern LogLevel g_log_level;
void log_write(LogLevel lv, const char* fmt, ...);
[[noreturn]] void fatal(const char* fmt, ...);

#define LOGT(...) do { if (g_log_level <= LogLevel::Trace) log_write(LogLevel::Trace, __VA_ARGS__); } while (0)
#define LOGD(...) do { if (g_log_level <= LogLevel::Debug) log_write(LogLevel::Debug, __VA_ARGS__); } while (0)
#define LOGI(...) log_write(LogLevel::Info, __VA_ARGS__)
#define LOGW(...) log_write(LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) log_write(LogLevel::Error, __VA_ARGS__)

// UTF-8 <-> UTF-16 helpers (Windows APIs are UTF-16)
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

inline u64 align_up(u64 v, u64 a) { return (v + a - 1) & ~(a - 1); }
inline u64 align_down(u64 v, u64 a) { return v & ~(a - 1); }
