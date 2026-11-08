// libSystem: file descriptors, stdio, directories, stat, paths.
#include "fdtab.h"
#include "gfmt.h"
#include "hle_common.h"
#include "vfs.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <filesystem>
#include <vector>

using namespace hlec;

namespace {
enum : int {
    DO_RDONLY = 0, DO_WRONLY = 1, DO_RDWR = 2, DO_ACCMODE = 3, DO_NONBLOCK = 0x4, DO_APPEND = 0x8,
    DO_CREAT = 0x200, DO_TRUNC = 0x400, DO_EXCL = 0x800, DO_DIRECTORY = 0x100000, DO_CLOEXEC = 0x1000000,
};
}  // namespace

// ===================================================================== fd table
namespace fdtab {
namespace {
std::mutex g_lock;
std::vector<std::shared_ptr<Fd>> g_fds;

void init_std() {
    static bool done = false;
    if (done) return;
    done = true;
    auto mk = [](Kind k) { auto f = std::make_shared<Fd>(); f->kind = k; return f; };
    g_fds.push_back(mk(Kind::StdIn));
    g_fds.push_back(mk(Kind::StdOut));
    g_fds.push_back(mk(Kind::StdErr));
}
}  // namespace

Fd::~Fd() {
    if (handle && (kind == Kind::File || kind == Kind::PipeRead || kind == Kind::PipeWrite)) CloseHandle(handle);
    if (kind == Kind::Socket && sock != ~0ull) closesocket((SOCKET)sock);
}

int add(std::shared_ptr<Fd> fd, int min_fd) {
    std::lock_guard lk(g_lock);
    init_std();
    for (size_t i = std::max(0, min_fd); i < g_fds.size(); i++)
        if (!g_fds[i]) { g_fds[i] = std::move(fd); return (int)i; }
    while ((int)g_fds.size() < min_fd) g_fds.push_back(nullptr);
    g_fds.push_back(std::move(fd));
    return (int)g_fds.size() - 1;
}
std::shared_ptr<Fd> get(int fd) {
    std::lock_guard lk(g_lock);
    init_std();
    if (fd < 0 || fd >= (int)g_fds.size()) return nullptr;
    return g_fds[fd];
}
int close(int fd) {
    std::shared_ptr<Fd> f;
    {
        std::lock_guard lk(g_lock);
        init_std();
        if (fd < 0 || fd >= (int)g_fds.size() || !g_fds[fd]) { set_errno(D_EBADF); return -1; }
        f = std::move(g_fds[fd]);
    }
    return 0;
}
int dup(int fd, int min_fd) {
    auto f = get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    return add(f, min_fd);
}
int dup2(int fd, int to) {
    auto f = get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    std::lock_guard lk(g_lock);
    while ((int)g_fds.size() <= to) g_fds.push_back(nullptr);
    g_fds[to] = f;
    return to;
}

s64 read(int fd, void* buf, size_t n) {
    auto f = get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    switch (f->kind) {
    case Kind::Null: return 0;
    case Kind::Random:
        BCryptGenRandom(nullptr, (PUCHAR)buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        return (s64)n;
    case Kind::StdIn: return 0;
    case Kind::File:
    case Kind::PipeRead: {
        DWORD got = 0;
        if (!ReadFile(f->handle, buf, (DWORD)std::min<size_t>(n, 0x7fffffff), &got, nullptr)) {
            DWORD e = GetLastError();
            if (e == ERROR_BROKEN_PIPE || e == ERROR_HANDLE_EOF) return 0;
            set_errno(map_win32_error(e));
            return -1;
        }
        return got;
    }
    case Kind::Socket: {
        int r = ::recv((SOCKET)f->sock, (char*)buf, (int)n, 0);
        if (r < 0) { set_errno(D_EAGAIN); return -1; }
        return r;
    }
    default: set_errno(D_EBADF); return -1;
    }
}

s64 write(int fd, const void* buf, size_t n) {
    auto f = get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    switch (f->kind) {
    case Kind::Null: return (s64)n;
    case Kind::StdOut:
    case Kind::StdErr: {
        FILE* out = f->kind == Kind::StdOut ? stdout : stderr;
        fwrite(buf, 1, n, out);
        fflush(out);
        return (s64)n;
    }
    case Kind::File:
    case Kind::PipeWrite: {
        std::lock_guard lk(f->lock);
        if (f->flags & DO_APPEND) {
            LARGE_INTEGER z{};
            SetFilePointerEx(f->handle, z, nullptr, FILE_END);
        }
        DWORD put = 0;
        if (!WriteFile(f->handle, buf, (DWORD)std::min<size_t>(n, 0x7fffffff), &put, nullptr)) {
            set_errno(map_win32_error(GetLastError()));
            return -1;
        }
        return put;
    }
    case Kind::Socket: {
        int r = ::send((SOCKET)f->sock, (const char*)buf, (int)n, 0);
        if (r < 0) { set_errno(D_EPIPE); return -1; }
        return r;
    }
    default: set_errno(D_EBADF); return -1;
    }
}

s64 lseek(int fd, s64 off, int whence) {
    auto f = get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    if (f->kind != Kind::File) { set_errno(D_ESPIPE); return -1; }
    LARGE_INTEGER li, out;
    li.QuadPart = off;
    DWORD m = whence == 0 ? FILE_BEGIN : whence == 1 ? FILE_CURRENT : FILE_END;
    if (!SetFilePointerEx(f->handle, li, &out, m)) { set_errno(D_EINVAL); return -1; }
    return out.QuadPart;
}

int open(const char* path, int dflags, int mode) {
    (void)mode;
    if (!path) { set_errno(D_EFAULT); return -1; }
    if (vfs::is_dev_null(path)) { auto f = std::make_shared<Fd>(); f->kind = Kind::Null; return add(f); }
    if (vfs::is_dev_random(path)) { auto f = std::make_shared<Fd>(); f->kind = Kind::Random; return add(f); }
    static const bool trace_slow = getenv("EMU_PROFILE") != nullptr;
    LARGE_INTEGER tq0, tq1, tq2, tqf;
    if (trace_slow) QueryPerformanceCounter(&tq0);
    std::wstring hp = vfs::to_host(path);
    if (trace_slow) QueryPerformanceCounter(&tq1);
    DWORD attr = GetFileAttributesW(hp.c_str());
    if (trace_slow) {
        QueryPerformanceCounter(&tq2);
        QueryPerformanceFrequency(&tqf);
        double a = 1000.0 * (tq1.QuadPart - tq0.QuadPart) / tqf.QuadPart, b = 1000.0 * (tq2.QuadPart - tq1.QuadPart) / tqf.QuadPart;
        if (a + b > 1.0) LOGI("slow open: to_host %.2f ms, GetFileAttributes %.2f ms: %s", a, b, path);
    }
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if ((dflags & DO_ACCMODE) != DO_RDONLY) { set_errno(D_EISDIR); return -1; }
        // directory fd: opened with backup semantics (used for fsync of dirs, openat...)
        HANDLE h = CreateFileW(hp.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE) { set_errno(map_win32_error(GetLastError())); return -1; }
        auto f = std::make_shared<Fd>();
        f->kind = Kind::File;
        f->handle = h;
        f->guest_path = path;
        return add(f);
    }
    if (dflags & DO_DIRECTORY) { set_errno(D_ENOTDIR); return -1; }
    if (((dflags & DO_ACCMODE) != DO_RDONLY || (dflags & (DO_CREAT | DO_TRUNC))) && vfs::is_readonly(path)) {
        LOGW("refusing write access to bundle file %s", path);
        set_errno(D_EROFS);
        return -1;
    }
    DWORD access = 0;
    switch (dflags & DO_ACCMODE) {
    case DO_RDONLY: access = GENERIC_READ; break;
    case DO_WRONLY: access = GENERIC_WRITE; break;
    default: access = GENERIC_READ | GENERIC_WRITE; break;
    }
    DWORD disp;
    if (dflags & DO_CREAT) {
        if (dflags & DO_EXCL) disp = CREATE_NEW;
        else if (dflags & DO_TRUNC) disp = CREATE_ALWAYS;
        else disp = OPEN_ALWAYS;
    } else {
        disp = (dflags & DO_TRUNC) ? TRUNCATE_EXISTING : OPEN_EXISTING;
    }
    if (trace_slow) QueryPerformanceCounter(&tq0);
    HANDLE h = CreateFileW(hp.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, disp,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (trace_slow) {
        QueryPerformanceCounter(&tq1);
        double c = 1000.0 * (tq1.QuadPart - tq0.QuadPart) / tqf.QuadPart;
        if (c > 1.0) LOGI("slow open: CreateFile %.2f ms: %s", c, path);
    }
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND) set_errno(D_ENOENT);
        else set_errno(map_win32_error(e));
        LOGD("open(%s) failed: %lu", path, e);
        return -1;
    }
    auto f = std::make_shared<Fd>();
    f->kind = Kind::File;
    f->handle = h;
    f->flags = dflags;
    f->guest_path = path;
    f->cloexec = (dflags & DO_CLOEXEC) != 0;
    return add(f);
}

}  // namespace fdtab

