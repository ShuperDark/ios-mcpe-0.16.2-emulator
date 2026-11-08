// Helpers shared by the libSystem HLE implementation files.
#pragma once
#include "common.h"
#include "cpu.h"
#include "hle.h"

namespace hlec {

// Darwin errno values
enum : int {
    D_EPERM = 1, D_ENOENT = 2, D_ESRCH = 3, D_EINTR = 4, D_EIO = 5, D_ENXIO = 6, D_E2BIG = 7, D_ENOEXEC = 8,
    D_EBADF = 9, D_ECHILD = 10, D_EDEADLK = 11, D_ENOMEM = 12, D_EACCES = 13, D_EFAULT = 14, D_EBUSY = 16,
    D_EEXIST = 17, D_EXDEV = 18, D_ENODEV = 19, D_ENOTDIR = 20, D_EISDIR = 21, D_EINVAL = 22, D_ENFILE = 23,
    D_EMFILE = 24, D_ENOTTY = 25, D_EFBIG = 27, D_ENOSPC = 28, D_ESPIPE = 29, D_EROFS = 30, D_EMLINK = 31,
    D_EPIPE = 32, D_EDOM = 33, D_ERANGE = 34, D_EAGAIN = 35, D_EINPROGRESS = 36, D_EALREADY = 37,
    D_ENOTSOCK = 38, D_EDESTADDRREQ = 39, D_EMSGSIZE = 40, D_EPROTOTYPE = 41, D_ENOPROTOOPT = 42,
    D_EPROTONOSUPPORT = 43, D_ENOTSUP = 45, D_EAFNOSUPPORT = 47, D_EADDRINUSE = 48, D_EADDRNOTAVAIL = 49,
    D_ENETDOWN = 50, D_ENETUNREACH = 51, D_ECONNABORTED = 53, D_ECONNRESET = 54, D_ENOBUFS = 55,
    D_EISCONN = 56, D_ENOTCONN = 57, D_ETIMEDOUT = 60, D_ECONNREFUSED = 61, D_ENAMETOOLONG = 63,
    D_EHOSTUNREACH = 65, D_ENOTEMPTY = 66, D_ENOLCK = 77, D_ENOSYS = 78, D_EOVERFLOW = 84, D_ECANCELED = 89,
    D_EILSEQ = 92, D_EOPNOTSUPP = 102,
};

int map_crt_errno(int e);   // MSVC CRT errno -> Darwin
int map_win32_error(unsigned long e);  // GetLastError -> Darwin
int map_wsa_error(int e);   // WSAGetLastError -> Darwin
void set_errno(int darwin_errno);
inline void set_errno_crt() { set_errno(map_crt_errno(errno)); }
int get_errno();

// Darwin struct layouts
#pragma pack(push, 1)
struct d_timespec { s64 tv_sec; s64 tv_nsec; };
struct d_timeval { s64 tv_sec; s32 tv_usec; s32 _pad; };
struct d_stat {
    s32 st_dev; u16 st_mode; u16 st_nlink; u64 st_ino; u32 st_uid; u32 st_gid; s32 st_rdev; s32 _pad0;
    d_timespec st_atimespec, st_mtimespec, st_ctimespec, st_birthtimespec;
    s64 st_size; s64 st_blocks; s32 st_blksize; u32 st_flags; u32 st_gen; s32 st_lspare; s64 st_qspare[2];
};
struct d_tm {
    s32 tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst, _pad;
    s64 tm_gmtoff;
    u64 tm_zone;  // char*
};
#pragma pack(pop)
static_assert(sizeof(d_stat) == 144);
static_assert(sizeof(d_tm) == 56);
static_assert(sizeof(d_timeval) == 16);

// Fill a Darwin stat from a host file attribute query. Returns false (and sets errno) on failure.
bool host_stat(const std::wstring& path, d_stat* st);
bool host_fstat_handle(void* h, d_stat* st);

// guest-visible static string storage (never freed)
const char* intern(const std::string& s);

}  // namespace hlec