// ===================================================================== stat helpers
namespace hlec {
static void fill_stat(const BY_HANDLE_FILE_INFORMATION& fi, d_stat* st) {
    memset(st, 0, sizeof *st);
    bool dir = (fi.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    st->st_mode = dir ? (0040000 | 0755) : (0100000 | 0644);
    st->st_nlink = (u16)std::max<DWORD>(1, fi.nNumberOfLinks);
    st->st_ino = (u64(fi.nFileIndexHigh) << 32) | fi.nFileIndexLow;
    st->st_dev = (s32)fi.dwVolumeSerialNumber;
    st->st_uid = st->st_gid = 501;
    st->st_size = dir ? 64 : (s64)((u64(fi.nFileSizeHigh) << 32) | fi.nFileSizeLow);
    st->st_blksize = 4096;
    st->st_blocks = (st->st_size + 511) / 512;
    auto cv = [](FILETIME ft, d_timespec& ts) {
        u64 t = (u64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        if (t < 116444736000000000ull) { ts.tv_sec = 0; ts.tv_nsec = 0; return; }
        t -= 116444736000000000ull;
        ts.tv_sec = s64(t / 10000000);
        ts.tv_nsec = s64(t % 10000000) * 100;
    };
    cv(fi.ftLastAccessTime, st->st_atimespec);
    cv(fi.ftLastWriteTime, st->st_mtimespec);
    cv(fi.ftLastWriteTime, st->st_ctimespec);
    cv(fi.ftCreationTime, st->st_birthtimespec);
}

bool host_fstat_handle(void* h, d_stat* st) {
    BY_HANDLE_FILE_INFORMATION fi;
    if (!GetFileInformationByHandle(h, &fi)) { set_errno(map_win32_error(GetLastError())); return false; }
    fill_stat(fi, st);
    return true;
}

bool host_stat(const std::wstring& path, d_stat* st) {
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        set_errno(e == ERROR_PATH_NOT_FOUND || e == ERROR_FILE_NOT_FOUND ? D_ENOENT : map_win32_error(e));
        LOGD("host_stat CreateFile error %lu", e);
        return false;
    }
    bool ok = host_fstat_handle(h, st);
    if (!ok) LOGD("host_stat GetFileInformationByHandle error %lu", GetLastError());
    CloseHandle(h);
    return ok;
}
}  // namespace hlec

// ===================================================================== POSIX file API
HLE_RAW(open) {
    // open(path, flags, ...) - mode is variadic (stack)
    const char* p = gptr<const char>(c.x(0));
    int fl = (int)c.x(1);
    int mode = (fl & DO_CREAT) ? (int)hle::VaReader(c.sp()).next_u64() : 0;
    c.set_x(0, (u64)(s64)fdtab::open(p, fl, mode));
}
HLE_RAW(openat) {
    const char* p = gptr<const char>(c.x(1));
    int fl = (int)c.x(2);
    int dfd = (int)c.x(0);
    std::string full = p ? p : "";
    if (p && p[0] != '/' && dfd != -2) {
        if (auto f = fdtab::get(dfd)) full = f->guest_path + "/" + full;
    }
    c.set_x(0, (u64)(s64)fdtab::open(full.c_str(), fl, 0));
}
HLE_FUNC(close, int, (int fd)) { return fdtab::close(fd); }
HLE_FUNC(read, s64, (int fd, void* b, size_t n)) { return fdtab::read(fd, b, n); }
HLE_FUNC(write, s64, (int fd, const void* b, size_t n)) { return fdtab::write(fd, b, n); }
HLE_FUNC(lseek, s64, (int fd, s64 off, int wh)) { return fdtab::lseek(fd, off, wh); }
HLE_FUNC(pread, s64, (int fd, void* b, size_t n, s64 off)) {
    auto f = fdtab::get(fd);
    if (!f || f->kind != fdtab::Kind::File) { set_errno(D_EBADF); return -1; }
    OVERLAPPED ov{};
    ov.Offset = (DWORD)(u64)off;
    ov.OffsetHigh = (DWORD)((u64)off >> 32);
    DWORD got = 0;
    if (!ReadFile(f->handle, b, (DWORD)n, &got, &ov)) {
        DWORD e = GetLastError();
        if (e == ERROR_HANDLE_EOF) return 0;
        set_errno(map_win32_error(e));
        return -1;
    }
    return got;
}
HLE_FUNC(pwrite, s64, (int fd, const void* b, size_t n, s64 off)) {
    auto f = fdtab::get(fd);
    if (!f || f->kind != fdtab::Kind::File) { set_errno(D_EBADF); return -1; }
    OVERLAPPED ov{};
    ov.Offset = (DWORD)(u64)off;
    ov.OffsetHigh = (DWORD)((u64)off >> 32);
    DWORD put = 0;
    if (!WriteFile(f->handle, b, (DWORD)n, &put, &ov)) { set_errno(map_win32_error(GetLastError())); return -1; }
    return put;
}
HLE_FUNC(fsync, int, (int fd)) {
    auto f = fdtab::get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    if (f->kind == fdtab::Kind::File) FlushFileBuffers(f->handle);
    return 0;
}
HLE_FUNC(fdatasync, int, (int fd)) { return hle_impl_fsync(fd); }
HLE_FUNC(ftruncate, int, (int fd, s64 len)) {
    auto f = fdtab::get(fd);
    if (!f || f->kind != fdtab::Kind::File) { set_errno(D_EBADF); return -1; }
    FILE_END_OF_FILE_INFO ei;
    ei.EndOfFile.QuadPart = len;
    if (!SetFileInformationByHandle(f->handle, FileEndOfFileInfo, &ei, sizeof ei)) {
        set_errno(map_win32_error(GetLastError()));
        return -1;
    }
    return 0;
}
HLE_FUNC(truncate, int, (const char* p, s64 len)) {
    if (vfs::is_readonly(p)) { LOGW("refusing to modify bundle path %s", p); set_errno(D_EROFS); return -1; }
    int fd = fdtab::open(p, DO_WRONLY, 0);
    if (fd < 0) return -1;
    int r = hle_impl_ftruncate(fd, len);
    fdtab::close(fd);
    return r;
}
HLE_FUNC(dup, int, (int fd)) { return fdtab::dup(fd); }
HLE_FUNC(dup2, int, (int fd, int to)) { return fdtab::dup2(fd, to); }
HLE_FUNC(isatty, int, (int fd)) { return fd <= 2 ? 1 : 0; }
HLE_RAW(fcntl) {
    int fd = (int)c.x(0), cmd = (int)c.x(1);
    u64 arg = hle::VaReader(c.sp()).next_u64();
    auto f = fdtab::get(fd);
    if (!f) { set_errno(D_EBADF); c.set_x(0, (u64)-1); return; }
    s64 r = 0;
    switch (cmd) {
    case 0: r = fdtab::dup(fd, (int)arg); break;           // F_DUPFD
    case 67: r = fdtab::dup(fd, (int)arg); break;          // F_DUPFD_CLOEXEC
    case 1: r = f->cloexec ? 1 : 0; break;                 // F_GETFD
    case 2: f->cloexec = (arg & 1) != 0; break;            // F_SETFD
    case 3: r = f->flags; break;                           // F_GETFL
    case 4: {                                              // F_SETFL
        f->flags = (f->flags & ~(DO_NONBLOCK | DO_APPEND)) | ((int)arg & (DO_NONBLOCK | DO_APPEND));
        if (f->kind == fdtab::Kind::Socket) {
            u_long nb = (arg & DO_NONBLOCK) ? 1 : 0;
            ioctlsocket((SOCKET)f->sock, FIONBIO, &nb);
        }
    } break;
    case 7: if (arg) *gptr<s16>(arg + 20) = 2; break;      // F_GETLK -> F_UNLCK (l_type at offset 20)
    case 8: case 9: break;                                 // F_SETLK / F_SETLKW: always succeed
    default: break;                                        // F_FULLFSYNC, F_NOCACHE, F_SETNOSIGPIPE...
    }
    c.set_x(0, (u64)r);
}
HLE_RAW(ioctl) {
    int fd = (int)c.x(0);
    u64 req = c.x(1) & 0xffffffff;
    u64 arg = hle::VaReader(c.sp()).next_u64();
    auto f = fdtab::get(fd);
    if (f && f->kind == fdtab::Kind::Socket) {
        if (req == 0x8004667e) {  // FIONBIO
            u_long nb = arg ? *gptr<int>(arg) : 0;
            ioctlsocket((SOCKET)f->sock, FIONBIO, &nb);
            c.set_x(0, 0);
            return;
        }
        if (req == 0x4004667f) {  // FIONREAD
            u_long n = 0;
            ioctlsocket((SOCKET)f->sock, FIONREAD, &n);
            if (arg) *gptr<int>(arg) = (int)n;
            c.set_x(0, 0);
            return;
        }
    }
    set_errno(D_ENOTTY);
    c.set_x(0, (u64)-1);
}

HLE_FUNC(stat, int, (const char* p, d_stat* st)) {
    if (vfs::is_dev_null(p) || vfs::is_dev_random(p)) { memset(st, 0, sizeof *st); st->st_mode = 0020666; return 0; }
    bool ok = host_stat(vfs::to_host(p), st);
    if (!ok) LOGD("stat(%s) failed -> %s", p, narrow(vfs::to_host(p)).c_str());
    return ok ? 0 : -1;
}
HLE_FUNC(lstat, int, (const char* p, d_stat* st)) { return hle_impl_stat(p, st); }
HLE_FUNC(fstat, int, (int fd, d_stat* st)) {
    auto f = fdtab::get(fd);
    if (!f) { set_errno(D_EBADF); return -1; }
    if (f->kind == fdtab::Kind::File) return host_fstat_handle(f->handle, st) ? 0 : -1;
    memset(st, 0, sizeof *st);
    st->st_mode = f->kind == fdtab::Kind::Socket ? 0140777 : 0020666;
    st->st_blksize = 4096;
    return 0;
}
HLE_ALIAS("_stat64", hle_impl_stat);
HLE_ALIAS("_lstat64", hle_impl_lstat);
HLE_ALIAS("_fstat64", hle_impl_fstat);
HLE_FUNC(fstatat, int, (int dfd, const char* p, d_stat* st, int)) {
    std::string full = p ? p : "";
    if (p && p[0] != '/' && dfd != -2) if (auto f = fdtab::get(dfd)) full = f->guest_path + "/" + full;
    return hle_impl_stat(full.c_str(), st);
}
HLE_FUNC(access, int, (const char* p, int mode)) {
    (void)mode;
    if (vfs::is_dev_null(p) || vfs::is_dev_random(p)) return 0;
    DWORD a = GetFileAttributesW(vfs::to_host(p).c_str());
    if (a == INVALID_FILE_ATTRIBUTES) { set_errno(D_ENOENT); return -1; }
    return 0;
}
HLE_FUNC(faccessat, int, (int, const char* p, int mode, int)) { return hle_impl_access(p, mode); }
HLE_FUNC(mkdir, int, (const char* p, u16)) {
    if (vfs::is_readonly(p)) { LOGW("refusing to modify bundle path %s", p); set_errno(D_EROFS); return -1; }
    if (!CreateDirectoryW(vfs::to_host(p).c_str(), nullptr)) {
        DWORD e = GetLastError();
        set_errno(e == ERROR_ALREADY_EXISTS ? D_EEXIST : e == ERROR_PATH_NOT_FOUND ? D_ENOENT : map_win32_error(e));
        return -1;
    }
    return 0;
}
HLE_FUNC(mkdirat, int, (int, const char* p, u16 m)) { return hle_impl_mkdir(p, m); }
HLE_FUNC(rmdir, int, (const char* p)) {
    if (vfs::is_readonly(p)) { LOGW("refusing to modify bundle path %s", p); set_errno(D_EROFS); return -1; }
    if (!RemoveDirectoryW(vfs::to_host(p).c_str())) { set_errno(map_win32_error(GetLastError())); return -1; }
    return 0;
}
static int do_unlink(const std::wstring& hp) {
    if (DeleteFileW(hp.c_str())) return 0;
    DWORD e = GetLastError();
    if (e == ERROR_ACCESS_DENIED) {
        // maybe read-only attribute
        SetFileAttributesW(hp.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(hp.c_str())) return 0;
        e = GetLastError();
    }
    set_errno(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? D_ENOENT : map_win32_error(e));
    return -1;
}
HLE_FUNC(unlink, int, (const char* p)) {
    if (vfs::is_readonly(p)) { LOGW("refusing to modify bundle path %s", p); set_errno(D_EROFS); return -1; } return do_unlink(vfs::to_host(p)); }
HLE_FUNC(unlinkat, int, (int, const char* p, int flags)) {
    if (vfs::is_readonly(p)) { LOGW("refusing to modify bundle path %s", p); set_errno(D_EROFS); return -1; }
    if (flags & 0x80) return hle_impl_rmdir(p);  // AT_REMOVEDIR
    return do_unlink(vfs::to_host(p));
}
HLE_FUNC(remove, int, (const char* p)) {
    if (vfs::is_readonly(p)) { LOGW("refusing to modify bundle path %s", p); set_errno(D_EROFS); return -1; }
    std::wstring hp = vfs::to_host(p);
    DWORD a = GetFileAttributesW(hp.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return hle_impl_rmdir(p);
    return do_unlink(hp);
}
HLE_FUNC(rename, int, (const char* a, const char* b)) {
    if (vfs::is_readonly(b)) { LOGW("refusing to modify bundle path %s", b); set_errno(D_EROFS); return -1; }
    if (vfs::is_readonly(a)) { LOGW("refusing to modify bundle path %s", a); set_errno(D_EROFS); return -1; }
    std::wstring ha = vfs::to_host(a), hb = vfs::to_host(b);
    if (!MoveFileExW(ha.c_str(), hb.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) {
        DWORD e = GetLastError();
        // renaming a directory onto an existing empty directory
        if (e == ERROR_ACCESS_DENIED || e == ERROR_ALREADY_EXISTS) {
            RemoveDirectoryW(hb.c_str());
            if (MoveFileExW(ha.c_str(), hb.c_str(), MOVEFILE_REPLACE_EXISTING)) return 0;
            e = GetLastError();
        }
        set_errno(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? D_ENOENT : map_win32_error(e));
        return -1;
    }
    return 0;
}
HLE_FUNC(renameat, int, (int, const char* a, int, const char* b)) { return hle_impl_rename(a, b); }
HLE_FUNC(link, int, (const char* a, const char* b)) {
    if (!CreateHardLinkW(vfs::to_host(b).c_str(), vfs::to_host(a).c_str(), nullptr)) {
        set_errno(map_win32_error(GetLastError()));
        return -1;
    }
    return 0;
}
HLE_FUNC(symlink, int, (const char*, const char*)) { set_errno(D_ENOTSUP); return -1; }
HLE_FUNC(readlink, s64, (const char*, char*, size_t)) { set_errno(D_EINVAL); return -1; }
HLE_FUNC(chmod, int, (const char*, u16)) { return 0; }
HLE_FUNC(fchmod, int, (int, u16)) { return 0; }
HLE_FUNC(fchmodat, int, (int, const char*, u16, int)) { return 0; }
HLE_FUNC(chown, int, (const char*, u32, u32)) { return 0; }
HLE_FUNC(utimes, int, (const char*, const void*)) { return 0; }
HLE_FUNC(utimensat, int, (int, const char*, const void*, int)) { return 0; }
HLE_FUNC(futimes, int, (int, const void*)) { return 0; }
HLE_FUNC(umask, u16, (u16)) { return 022; }
HLE_FUNC(getcwd, char*, (char* buf, size_t n)) {
    std::string c = vfs::cwd();
    if (!buf) { buf = static_cast<char*>(vmem::gmalloc(c.size() + 1)); n = c.size() + 1; }
    if (c.size() + 1 > n) { set_errno(D_ERANGE); return nullptr; }
    memcpy(buf, c.c_str(), c.size() + 1);
    return buf;
}
HLE_FUNC(chdir, int, (const char* p)) { vfs::set_cwd(p); return 0; }
static char* do_realpath(const char* p, char* out) {
    std::wstring hp = vfs::to_host(p);
    DWORD a = GetFileAttributesW(hp.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) { set_errno(D_ENOENT); return nullptr; }
    std::string g = vfs::to_guest(hp);
    if (!out) out = static_cast<char*>(vmem::gmalloc(1024));
    strncpy(out, g.c_str(), 1023);
    out[1023] = 0;
    return out;
}
HLE_FUNC(realpath, char*, (const char* p, char* out)) { return do_realpath(p, out); }
HLE_ALIAS("_realpath$DARWIN_EXTSN", hle_impl_realpath);
HLE_FUNC(statfs, int, (const char*, void* buf)) {
    memset(buf, 0, 2168);
    u32* b = static_cast<u32*>(buf);
    b[0] = 4096;  // f_bsize
    *reinterpret_cast<u64*>(b + 2) = 1ull << 24;  // f_blocks
    *reinterpret_cast<u64*>(b + 4) = 1ull << 23;  // f_bfree
    *reinterpret_cast<u64*>(b + 6) = 1ull << 23;  // f_bavail
    return 0;
}
HLE_FUNC(statvfs, int, (const char*, void* buf)) {
    // struct statvfs { ulong f_bsize, f_frsize; fsblkcnt_t(u32) f_blocks, f_bfree, f_bavail; fsfilcnt_t(u32) f_files, f_ffree, f_favail; ulong f_fsid, f_flag, f_namemax; }
    memset(buf, 0, 64);
    u8* b = static_cast<u8*>(buf);
    *reinterpret_cast<u64*>(b + 0) = 4096;
    *reinterpret_cast<u64*>(b + 8) = 4096;
    *reinterpret_cast<u32*>(b + 16) = 1u << 24;
    *reinterpret_cast<u32*>(b + 20) = 1u << 23;
    *reinterpret_cast<u32*>(b + 24) = 1u << 23;
    return 0;
}
HLE_FUNC(pipe, int, (int* fds)) {
    HANDLE r, w;
    if (!CreatePipe(&r, &w, nullptr, 1 << 16)) { set_errno(D_EMFILE); return -1; }
    auto fr = std::make_shared<fdtab::Fd>();
    fr->kind = fdtab::Kind::PipeRead;
    fr->handle = r;
    auto fw = std::make_shared<fdtab::Fd>();
    fw->kind = fdtab::Kind::PipeWrite;
    fw->handle = w;
    fds[0] = fdtab::add(fr);
    fds[1] = fdtab::add(fw);
    return 0;
}
HLE_FUNC(mkstemp, int, (char* tmpl)) {
    size_t n = strlen(tmpl);
    for (int attempt = 0; attempt < 100; attempt++) {
        for (size_t i = n; i > 0 && tmpl[i - 1] == 'X'; i--) tmpl[i - 1] = 'X';
        static const char cs[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        for (size_t i = n; i > 0 && (tmpl[i - 1] == 'X' || attempt); i--) {
            if (i <= n - 6) break;
            u32 r;
            BCryptGenRandom(nullptr, (PUCHAR)&r, 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
            tmpl[i - 1] = cs[r % 36];
        }
        int fd = fdtab::open(tmpl, DO_RDWR | DO_CREAT | DO_EXCL, 0600);
        if (fd >= 0) return fd;
    }
    return -1;
}

// ===================================================================== directories
namespace {
#pragma pack(push, 1)
struct d_dirent {
    u64 d_ino, d_seekoff;
    u16 d_reclen, d_namlen;
    u8 d_type;
    char d_name[1024];
    u8 _pad[3];
};
#pragma pack(pop)
struct GDir {
    u8 darwin_dir[64];  // opaque DIR area
    HANDLE find = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW fd;
    bool have = false;
    bool emitted_dot = false, emitted_dotdot = false;
    d_dirent ent;
    std::string path;
    int guest_fd = -1;
};
}  // namespace

HLE_FUNC(opendir, GDir*, (const char* p)) {
    std::wstring hp = vfs::to_host(p);
    DWORD a = GetFileAttributesW(hp.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) { set_errno(D_ENOENT); return nullptr; }
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) { set_errno(D_ENOTDIR); return nullptr; }
    void* mem = vmem::gcalloc(1, sizeof(GDir));
    auto* d = new (mem) GDir();
    d->path = p;
    d->find = FindFirstFileW((hp + L"\\*").c_str(), &d->fd);
    d->have = d->find != INVALID_HANDLE_VALUE;
    return d;
}
HLE_FUNC(fdopendir, GDir*, (int fd)) {
    auto f = fdtab::get(fd);
    if (!f) { set_errno(D_EBADF); return nullptr; }
    GDir* d = hle_impl_opendir(f->guest_path.c_str());
    if (d) d->guest_fd = fd;
    return d;
}
static d_dirent* next_ent(GDir* d) {
    while (d->have) {
        std::string name = narrow(d->fd.cFileName);
        bool dir = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        d->have = FindNextFileW(d->find, &d->fd) != 0;
        memset(&d->ent, 0, sizeof d->ent);
        d->ent.d_ino = std::hash<std::string>{}(name) | 1;
        d->ent.d_reclen = sizeof(d_dirent);
        d->ent.d_namlen = (u16)std::min<size_t>(name.size(), 1023);
        d->ent.d_type = dir ? 4 : 8;
        memcpy(d->ent.d_name, name.c_str(), d->ent.d_namlen);
        return &d->ent;
    }
    return nullptr;
}
HLE_FUNC(readdir, d_dirent*, (GDir* d)) { return d ? next_ent(d) : nullptr; }
HLE_FUNC(readdir_r, int, (GDir* d, d_dirent* e, d_dirent** res)) {
    d_dirent* n = d ? next_ent(d) : nullptr;
    if (n) { memcpy(e, n, sizeof *e); *res = e; } else *res = nullptr;
    return 0;
}
HLE_FUNC(closedir, int, (GDir* d)) {
    if (!d) return -1;
    if (d->find != INVALID_HANDLE_VALUE) FindClose(d->find);
    if (d->guest_fd >= 0) fdtab::close(d->guest_fd);
    d->~GDir();
    vmem::gfree(d);
    return 0;
}
HLE_FUNC(rewinddir, void, (GDir* d)) {
    if (!d) return;
    if (d->find != INVALID_HANDLE_VALUE) FindClose(d->find);
    d->find = FindFirstFileW((vfs::to_host(d->path.c_str()) + L"\\*").c_str(), &d->fd);
    d->have = d->find != INVALID_HANDLE_VALUE;
}
HLE_FUNC(dirfd, int, (GDir* d)) {
    if (!d) return -1;
    if (d->guest_fd < 0) d->guest_fd = fdtab::open(d->path.c_str(), 0, 0);
    return d->guest_fd;
}

// ===================================================================== stdio
namespace {
struct GFile {
    u8 darwin_file[152];  // looks like a Darwin FILE to anything poking at it
    int fd = -1;
    bool eof = false, err = false;
    bool writing = false;
    bool owns_fd = true;
    std::vector<u8> rbuf;
    size_t rpos = 0, rlen = 0;
    std::vector<u8> wbuf;
    std::vector<u8> unget;
    std::string mem;  // fmemopen-less helpers
    std::recursive_mutex lock;
};

GFile* g_stdin;
GFile* g_stdout;
GFile* g_stderr;

GFile* new_file(int fd) {
    void* mem = vmem::gcalloc(1, sizeof(GFile));
    auto* f = new (mem) GFile();
    f->fd = fd;
    return f;
}

bool flush_w(GFile* f) {
    if (!f->wbuf.empty()) {
        size_t off = 0;
        while (off < f->wbuf.size()) {
            s64 n = fdtab::write(f->fd, f->wbuf.data() + off, f->wbuf.size() - off);
            if (n <= 0) { f->err = true; f->wbuf.clear(); return false; }
            off += (size_t)n;
        }
        f->wbuf.clear();
    }
    return true;
}
// drop read-ahead so the OS position equals the logical position
void sync_r(GFile* f) {
    if (f->rlen > f->rpos) fdtab::lseek(f->fd, -(s64)(f->rlen - f->rpos), 1);
    f->rpos = f->rlen = 0;
    f->unget.clear();
}
int getc_f(GFile* f) {
    if (!f->unget.empty()) { int c = f->unget.back(); f->unget.pop_back(); return c; }
    if (f->writing) { flush_w(f); f->writing = false; }
    if (f->rpos >= f->rlen) {
        if (f->rbuf.size() < 16384) f->rbuf.resize(16384);
        s64 n = fdtab::read(f->fd, f->rbuf.data(), f->rbuf.size());
        if (n <= 0) { if (n < 0) f->err = true; else f->eof = true; return -1; }
        f->rpos = 0;
        f->rlen = (size_t)n;
    }
    return f->rbuf[f->rpos++];
}
size_t read_f(GFile* f, u8* out, size_t n) {
    size_t got = 0;
    while (got < n && !f->unget.empty()) { out[got++] = f->unget.back(); f->unget.pop_back(); }
    if (f->writing) { flush_w(f); f->writing = false; }
    size_t avail = f->rlen - f->rpos;
    if (avail) {
        size_t k = std::min(avail, n - got);
        memcpy(out + got, f->rbuf.data() + f->rpos, k);
        f->rpos += k;
        got += k;
    }
    while (got < n) {
        if (n - got >= 16384) {
            s64 r = fdtab::read(f->fd, out + got, n - got);
            if (r <= 0) { if (r < 0) f->err = true; else f->eof = true; break; }
            got += (size_t)r;
        } else {
            int c = getc_f(f);
            if (c < 0) break;
            out[got++] = (u8)c;
            size_t k = std::min(f->rlen - f->rpos, n - got);
            memcpy(out + got, f->rbuf.data() + f->rpos, k);
            f->rpos += k;
            got += k;
        }
    }
    return got;
}
size_t write_f(GFile* f, const u8* p, size_t n) {
    if (!f->writing) { sync_r(f); f->writing = true; }
    if (f == g_stdout || f == g_stderr) {
        s64 r = fdtab::write(f->fd, p, n);
        return r < 0 ? 0 : (size_t)r;
    }
    f->wbuf.insert(f->wbuf.end(), p, p + n);
    if (f->wbuf.size() >= 65536) flush_w(f);
    return n;
}
void init_stdio() {
    static bool done = false;
    if (done) return;
    done = true;
    g_stdin = new_file(0);
    g_stdout = new_file(1);
    g_stderr = new_file(2);
    g_stdin->owns_fd = g_stdout->owns_fd = g_stderr->owns_fd = false;
}
GFile* parse_mode_open(const char* path, const char* mode) {
    int fl = 0;
    bool plus = strchr(mode, '+') != nullptr;
    switch (mode[0]) {
    case 'r': fl = plus ? DO_RDWR : DO_RDONLY; break;
    case 'w': fl = (plus ? DO_RDWR : DO_WRONLY) | DO_CREAT | DO_TRUNC; break;
    case 'a': fl = (plus ? DO_RDWR : DO_WRONLY) | DO_CREAT | DO_APPEND; break;
    default: set_errno(D_EINVAL); return nullptr;
    }
    if (strchr(mode, 'x')) fl |= DO_EXCL;
    int fd = fdtab::open(path, fl, 0666);
    if (fd < 0) return nullptr;
    return new_file(fd);
}
}  // namespace

HLE_DATA(__stdinp, 8) { init_stdio(); *static_cast<GFile**>(p) = g_stdin; }
HLE_DATA(__stdoutp, 8) { init_stdio(); *static_cast<GFile**>(p) = g_stdout; }
HLE_DATA(__stderrp, 8) { init_stdio(); *static_cast<GFile**>(p) = g_stderr; }

HLE_FUNC(fopen, GFile*, (const char* p, const char* m)) {
    GFile* f = parse_mode_open(p, m);
    LOGD("fopen(%s, %s) -> %p", p, m, (void*)f);
    return f;
}
HLE_ALIAS("_fopen$DARWIN_EXTSN", hle_impl_fopen);
HLE_FUNC(fdopen, GFile*, (int fd, const char*)) { return fdtab::get(fd) ? new_file(fd) : nullptr; }
HLE_FUNC(freopen, GFile*, (const char* p, const char* m, GFile* f)) {
    if (!f) return nullptr;
    std::lock_guard lk(f->lock);
    flush_w(f);
    if (f->owns_fd) fdtab::close(f->fd);
    GFile* n = parse_mode_open(p, m);
    if (!n) return nullptr;
    f->fd = n->fd;
    f->eof = f->err = false;
    f->rpos = f->rlen = 0;
    f->owns_fd = true;
    n->owns_fd = false;
    return f;
}
HLE_FUNC(fclose, int, (GFile* f)) {
    if (!f) return -1;
    {
        std::lock_guard lk(f->lock);
        flush_w(f);
        if (f->owns_fd) fdtab::close(f->fd);
    }
    if (f == g_stdin || f == g_stdout || f == g_stderr) return 0;
    f->~GFile();
    vmem::gfree(f);
    return 0;
}
HLE_FUNC(fread, size_t, (void* buf, size_t sz, size_t n, GFile* f)) {
    if (!sz || !n) return 0;
    std::lock_guard lk(f->lock);
    return read_f(f, static_cast<u8*>(buf), sz * n) / sz;
}
HLE_FUNC(fwrite, size_t, (const void* buf, size_t sz, size_t n, GFile* f)) {
    if (!sz || !n) return 0;
    std::lock_guard lk(f->lock);
    return write_f(f, static_cast<const u8*>(buf), sz * n) / sz;
}
HLE_FUNC(fflush, int, (GFile* f)) {
    if (!f) return 0;
    std::lock_guard lk(f->lock);
    return flush_w(f) ? 0 : -1;
}
HLE_FUNC(fseeko, int, (GFile* f, s64 off, int wh)) {
    std::lock_guard lk(f->lock);
    flush_w(f);
    f->writing = false;
    if (wh == 1) off -= (s64)(f->rlen - f->rpos) + (s64)f->unget.size();
    f->rpos = f->rlen = 0;
    f->unget.clear();
    f->eof = false;
    return fdtab::lseek(f->fd, off, wh) < 0 ? -1 : 0;
}
HLE_FUNC(fseek, int, (GFile* f, s64 off, int wh)) { return hle_impl_fseeko(f, off, wh); }
HLE_FUNC(ftello, s64, (GFile* f)) {
    std::lock_guard lk(f->lock);
    s64 pos = fdtab::lseek(f->fd, 0, 1);
    if (pos < 0) return -1;
    if (f->writing) return pos + (s64)f->wbuf.size();
    return pos - (s64)(f->rlen - f->rpos) - (s64)f->unget.size();
}
HLE_FUNC(ftell, s64, (GFile* f)) { return hle_impl_ftello(f); }
HLE_FUNC(rewind, void, (GFile* f)) { hle_impl_fseeko(f, 0, 0); f->err = false; }
HLE_FUNC(fgetpos, int, (GFile* f, s64* pos)) { *pos = hle_impl_ftello(f); return *pos < 0 ? -1 : 0; }
HLE_FUNC(fsetpos, int, (GFile* f, const s64* pos)) { return hle_impl_fseeko(f, *pos, 0); }
HLE_FUNC(feof, int, (GFile* f)) { return f->eof ? 1 : 0; }
HLE_FUNC(ferror, int, (GFile* f)) { return f->err ? 1 : 0; }
HLE_FUNC(clearerr, void, (GFile* f)) { f->eof = f->err = false; }
HLE_FUNC(fileno, int, (GFile* f)) { return f->fd; }
HLE_FUNC(setbuf, void, (GFile*, char*)) {}
HLE_FUNC(setvbuf, int, (GFile*, char*, int, size_t)) { return 0; }
HLE_FUNC(setlinebuf, int, (GFile*)) { return 0; }
HLE_FUNC(flockfile, void, (GFile* f)) { f->lock.lock(); }
HLE_FUNC(funlockfile, void, (GFile* f)) { f->lock.unlock(); }
HLE_FUNC(fgetc, int, (GFile* f)) { std::lock_guard lk(f->lock); return getc_f(f); }
HLE_FUNC(getc, int, (GFile* f)) { std::lock_guard lk(f->lock); return getc_f(f); }
HLE_FUNC(getc_unlocked, int, (GFile* f)) { return getc_f(f); }
HLE_FUNC(getchar, int, ()) { return -1; }
HLE_FUNC(ungetc, int, (int ch, GFile* f)) {
    if (ch < 0) return -1;
    std::lock_guard lk(f->lock);
    f->unget.push_back((u8)ch);
    f->eof = false;
    return ch;
}
HLE_FUNC(fgets, char*, (char* buf, int n, GFile* f)) {
    if (n <= 0) return nullptr;
    std::lock_guard lk(f->lock);
    int i = 0;
    while (i < n - 1) {
        int ch = getc_f(f);
        if (ch < 0) break;
        buf[i++] = (char)ch;
        if (ch == '\n') break;
    }
    if (!i) return nullptr;
    buf[i] = 0;
    return buf;
}
HLE_FUNC(getline, s64, (char** lp, size_t* cap, GFile* f)) {
    std::lock_guard lk(f->lock);
    std::string s;
    for (;;) {
        int ch = getc_f(f);
        if (ch < 0) break;
        s += (char)ch;
        if (ch == '\n') break;
    }
    if (s.empty()) return -1;
    if (!*lp || *cap < s.size() + 1) { *lp = static_cast<char*>(vmem::grealloc(*lp, s.size() + 1)); *cap = s.size() + 1; }
    memcpy(*lp, s.c_str(), s.size() + 1);
    return (s64)s.size();
}
HLE_FUNC(fputc, int, (int ch, GFile* f)) {
    std::lock_guard lk(f->lock);
    u8 b = (u8)ch;
    return write_f(f, &b, 1) == 1 ? (ch & 0xff) : -1;
}
HLE_FUNC(putc, int, (int ch, GFile* f)) { return hle_impl_fputc(ch, f); }
HLE_FUNC(putchar, int, (int ch)) { init_stdio(); return hle_impl_fputc(ch, g_stdout); }
HLE_FUNC(fputs, int, (const char* s, GFile* f)) {
    std::lock_guard lk(f->lock);
    size_t n = strlen(s);
    return write_f(f, reinterpret_cast<const u8*>(s), n) == n ? 0 : -1;
}
HLE_FUNC(puts, int, (const char* s)) {
    init_stdio();
    hle_impl_fputs(s, g_stdout);
    return hle_impl_fputc('\n', g_stdout);
}
HLE_FUNC(fputwc, u32, (u32 ch, GFile* f)) {
    char32_t c32 = ch;
    std::string u = gfmt::utf32_to_utf8(&c32, 1);
    std::lock_guard lk(f->lock);
    write_f(f, reinterpret_cast<const u8*>(u.data()), u.size());
    return ch;
}
HLE_FUNC(getwc, u32, (GFile* f)) { int c = hle_impl_getc(f); return c < 0 ? 0xffffffffu : (u32)c; }
HLE_FUNC(fgetwc, u32, (GFile* f)) { int c = hle_impl_getc(f); return c < 0 ? 0xffffffffu : (u32)c; }
HLE_FUNC(ungetwc, u32, (u32 ch, GFile* f)) { hle_impl_ungetc((int)ch, f); return ch; }

// ---- printf family (Apple arm64: variadic args on the stack) ----
static void out_str(GFile* f, const std::string& s) {
    std::lock_guard lk(f->lock);
    write_f(f, reinterpret_cast<const u8*>(s.data()), s.size());
}
static s64 put_buf(char* buf, size_t n, const std::string& s) {
    if (n) {
        size_t k = std::min(s.size(), n - 1);
        memcpy(buf, s.data(), k);
        buf[k] = 0;
    }
    return (s64)s.size();
}
HLE_RAW(printf) { init_stdio(); hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(0)), va); out_str(g_stdout, s); c.set_x(0, s.size()); }
HLE_RAW(vprintf) { init_stdio(); hle::VaReader va(c.x(1)); auto s = gfmt::format(gptr<const char>(c.x(0)), va); out_str(g_stdout, s); c.set_x(0, s.size()); }
HLE_RAW(fprintf) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(1)), va); out_str(gptr<GFile>(c.x(0)), s); c.set_x(0, s.size()); }
HLE_RAW(vfprintf) { hle::VaReader va(c.x(2)); auto s = gfmt::format(gptr<const char>(c.x(1)), va); out_str(gptr<GFile>(c.x(0)), s); c.set_x(0, s.size()); }
HLE_RAW(dprintf) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(1)), va); fdtab::write((int)c.x(0), s.data(), s.size()); c.set_x(0, s.size()); }
HLE_RAW(sprintf) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(1)), va); memcpy(gptr(c.x(0)), s.c_str(), s.size() + 1); c.set_x(0, s.size()); }
HLE_RAW(vsprintf) { hle::VaReader va(c.x(2)); auto s = gfmt::format(gptr<const char>(c.x(1)), va); memcpy(gptr(c.x(0)), s.c_str(), s.size() + 1); c.set_x(0, s.size()); }
HLE_RAW(snprintf) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(2)), va); c.set_x(0, (u64)put_buf(gptr<char>(c.x(0)), c.x(1), s)); }
HLE_RAW(vsnprintf) { hle::VaReader va(c.x(3)); auto s = gfmt::format(gptr<const char>(c.x(2)), va); c.set_x(0, (u64)put_buf(gptr<char>(c.x(0)), c.x(1), s)); }
HLE_RAW(__snprintf_chk) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(4)), va); c.set_x(0, (u64)put_buf(gptr<char>(c.x(0)), c.x(1), s)); }
HLE_RAW(__sprintf_chk) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(3)), va); memcpy(gptr(c.x(0)), s.c_str(), s.size() + 1); c.set_x(0, s.size()); }
HLE_RAW(__vsnprintf_chk) { hle::VaReader va(c.x(5)); auto s = gfmt::format(gptr<const char>(c.x(4)), va); c.set_x(0, (u64)put_buf(gptr<char>(c.x(0)), c.x(1), s)); }
HLE_RAW(snprintf_l) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(3)), va); c.set_x(0, (u64)put_buf(gptr<char>(c.x(0)), c.x(1), s)); }
HLE_RAW(vsnprintf_l) { hle::VaReader va(c.x(4)); auto s = gfmt::format(gptr<const char>(c.x(3)), va); c.set_x(0, (u64)put_buf(gptr<char>(c.x(0)), c.x(1), s)); }
HLE_RAW(sprintf_l) { hle::VaReader va(c.sp()); auto s = gfmt::format(gptr<const char>(c.x(2)), va); memcpy(gptr(c.x(0)), s.c_str(), s.size() + 1); c.set_x(0, s.size()); }
static void do_asprintf(Cpu& c, u64 out, const std::string& s) {
    char* p = static_cast<char*>(vmem::gmalloc(s.size() + 1));
    memcpy(p, s.c_str(), s.size() + 1);
    *gptr<char*>(out) = p;
    c.set_x(0, s.size());
}
HLE_RAW(asprintf) { hle::VaReader va(c.sp()); do_asprintf(c, c.x(0), gfmt::format(gptr<const char>(c.x(1)), va)); }
HLE_RAW(vasprintf) { hle::VaReader va(c.x(2)); do_asprintf(c, c.x(0), gfmt::format(gptr<const char>(c.x(1)), va)); }
HLE_RAW(asprintf_l) { hle::VaReader va(c.sp()); do_asprintf(c, c.x(0), gfmt::format(gptr<const char>(c.x(2)), va)); }

// ---- scanf family ----
HLE_RAW(sscanf) { hle::VaReader va(c.sp()); c.set_x(0, (u64)(s64)gfmt::scan(gptr<const char>(c.x(0)), gptr<const char>(c.x(1)), va)); }
HLE_RAW(vsscanf) { hle::VaReader va(c.x(2)); c.set_x(0, (u64)(s64)gfmt::scan(gptr<const char>(c.x(0)), gptr<const char>(c.x(1)), va)); }
HLE_RAW(sscanf_l) { hle::VaReader va(c.sp()); c.set_x(0, (u64)(s64)gfmt::scan(gptr<const char>(c.x(0)), gptr<const char>(c.x(2)), va)); }
HLE_RAW(fscanf) {
    // Read the rest of the line(s) needed: simplest approach reads one line at a time
    GFile* f = gptr<GFile>(c.x(0));
    std::lock_guard lk(f->lock);
    std::string line;
    int ch;
    while ((ch = getc_f(f)) >= 0) { line += (char)ch; if (ch == '\n') break; }
    if (line.empty()) { c.set_x(0, (u64)-1); return; }
    hle::VaReader va(c.sp());
    size_t used = 0;
    int r = gfmt::scan(line.c_str(), gptr<const char>(c.x(1)), va, &used);
    for (size_t i = line.size(); i > used; i--) f->unget.push_back((u8)line[i - 1]);
    c.set_x(0, (u64)(s64)r);
}
