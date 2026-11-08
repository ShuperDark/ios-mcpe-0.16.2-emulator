// libSystem: memory, strings, ctype, stdlib, wide chars, locale, math, time.
#include "gfmt.h"
#include "hle_common.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>

#pragma comment(lib, "bcrypt.lib")

using namespace hlec;

// ===================================================================== errno
namespace hlec {
int map_crt_errno(int e) {
    switch (e) {
    case 0: return 0;
    case EAGAIN: return D_EAGAIN;
    case EDEADLK: return D_EDEADLK;
    case ENAMETOOLONG: return D_ENAMETOOLONG;
    case ENOLCK: return D_ENOLCK;
    case ENOSYS: return D_ENOSYS;
    case ENOTEMPTY: return D_ENOTEMPTY;
    case EILSEQ: return D_EILSEQ;
    case EADDRINUSE: return D_EADDRINUSE;
    case EADDRNOTAVAIL: return D_EADDRNOTAVAIL;
    case EAFNOSUPPORT: return D_EAFNOSUPPORT;
    case EALREADY: return D_EALREADY;
    case ECANCELED: return D_ECANCELED;
    case ECONNABORTED: return D_ECONNABORTED;
    case ECONNREFUSED: return D_ECONNREFUSED;
    case ECONNRESET: return D_ECONNRESET;
    case EHOSTUNREACH: return D_EHOSTUNREACH;
    case EINPROGRESS: return D_EINPROGRESS;
    case EISCONN: return D_EISCONN;
    case EMSGSIZE: return D_EMSGSIZE;
    case ENETUNREACH: return D_ENETUNREACH;
    case ENOBUFS: return D_ENOBUFS;
    case ENOTCONN: return D_ENOTCONN;
    case ENOTSOCK: return D_ENOTSOCK;
    case ENOTSUP: return D_ENOTSUP;
    case EOPNOTSUPP: return D_EOPNOTSUPP;
    case EOVERFLOW: return D_EOVERFLOW;
    case ETIMEDOUT: return D_ETIMEDOUT;
    case EWOULDBLOCK: return D_EAGAIN;
    case ENOPROTOOPT: return D_ENOPROTOOPT;
    case EPROTONOSUPPORT: return D_EPROTONOSUPPORT;
    default: return e <= 34 ? e : D_EINVAL;
    }
}

int map_win32_error(unsigned long e) {
    switch (e) {
    case ERROR_SUCCESS: return 0;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_BAD_NETPATH: return D_ENOENT;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION: return D_EACCES;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS: return D_EEXIST;
    case ERROR_DIR_NOT_EMPTY: return D_ENOTEMPTY;
    case ERROR_DIRECTORY: return D_ENOTDIR;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY: return D_ENOMEM;
    case ERROR_DISK_FULL: return D_ENOSPC;
    case ERROR_INVALID_HANDLE: return D_EBADF;
    case ERROR_FILENAME_EXCED_RANGE: return D_ENAMETOOLONG;
    default: return D_EIO;
    }
}

void set_errno(int e) {
    if (auto* t = threads::current()) t->errno_val = e;
}
int get_errno() {
    auto* t = threads::current();
    return t ? t->errno_val : 0;
}

const char* intern(const std::string& s) {
    static std::mutex m;
    static std::unordered_set<std::string> pool;
    std::lock_guard lk(m);
    return pool.insert(s).first->c_str();
}
}  // namespace hlec

HLE_FUNC(__error, int*, ()) {
    auto* t = threads::current();
    static int dummy;
    return t ? &t->errno_val : &dummy;
}

// ===================================================================== memory
HLE_FUNC(malloc, void*, (size_t n)) { return vmem::gmalloc(n); }
HLE_FUNC(calloc, void*, (size_t n, size_t s)) { return vmem::gcalloc(n, s); }
HLE_FUNC(realloc, void*, (void* p, size_t n)) { return vmem::grealloc(p, n); }
HLE_FUNC(reallocf, void*, (void* p, size_t n)) {
    void* r = vmem::grealloc(p, n);
    if (!r && n) vmem::gfree(p);
    return r;
}
HLE_FUNC(free, void, (void* p)) { vmem::gfree(p); }
HLE_FUNC(valloc, void*, (size_t n)) { return vmem::gmemalign(0x4000, n); }
HLE_FUNC(aligned_alloc, void*, (size_t a, size_t n)) { return vmem::gmemalign(a, n); }
HLE_FUNC(posix_memalign, int, (void** out, size_t a, size_t n)) {
    *out = vmem::gmemalign(a, n);
    return *out ? 0 : D_ENOMEM;
}
HLE_FUNC(malloc_size, size_t, (void* p)) { return vmem::gmsize(p); }
HLE_FUNC(malloc_good_size, size_t, (size_t n)) { return align_up(n, 16); }

HLE_FUNC(memcpy, void*, (void* d, const void* s, size_t n)) { return memmove(d, s, n); }
HLE_FUNC(memmove, void*, (void* d, const void* s, size_t n)) { return memmove(d, s, n); }
HLE_FUNC(memset, void*, (void* d, int c, size_t n)) { return memset(d, c, n); }
HLE_FUNC(__memcpy_chk, void*, (void* d, const void* s, size_t n, size_t)) { return memmove(d, s, n); }
HLE_FUNC(__memmove_chk, void*, (void* d, const void* s, size_t n, size_t)) { return memmove(d, s, n); }
HLE_FUNC(__memset_chk, void*, (void* d, int c, size_t n, size_t)) { return memset(d, c, n); }
HLE_FUNC(memcmp, int, (const void* a, const void* b, size_t n)) { return memcmp(a, b, n); }
HLE_FUNC(bcmp, int, (const void* a, const void* b, size_t n)) { return memcmp(a, b, n); }
HLE_FUNC(memchr, const void*, (const void* s, int c, size_t n)) { return memchr(s, c, n); }
HLE_FUNC(memrchr, const void*, (const void* s, int c, size_t n)) {
    const u8* p = static_cast<const u8*>(s);
    while (n--) if (p[n] == (u8)c) return p + n;
    return nullptr;
}
HLE_FUNC(bzero, void, (void* d, size_t n)) { memset(d, 0, n); }
HLE_FUNC(__bzero, void, (void* d, size_t n)) { memset(d, 0, n); }
HLE_FUNC(memset_pattern4, void, (void* d, const void* pat, size_t n)) {
    u8* p = static_cast<u8*>(d);
    for (size_t i = 0; i < n; i++) p[i] = static_cast<const u8*>(pat)[i & 3];
}
HLE_FUNC(memset_pattern8, void, (void* d, const void* pat, size_t n)) {
    u8* p = static_cast<u8*>(d);
    for (size_t i = 0; i < n; i++) p[i] = static_cast<const u8*>(pat)[i & 7];
}
HLE_FUNC(memset_pattern16, void, (void* d, const void* pat, size_t n)) {
    u8* p = static_cast<u8*>(d);
    for (size_t i = 0; i < n; i++) p[i] = static_cast<const u8*>(pat)[i & 15];
}

// ===================================================================== strings
HLE_FUNC(strlen, size_t, (const char* s)) { return strlen(s); }
HLE_FUNC(strnlen, size_t, (const char* s, size_t n)) { return strnlen(s, n); }
HLE_FUNC(strcmp, int, (const char* a, const char* b)) { return strcmp(a, b); }
HLE_FUNC(strncmp, int, (const char* a, const char* b, size_t n)) { return strncmp(a, b, n); }
HLE_FUNC(strcoll, int, (const char* a, const char* b)) { return strcmp(a, b); }
HLE_FUNC(strcoll_l, int, (const char* a, const char* b, void*)) { return strcmp(a, b); }
HLE_FUNC(strxfrm, size_t, (char* d, const char* s, size_t n)) {
    size_t l = strlen(s);
    if (n) { size_t c = std::min(l, n - 1); memcpy(d, s, c); d[c] = 0; }
    return l;
}
HLE_FUNC(strxfrm_l, size_t, (char* d, const char* s, size_t n, void*)) {
    size_t l = strlen(s);
    if (n) { size_t c = std::min(l, n - 1); memcpy(d, s, c); d[c] = 0; }
    return l;
}
HLE_FUNC(strcpy, char*, (char* d, const char* s)) { return strcpy(d, s); }
HLE_FUNC(stpcpy, char*, (char* d, const char* s)) { size_t n = strlen(s); memcpy(d, s, n + 1); return d + n; }
HLE_FUNC(strncpy, char*, (char* d, const char* s, size_t n)) { return strncpy(d, s, n); }
HLE_FUNC(strlcpy, size_t, (char* d, const char* s, size_t n)) {
    size_t l = strlen(s);
    if (n) { size_t c = std::min(l, n - 1); memcpy(d, s, c); d[c] = 0; }
    return l;
}
HLE_FUNC(strlcat, size_t, (char* d, const char* s, size_t n)) {
    size_t dl = strnlen(d, n), sl = strlen(s);
    if (dl == n) return n + sl;
    size_t c = std::min(sl, n - dl - 1);
    memcpy(d + dl, s, c);
    d[dl + c] = 0;
    return dl + sl;
}
HLE_FUNC(strcat, char*, (char* d, const char* s)) { return strcat(d, s); }
HLE_FUNC(__strcat_chk, char*, (char* d, const char* s, size_t)) { return strcat(d, s); }
HLE_FUNC(__strcpy_chk, char*, (char* d, const char* s, size_t)) { return strcpy(d, s); }
HLE_FUNC(__strncpy_chk, char*, (char* d, const char* s, size_t n, size_t)) { return strncpy(d, s, n); }
HLE_FUNC(__strncat_chk, char*, (char* d, const char* s, size_t n, size_t)) { return strncat(d, s, n); }
HLE_FUNC(strncat, char*, (char* d, const char* s, size_t n)) { return strncat(d, s, n); }
HLE_FUNC(strchr, const char*, (const char* s, int c)) { return strchr(s, c); }
HLE_FUNC(strrchr, const char*, (const char* s, int c)) { return strrchr(s, c); }
HLE_FUNC(index, const char*, (const char* s, int c)) { return strchr(s, c); }
HLE_FUNC(rindex, const char*, (const char* s, int c)) { return strrchr(s, c); }
HLE_FUNC(strstr, const char*, (const char* s, const char* n)) { return strstr(s, n); }
HLE_FUNC(strcasestr, const char*, (const char* s, const char* n)) {
    size_t nl = strlen(n);
    for (; *s; s++) if (_strnicmp(s, n, nl) == 0) return s;
    return nl ? nullptr : s;
}
HLE_FUNC(strnstr, const char*, (const char* s, const char* n, size_t len)) {
    size_t nl = strlen(n);
    if (!nl) return s;
    for (size_t i = 0; i + nl <= len && s[i]; i++) if (!strncmp(s + i, n, nl)) return s + i;
    return nullptr;
}
HLE_FUNC(strdup, char*, (const char* s)) {
    size_t n = strlen(s) + 1;
    char* r = static_cast<char*>(vmem::gmalloc(n));
    memcpy(r, s, n);
    return r;
}
HLE_FUNC(strndup, char*, (const char* s, size_t m)) {
    size_t n = strnlen(s, m);
    char* r = static_cast<char*>(vmem::gmalloc(n + 1));
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}
HLE_FUNC(strcasecmp, int, (const char* a, const char* b)) { return _stricmp(a, b); }
HLE_FUNC(strncasecmp, int, (const char* a, const char* b, size_t n)) { return _strnicmp(a, b, n); }
HLE_FUNC(strspn, size_t, (const char* s, const char* a)) { return strspn(s, a); }
HLE_FUNC(strcspn, size_t, (const char* s, const char* a)) { return strcspn(s, a); }
HLE_FUNC(strpbrk, const char*, (const char* s, const char* a)) { return strpbrk(s, a); }
HLE_FUNC(strsep, char*, (char** sp, const char* delim)) {
    char* s = *sp;
    if (!s) return nullptr;
    char* e = s + strcspn(s, delim);
    if (*e) { *e = 0; *sp = e + 1; } else *sp = nullptr;
    return s;
}
HLE_FUNC(strtok_r, char*, (char* s, const char* d, char** save)) { return strtok_s(s, d, save); }
HLE_FUNC(strtok, char*, (char* s, const char* d)) {
    static thread_local char* save;
    return strtok_s(s, d, &save);
}

static const char* darwin_strerror(int e) {
    switch (e) {
    case 0: return "Undefined error: 0";
    case D_EPERM: return "Operation not permitted";
    case D_ENOENT: return "No such file or directory";
    case D_EINTR: return "Interrupted system call";
    case D_EIO: return "Input/output error";
    case D_EBADF: return "Bad file descriptor";
    case D_ENOMEM: return "Cannot allocate memory";
    case D_EACCES: return "Permission denied";
    case D_EEXIST: return "File exists";
    case D_ENOTDIR: return "Not a directory";
    case D_EISDIR: return "Is a directory";
    case D_EINVAL: return "Invalid argument";
    case D_ENOSPC: return "No space left on device";
    case D_EAGAIN: return "Resource temporarily unavailable";
    case D_EINPROGRESS: return "Operation now in progress";
    case D_ETIMEDOUT: return "Operation timed out";
    case D_ECONNREFUSED: return "Connection refused";
    case D_ENOTEMPTY: return "Directory not empty";
    case D_ENOSYS: return "Function not implemented";
    case D_ENOTSUP: return "Operation not supported";
    default: return intern("Unknown error: " + std::to_string(e));
    }
}
HLE_FUNC(strerror, const char*, (int e)) { return darwin_strerror(e); }
HLE_FUNC(strerror_r, int, (int e, char* buf, size_t n)) {
    if (n) { strncpy(buf, darwin_strerror(e), n - 1); buf[n - 1] = 0; }
    return 0;
}
HLE_FUNC(perror, void, (const char* s)) {
    fprintf(stderr, "%s%s%s\n", s ? s : "", s && *s ? ": " : "", darwin_strerror(get_errno()));
}

// ===================================================================== ctype
namespace {
enum : u32 {
    CT_A = 0x100, CT_C = 0x200, CT_D = 0x400, CT_G = 0x800, CT_L = 0x1000, CT_P = 0x2000, CT_S = 0x4000,
    CT_U = 0x8000, CT_X = 0x10000, CT_B = 0x20000, CT_R = 0x40000, CT_SW1 = 0x40000000,
};
u32 rune_type(u32 c) {
    if (c >= 128) {
        // treat non-ASCII code points as printable letters (good enough for UTF-8 text)
        return c < 0x110000 ? (CT_A | CT_G | CT_R | CT_SW1) : 0;
    }
    u32 t = 0;
    if (isalpha((int)c)) t |= CT_A;
    if (iscntrl((int)c)) t |= CT_C;
    if (isdigit((int)c)) t |= CT_D | (c - '0');  // digit value in low bits
    if (isgraph((int)c)) t |= CT_G;
    if (islower((int)c)) t |= CT_L;
    if (ispunct((int)c)) t |= CT_P;
    if (isspace((int)c)) t |= CT_S;
    if (isupper((int)c)) t |= CT_U;
    if (isxdigit((int)c)) {
        t |= CT_X;
        if (!isdigit((int)c)) t |= (tolower((int)c) - 'a' + 10);
    }
    if (c == ' ' || c == '\t') t |= CT_B;
    if (isprint((int)c)) t |= CT_R | CT_SW1;
    return t;
}
}  // namespace

HLE_DATA(_DefaultRuneLocale, 4096) {
    u8* b = static_cast<u8*>(p);
    memcpy(b, "RuneMagA", 8);
    strcpy(reinterpret_cast<char*>(b + 8), "NONE");
    *reinterpret_cast<s32*>(b + 56) = 0xfffd;
    u32* runetype = reinterpret_cast<u32*>(b + 60);
    s32* maplower = reinterpret_cast<s32*>(b + 60 + 1024);
    s32* mapupper = reinterpret_cast<s32*>(b + 60 + 2048);
    for (int c = 0; c < 256; c++) {
        runetype[c] = c < 128 ? rune_type(c) : 0;
        maplower[c] = c < 128 ? tolower(c) : c;
        mapupper[c] = c < 128 ? toupper(c) : c;
    }
}
HLE_DATA(_CurrentRuneLocale, 8) {
    // filled in after HLE init would be ideal; libc++ does not read it
}

HLE_FUNC(__maskrune, u32, (s32 c, u32 mask)) { return c < 0 ? 0 : rune_type((u32)c) & mask; }
HLE_FUNC(__maskrune_l, u32, (s32 c, u32 mask, void*)) { return c < 0 ? 0 : rune_type((u32)c) & mask; }
HLE_FUNC(__istype, u32, (s32 c, u32 mask)) { return c < 0 ? 0 : rune_type((u32)c) & mask; }
HLE_FUNC(__tolower, s32, (s32 c)) { return c >= 0 && c < 128 ? tolower(c) : c; }
HLE_FUNC(__toupper, s32, (s32 c)) { return c >= 0 && c < 128 ? toupper(c) : c; }
HLE_FUNC(___tolower_l, s32, (s32 c, void*)) { return c >= 0 && c < 128 ? tolower(c) : c; }
HLE_FUNC(___toupper_l, s32, (s32 c, void*)) { return c >= 0 && c < 128 ? toupper(c) : c; }
HLE_FUNC(___mb_cur_max, int, ()) { return 1; }
HLE_FUNC(___mb_cur_max_l, int, (void*)) { return 1; }
HLE_FUNC(tolower, int, (int c)) { return c >= 0 && c < 128 ? tolower(c) : c; }
HLE_FUNC(toupper, int, (int c)) { return c >= 0 && c < 128 ? toupper(c) : c; }
HLE_FUNC(isalpha, int, (int c)) { return c >= 0 && c < 128 && isalpha(c); }
HLE_FUNC(isdigit, int, (int c)) { return c >= 0 && c < 128 && isdigit(c); }
HLE_FUNC(isalnum, int, (int c)) { return c >= 0 && c < 128 && isalnum(c); }
HLE_FUNC(isspace, int, (int c)) { return c >= 0 && c < 128 && isspace(c); }
HLE_FUNC(isupper, int, (int c)) { return c >= 0 && c < 128 && isupper(c); }
HLE_FUNC(islower, int, (int c)) { return c >= 0 && c < 128 && islower(c); }
HLE_FUNC(isxdigit, int, (int c)) { return c >= 0 && c < 128 && isxdigit(c); }
HLE_FUNC(isprint, int, (int c)) { return c >= 0 && c < 128 && isprint(c); }
HLE_FUNC(ispunct, int, (int c)) { return c >= 0 && c < 128 && ispunct(c); }
HLE_FUNC(iswspace, int, (u32 c)) { return c < 128 && isspace((int)c); }
HLE_FUNC(iswalpha, int, (u32 c)) { return c >= 128 || isalpha((int)c); }
HLE_FUNC(iswdigit, int, (u32 c)) { return c < 128 && isdigit((int)c); }
HLE_FUNC(towlower, u32, (u32 c)) { return c < 128 ? (u32)tolower((int)c) : c; }
HLE_FUNC(towupper, u32, (u32 c)) { return c < 128 ? (u32)toupper((int)c) : c; }

// ===================================================================== locale
namespace {
struct DLconv {
    u64 decimal_point, thousands_sep, grouping, int_curr_symbol, currency_symbol, mon_decimal_point,
        mon_thousands_sep, mon_grouping, positive_sign, negative_sign;
    char fields[14];
};
DLconv* c_lconv() {
    static DLconv* lc = [] {
        auto* l = static_cast<DLconv*>(vmem::gcalloc(1, sizeof(DLconv)));
        static const char dot[] = ".", empty[] = "";
        l->decimal_point = gaddr_of(dot);
        l->thousands_sep = l->grouping = l->int_curr_symbol = l->currency_symbol = l->mon_decimal_point =
            l->mon_thousands_sep = l->mon_grouping = l->positive_sign = l->negative_sign = gaddr_of(empty);
        memset(l->fields, 127, sizeof l->fields);
        return l;
    }();
    return lc;
}
char g_fake_locale[64];
}  // namespace
HLE_FUNC(setlocale, const char*, (int, const char*)) { return "C"; }
HLE_FUNC(newlocale, void*, (int, const char*, void*)) { return g_fake_locale; }
HLE_FUNC(duplocale, void*, (void*)) { return g_fake_locale; }
HLE_FUNC(freelocale, int, (void*)) { return 0; }
HLE_FUNC(uselocale, void*, (void*)) { return g_fake_locale; }
HLE_FUNC(querylocale, const char*, (int, void*)) { return "C"; }
HLE_FUNC(localeconv, void*, ()) { return c_lconv(); }
HLE_FUNC(localeconv_l, void*, (void*)) { return c_lconv(); }
HLE_FUNC(catopen, s64, (const char*, int)) { return -1; }
HLE_FUNC(catgets, const char*, (s64, int, int, const char* s)) { return s; }
HLE_FUNC(catclose, int, (s64)) { return 0; }
HLE_FUNC(nl_langinfo, const char*, (int)) { return "UTF-8"; }

// ===================================================================== stdlib
HLE_FUNC(atoi, int, (const char* s)) { return atoi(s); }
HLE_FUNC(atol, s64, (const char* s)) { return _atoi64(s); }
HLE_FUNC(atoll, s64, (const char* s)) { return _atoi64(s); }
HLE_FUNC(atof, double, (const char* s)) { return strtod(s, nullptr); }
static void erange() { if (errno == ERANGE) set_errno(D_ERANGE); }
HLE_FUNC(strtol, s64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoll(s, e, b); erange(); return r; }
HLE_FUNC(strtoll, s64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoll(s, e, b); erange(); return r; }
HLE_FUNC(strtoq, s64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoll(s, e, b); erange(); return r; }
HLE_FUNC(strtoimax, s64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoll(s, e, b); erange(); return r; }
HLE_FUNC(strtoul, u64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoull(s, e, b); erange(); return r; }
HLE_FUNC(strtoull, u64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoull(s, e, b); erange(); return r; }
HLE_FUNC(strtouq, u64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoull(s, e, b); erange(); return r; }
HLE_FUNC(strtoumax, u64, (const char* s, char** e, int b)) { errno = 0; auto r = strtoull(s, e, b); erange(); return r; }
HLE_FUNC(strtoll_l, s64, (const char* s, char** e, int b, void*)) { errno = 0; auto r = strtoll(s, e, b); erange(); return r; }
HLE_FUNC(strtoull_l, u64, (const char* s, char** e, int b, void*)) { errno = 0; auto r = strtoull(s, e, b); erange(); return r; }
HLE_FUNC(strtod, double, (const char* s, char** e)) { errno = 0; auto r = strtod(s, e); erange(); return r; }
HLE_FUNC(strtof, float, (const char* s, char** e)) { errno = 0; auto r = strtof(s, e); erange(); return r; }
HLE_FUNC(strtold, double, (const char* s, char** e)) { errno = 0; auto r = strtod(s, e); erange(); return r; }
HLE_FUNC(strtod_l, double, (const char* s, char** e, void*)) { errno = 0; auto r = strtod(s, e); erange(); return r; }
HLE_FUNC(strtof_l, float, (const char* s, char** e, void*)) { errno = 0; auto r = strtof(s, e); erange(); return r; }
HLE_FUNC(strtold_l, double, (const char* s, char** e, void*)) { errno = 0; auto r = strtod(s, e); erange(); return r; }
HLE_FUNC(abs, int, (int v)) { return v < 0 ? -v : v; }
HLE_FUNC(labs, s64, (s64 v)) { return v < 0 ? -v : v; }
HLE_FUNC(llabs, s64, (s64 v)) { return v < 0 ? -v : v; }

static std::mutex g_rand_lock;
static u32 g_rand_state = 1;
HLE_FUNC(rand, int, ()) {
    std::lock_guard lk(g_rand_lock);
    g_rand_state = g_rand_state * 1103515245 + 12345;
    return int((g_rand_state >> 1) & 0x7fffffff);
}
HLE_FUNC(srand, void, (u32 s)) { std::lock_guard lk(g_rand_lock); g_rand_state = s; }
HLE_FUNC(random, s64, ()) { std::lock_guard lk(g_rand_lock); g_rand_state = g_rand_state * 1103515245 + 12345; return (g_rand_state >> 1) & 0x7fffffff; }
HLE_FUNC(srandom, void, (u32 s)) { std::lock_guard lk(g_rand_lock); g_rand_state = s; }
HLE_FUNC(arc4random_buf, void, (void* buf, size_t n)) { BCryptGenRandom(nullptr, (PUCHAR)buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG); }
HLE_FUNC(arc4random, u32, ()) {
    u32 v;
    BCryptGenRandom(nullptr, (PUCHAR)&v, 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v;
}
HLE_FUNC(arc4random_uniform, u32, (u32 n)) {
    if (n < 2) return 0;
    u32 v;
    BCryptGenRandom(nullptr, (PUCHAR)&v, 4, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return v % n;
}
HLE_FUNC(SecRandomCopyBytes, int, (void*, size_t n, void* buf)) {
    BCryptGenRandom(nullptr, (PUCHAR)buf, (ULONG)n, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return 0;
}
HLE_DATA(kSecRandomDefault, 8) {}

HLE_FUNC_R(qsort, void, (void* base, size_t n, size_t sz, gaddr cmp)) {
    if (n < 2 || !sz) return;
    std::vector<u8> copy(static_cast<u8*>(base), static_cast<u8*>(base) + n * sz);
    std::vector<size_t> idx(n);
    for (size_t i = 0; i < n; i++) idx[i] = i;
    Cpu* c = g_cpu;
    // Elements are compared at their copy locations (guest-visible memory).
    u8* tmp = static_cast<u8*>(vmem::gmalloc(n * sz));
    memcpy(tmp, copy.data(), n * sz);
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return (s32)c->call(cmp, {gaddr_of(tmp + a * sz), gaddr_of(tmp + b * sz)}) < 0;
    });
    for (size_t i = 0; i < n; i++) memcpy(static_cast<u8*>(base) + i * sz, tmp + idx[i] * sz, sz);
    vmem::gfree(tmp);
}
HLE_FUNC_R(bsearch, void*, (const void* key, const void* base, size_t n, size_t sz, gaddr cmp)) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const u8* e = static_cast<const u8*>(base) + mid * sz;
        s32 r = (s32)g_cpu->call(cmp, {gaddr_of(key), gaddr_of(e)});
        if (r == 0) return const_cast<u8*>(e);
        if (r < 0) hi = mid; else lo = mid + 1;
    }
    return nullptr;
}

// ===================================================================== wide chars (32-bit wchar_t)
using wc32 = char32_t;
HLE_FUNC(wcslen, size_t, (const wc32* s)) { return gfmt::u32len(s); }
HLE_FUNC(wcsnlen, size_t, (const wc32* s, size_t m)) { size_t n = 0; while (n < m && s[n]) n++; return n; }
HLE_FUNC(wmemchr, const wc32*, (const wc32* s, wc32 c, size_t n)) {
    for (size_t i = 0; i < n; i++) if (s[i] == c) return s + i;
    return nullptr;
}
HLE_FUNC(wmemcmp, int, (const wc32* a, const wc32* b, size_t n)) {
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return (s32)a[i] < (s32)b[i] ? -1 : 1;
    return 0;
}
HLE_FUNC(wmemcpy, wc32*, (wc32* d, const wc32* s, size_t n)) { memmove(d, s, n * 4); return d; }
HLE_FUNC(wmemmove, wc32*, (wc32* d, const wc32* s, size_t n)) { memmove(d, s, n * 4); return d; }
HLE_FUNC(wmemset, wc32*, (wc32* d, wc32 c, size_t n)) { for (size_t i = 0; i < n; i++) d[i] = c; return d; }
HLE_FUNC(wcscmp, int, (const wc32* a, const wc32* b)) {
    while (*a && *a == *b) { a++; b++; }
    return (s32)*a < (s32)*b ? -1 : (*a != *b);
}
HLE_FUNC(wcscoll_l, int, (const wc32* a, const wc32* b, void*)) {
    while (*a && *a == *b) { a++; b++; }
    return (s32)*a < (s32)*b ? -1 : (*a != *b);
}
HLE_FUNC(wcsxfrm_l, size_t, (wc32* d, const wc32* s, size_t n, void*)) {
    size_t l = gfmt::u32len(s);
    if (n) { size_t c = std::min(l, n - 1); memcpy(d, s, c * 4); d[c] = 0; }
    return l;
}
template <class T, class F>
static T wconv(const wc32* s, wc32** end, F f) {
    std::string u = gfmt::utf32_to_utf8(s);
    char* e = nullptr;
    T r = f(u.c_str(), &e);
    if (end) {
        // map the consumed UTF-8 byte count back to code points (ASCII digits only)
        *end = const_cast<wc32*>(s + (e - u.c_str()));
    }
    return r;
}
HLE_FUNC(wcstol, s64, (const wc32* s, wc32** e, int b)) { return wconv<s64>(s, e, [b](const char* p, char** q) { return strtoll(p, q, b); }); }
HLE_FUNC(wcstoll, s64, (const wc32* s, wc32** e, int b)) { return wconv<s64>(s, e, [b](const char* p, char** q) { return strtoll(p, q, b); }); }
HLE_FUNC(wcstoul, u64, (const wc32* s, wc32** e, int b)) { return wconv<u64>(s, e, [b](const char* p, char** q) { return strtoull(p, q, b); }); }
HLE_FUNC(wcstoull, u64, (const wc32* s, wc32** e, int b)) { return wconv<u64>(s, e, [b](const char* p, char** q) { return strtoull(p, q, b); }); }
HLE_FUNC(wcstod, double, (const wc32* s, wc32** e)) { return wconv<double>(s, e, [](const char* p, char** q) { return strtod(p, q); }); }
HLE_FUNC(wcstof, float, (const wc32* s, wc32** e)) { return wconv<float>(s, e, [](const char* p, char** q) { return strtof(p, q); }); }
HLE_FUNC(wcstold, double, (const wc32* s, wc32** e)) { return wconv<double>(s, e, [](const char* p, char** q) { return strtod(p, q); }); }

// multibyte <-> wide (UTF-8, mbstate ignored)
static size_t utf8_decode(const char* s, size_t n, wc32* out) {
    if (!s || !n) return (size_t)-2;
    const u8* p = reinterpret_cast<const u8*>(s);
    u32 c = p[0];
    size_t len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : (c & 0xf8) == 0xf0 ? 4 : 0;
    if (!len) return (size_t)-1;
    if (n < len) return (size_t)-2;
    u32 cp = len == 1 ? c : len == 2 ? c & 0x1f : len == 3 ? c & 0x0f : c & 0x07;
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xc0) != 0x80) return (size_t)-1;
        cp = (cp << 6) | (p[i] & 0x3f);
    }
    if (out) *out = cp;
    return cp ? len : 0;
}
static size_t utf8_encode(wc32 c, char* out) {
    std::string s = gfmt::utf32_to_utf8(&c, 1);
    if (out) memcpy(out, s.data(), s.size());
    return s.size();
}
HLE_FUNC(mbrtowc, size_t, (wc32* pwc, const char* s, size_t n, void*)) { return s ? utf8_decode(s, n, pwc) : 0; }
HLE_FUNC(mbrtowc_l, size_t, (wc32* pwc, const char* s, size_t n, void*, void*)) { return s ? utf8_decode(s, n, pwc) : 0; }
HLE_FUNC(mbtowc_l, int, (wc32* pwc, const char* s, size_t n, void*)) { if (!s) return 0; size_t r = utf8_decode(s, n, pwc); return r > 4 ? -1 : (int)r; }
HLE_FUNC(mbrlen_l, size_t, (const char* s, size_t n, void*, void*)) { return s ? utf8_decode(s, n, nullptr) : 0; }
HLE_FUNC(wcrtomb, size_t, (char* s, wc32 c, void*)) { return s ? utf8_encode(c, s) : 1; }
HLE_FUNC(wcrtomb_l, size_t, (char* s, wc32 c, void*, void*)) { return s ? utf8_encode(c, s) : 1; }
HLE_FUNC(btowc_l, u32, (int c, void*)) { return c >= 0 && c < 128 ? (u32)c : 0xffffffffu; }
HLE_FUNC(btowc, u32, (int c)) { return c >= 0 && c < 128 ? (u32)c : 0xffffffffu; }
HLE_FUNC(wctob_l, int, (u32 c, void*)) { return c < 128 ? (int)c : -1; }
HLE_FUNC(wctob, int, (u32 c)) { return c < 128 ? (int)c : -1; }
HLE_FUNC(mbsnrtowcs_l, size_t, (wc32* dst, const char** src, size_t nms, size_t len, void*, void*)) {
    const char* s = *src;
    size_t n = 0, used = 0;
    while (used < nms && (!dst || n < len)) {
        wc32 c;
        size_t r = utf8_decode(s + used, nms - used, &c);
        if (r == (size_t)-1) { set_errno(D_EILSEQ); *src = s + used; return (size_t)-1; }
        if (r == (size_t)-2) break;
        if (dst) dst[n] = c;
        if (r == 0) { if (dst) *src = nullptr; return n; }
        used += r;
        n++;
    }
    if (dst) *src = s + used;
    return n;
}
HLE_FUNC(mbsrtowcs_l, size_t, (wc32* dst, const char** src, size_t len, void*, void*)) {
    const char* s = *src;
    size_t n = 0, used = 0;
    while (!dst || n < len) {
        wc32 c;
        size_t r = utf8_decode(s + used, 4, &c);
        if (r == (size_t)-1 || r == (size_t)-2) { set_errno(D_EILSEQ); return (size_t)-1; }
        if (r == 0) { if (dst) { dst[n] = 0; *src = nullptr; } return n; }
        if (dst) dst[n] = c;
        used += r;
        n++;
    }
    if (dst) *src = s + used;
    return n;
}
HLE_FUNC(wcsnrtombs_l, size_t, (char* dst, const wc32** src, size_t nwc, size_t len, void*, void*)) {
    const wc32* s = *src;
    size_t out = 0;
    for (size_t i = 0; i < nwc; i++) {
        char tmp[4];
        size_t k = utf8_encode(s[i], tmp);
        if (s[i] == 0) { if (dst) { if (out < len) dst[out] = 0; *src = nullptr; } return out; }
        if (dst) {
            if (out + k > len) { *src = s + i; return out; }
            memcpy(dst + out, tmp, k);
        }
        out += k;
    }
    if (dst) *src = s + nwc;
    return out;
}
HLE_RAW(swprintf) {
    wc32* buf = gptr<wc32>(c.x(0));
    size_t n = c.x(1);
    const wc32* fmt = gptr<const wc32>(c.x(2));
    hle::VaReader va(c.sp());
    std::u32string r = gfmt::wformat(fmt, va);
    if (n) {
        size_t k = std::min(r.size(), n - 1);
        memcpy(buf, r.data(), k * 4);
        buf[k] = 0;
    }
    c.set_x(0, r.size() < n ? r.size() : (u64)-1);
}

// ===================================================================== math
#define MATH1(n) HLE_FUNC(n, double, (double a)) { return ::n(a); } HLE_FUNC(n##f, float, (float a)) { return ::n##f(a); }
#define MATH2(n) HLE_FUNC(n, double, (double a, double b)) { return ::n(a, b); } HLE_FUNC(n##f, float, (float a, float b)) { return ::n##f(a, b); }
MATH1(sin) MATH1(cos) MATH1(tan) MATH1(asin) MATH1(acos) MATH1(atan) MATH1(sinh) MATH1(cosh) MATH1(tanh)
MATH1(exp) MATH1(exp2) MATH1(expm1) MATH1(log) MATH1(log10) MATH1(log2) MATH1(log1p) MATH1(sqrt) MATH1(cbrt)
MATH1(floor) MATH1(ceil) MATH1(round) MATH1(trunc) MATH1(rint) MATH1(nearbyint) MATH1(fabs) MATH1(asinh)
MATH1(acosh) MATH1(atanh) MATH1(erf) MATH1(erfc) MATH1(tgamma) MATH1(lgamma)
MATH2(atan2) MATH2(pow) MATH2(fmod) MATH2(hypot) MATH2(remainder) MATH2(copysign) MATH2(fmin) MATH2(fmax) MATH2(fdim)
MATH2(nextafter)
HLE_FUNC(ldexp, double, (double a, int e)) { return ::ldexp(a, e); }
HLE_FUNC(ldexpf, float, (float a, int e)) { return ::ldexpf(a, e); }
HLE_FUNC(scalbn, double, (double a, int e)) { return ::scalbn(a, e); }
HLE_FUNC(scalbnf, float, (float a, int e)) { return ::scalbnf(a, e); }
HLE_FUNC(frexp, double, (double a, int* e)) { return ::frexp(a, e); }
HLE_FUNC(frexpf, float, (float a, int* e)) { return ::frexpf(a, e); }
HLE_FUNC(modf, double, (double a, double* i)) { return ::modf(a, i); }
HLE_FUNC(modff, float, (float a, float* i)) { return ::modff(a, i); }
HLE_FUNC(lround, s64, (double a)) { return ::llround(a); }
HLE_FUNC(lroundf, s64, (float a)) { return ::llroundf(a); }
HLE_FUNC(llround, s64, (double a)) { return ::llround(a); }
HLE_FUNC(lrint, s64, (double a)) { return ::llrint(a); }
HLE_FUNC(lrintf, s64, (float a)) { return ::llrintf(a); }
HLE_FUNC(fma, double, (double a, double b, double c)) { return ::fma(a, b, c); }
HLE_FUNC(fmaf, float, (float a, float b, float c)) { return ::fmaf(a, b, c); }
HLE_FUNC(__exp10, double, (double a)) { return ::pow(10.0, a); }
HLE_FUNC(__exp10f, float, (float a)) { return ::powf(10.0f, a); }
HLE_FUNC(__signbitf, int, (float a)) { return std::signbit(a); }
HLE_FUNC(__signbitd, int, (double a)) { return std::signbit(a); }
HLE_FUNC(__fpclassifyd, int, (double a)) {
    switch (std::fpclassify(a)) { case FP_NAN: return 1; case FP_INFINITE: return 2; case FP_ZERO: return 3; case FP_SUBNORMAL: return 5; default: return 4; }
}
HLE_FUNC(__fpclassifyf, int, (float a)) {
    switch (std::fpclassify(a)) { case FP_NAN: return 1; case FP_INFINITE: return 2; case FP_ZERO: return 3; case FP_SUBNORMAL: return 5; default: return 4; }
}
HLE_RAW(__sincos_stret) { double a = c.d(0); c.set_d(0, ::sin(a)); c.set_d(1, ::cos(a)); }
HLE_RAW(__sincosf_stret) { float a = c.s(0); c.set_s(0, ::sinf(a)); c.set_s(1, ::cosf(a)); }
HLE_RAW(__sincospi_stret) { double a = c.d(0) * 3.14159265358979323846; c.set_d(0, ::sin(a)); c.set_d(1, ::cos(a)); }

// 128-bit integer helpers (compiler-rt)
static void u128_divmod(u64 nlo, u64 nhi, u64 dlo, u64 dhi, u64& qlo, u64& qhi, u64& rlo, u64& rhi) {
    if (!dhi && !nhi) {
        if (!dlo) { qlo = qhi = rlo = rhi = 0; return; }
        qlo = nlo / dlo; qhi = 0; rlo = nlo % dlo; rhi = 0;
        return;
    }
    qlo = qhi = rlo = rhi = 0;
    for (int i = 127; i >= 0; i--) {
        rhi = (rhi << 1) | (rlo >> 63);
        rlo <<= 1;
        u64 bit = i >= 64 ? (nhi >> (i - 64)) & 1 : (nlo >> i) & 1;
        rlo |= bit;
        if (rhi > dhi || (rhi == dhi && rlo >= dlo)) {
            u64 b = rlo < dlo;
            rlo -= dlo;
            rhi -= dhi + b;
            if (i >= 64) qhi |= 1ull << (i - 64); else qlo |= 1ull << i;
        }
    }
}
static void neg128(u64& lo, u64& hi) { lo = ~lo + 1; hi = ~hi + (lo == 0); }
HLE_RAW(__udivti3) { u64 ql, qh, rl, rh; u128_divmod(c.x(0), c.x(1), c.x(2), c.x(3), ql, qh, rl, rh); c.set_x(0, ql); c.set_x(1, qh); }
HLE_RAW(__umodti3) { u64 ql, qh, rl, rh; u128_divmod(c.x(0), c.x(1), c.x(2), c.x(3), ql, qh, rl, rh); c.set_x(0, rl); c.set_x(1, rh); }
HLE_RAW(__divti3) {
    u64 nl = c.x(0), nh = c.x(1), dl = c.x(2), dh = c.x(3);
    bool nn = (s64)nh < 0, dn = (s64)dh < 0;
    if (nn) neg128(nl, nh);
    if (dn) neg128(dl, dh);
    u64 ql, qh, rl, rh;
    u128_divmod(nl, nh, dl, dh, ql, qh, rl, rh);
    if (nn != dn) neg128(ql, qh);
    c.set_x(0, ql); c.set_x(1, qh);
}
HLE_RAW(__modti3) {
    u64 nl = c.x(0), nh = c.x(1), dl = c.x(2), dh = c.x(3);
    bool nn = (s64)nh < 0, dn = (s64)dh < 0;
    if (nn) neg128(nl, nh);
    if (dn) neg128(dl, dh);
    u64 ql, qh, rl, rh;
    u128_divmod(nl, nh, dl, dh, ql, qh, rl, rh);
    if (nn) neg128(rl, rh);
    c.set_x(0, rl); c.set_x(1, rh);
}

// ===================================================================== time
namespace {
u64 unix_now_ns() {
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    u64 t = (u64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    return (t - 116444736000000000ull) * 100;
}
u64 mono_ns() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return u64(c.QuadPart / f.QuadPart) * 1000000000ull + u64(c.QuadPart % f.QuadPart) * 1000000000ull / f.QuadPart;
}
void to_dtm(const std::tm& t, d_tm* o, s64 gmtoff, const char* zone) {
    o->tm_sec = t.tm_sec; o->tm_min = t.tm_min; o->tm_hour = t.tm_hour; o->tm_mday = t.tm_mday;
    o->tm_mon = t.tm_mon; o->tm_year = t.tm_year; o->tm_wday = t.tm_wday; o->tm_yday = t.tm_yday;
    o->tm_isdst = t.tm_isdst; o->tm_gmtoff = gmtoff; o->tm_zone = gaddr_of(zone);
}
std::tm from_dtm(const d_tm* o) {
    std::tm t{};
    t.tm_sec = o->tm_sec; t.tm_min = o->tm_min; t.tm_hour = o->tm_hour; t.tm_mday = o->tm_mday;
    t.tm_mon = o->tm_mon; t.tm_year = o->tm_year; t.tm_wday = o->tm_wday; t.tm_yday = o->tm_yday;
    t.tm_isdst = o->tm_isdst;
    return t;
}
long local_gmtoff() {
    long tz = 0;
    _get_timezone(&tz);
    return -tz;
}
}  // namespace

HLE_FUNC(time, s64, (s64* t)) { s64 v = s64(unix_now_ns() / 1000000000ull); if (t) *t = v; return v; }
HLE_FUNC(gettimeofday, int, (d_timeval* tv, void* tz)) {
    if (tv) { u64 ns = unix_now_ns(); tv->tv_sec = s64(ns / 1000000000ull); tv->tv_usec = s32((ns / 1000) % 1000000); }
    if (tz) memset(tz, 0, 8);
    return 0;
}
HLE_FUNC(clock_gettime, int, (int clk, d_timespec* ts)) {
    // Darwin: CLOCK_REALTIME 0, MONOTONIC 6, MONOTONIC_RAW 4, UPTIME_RAW 8, PROCESS_CPUTIME 12, THREAD_CPUTIME 16
    u64 ns = clk == 0 ? unix_now_ns() : mono_ns();
    ts->tv_sec = s64(ns / 1000000000ull);
    ts->tv_nsec = s64(ns % 1000000000ull);
    return 0;
}
HLE_FUNC(clock_gettime_nsec_np, u64, (int clk)) { return clk == 0 ? unix_now_ns() : mono_ns(); }
HLE_FUNC(clock_getres, int, (int, d_timespec* ts)) { if (ts) { ts->tv_sec = 0; ts->tv_nsec = 1000; } return 0; }
HLE_FUNC(mach_absolute_time, u64, ()) { return mono_ns(); }
HLE_FUNC(mach_continuous_time, u64, ()) { return mono_ns(); }
HLE_FUNC(mach_approximate_time, u64, ()) { return mono_ns(); }
HLE_FUNC(mach_timebase_info, int, (u32* tb)) { tb[0] = 1; tb[1] = 1; return 0; }
HLE_FUNC(clock, u64, ()) { return u64(::clock()) * (1000000 / CLOCKS_PER_SEC); }
HLE_FUNC(difftime, double, (s64 a, s64 b)) { return double(a - b); }

static thread_local d_tm t_tm;
HLE_FUNC(localtime_r, d_tm*, (const s64* t, d_tm* out)) {
    std::tm tm{};
    time_t tt = (time_t)*t;
    localtime_s(&tm, &tt);
    to_dtm(tm, out, local_gmtoff(), tm.tm_isdst ? "DST" : "LOC");
    return out;
}
HLE_FUNC(gmtime_r, d_tm*, (const s64* t, d_tm* out)) {
    std::tm tm{};
    time_t tt = (time_t)*t;
    gmtime_s(&tm, &tt);
    to_dtm(tm, out, 0, "UTC");
    return out;
}
HLE_FUNC(localtime, d_tm*, (const s64* t)) { return hle_impl_localtime_r(t, &t_tm); }
HLE_FUNC(gmtime, d_tm*, (const s64* t)) { return hle_impl_gmtime_r(t, &t_tm); }
HLE_FUNC(mktime, s64, (d_tm* t)) {
    std::tm tm = from_dtm(t);
    s64 r = (s64)::mktime(&tm);
    to_dtm(tm, t, local_gmtoff(), "LOC");
    return r;
}
HLE_FUNC(timegm, s64, (d_tm* t)) {
    std::tm tm = from_dtm(t);
    return (s64)_mkgmtime(&tm);
}
HLE_FUNC(tzset, void, ()) {}
static char t_ctime_buf[64];
HLE_FUNC(ctime, const char*, (const s64* t)) {
    time_t tt = (time_t)*t;
    ctime_s(t_ctime_buf, sizeof t_ctime_buf, &tt);
    return t_ctime_buf;
}
HLE_FUNC(asctime, const char*, (const d_tm* t)) {
    std::tm tm = from_dtm(t);
    asctime_s(t_ctime_buf, sizeof t_ctime_buf, &tm);
    return t_ctime_buf;
}
static size_t do_strftime(char* s, size_t max, const char* fmt, const d_tm* t) {
    std::tm tm = from_dtm(t);
    // MSVC rejects a few POSIX conversions; rewrite the common ones
    std::string f;
    for (const char* p = fmt; *p; p++) {
        if (*p == '%' && p[1]) {
            char c = p[1];
            if (c == 'e') { f += "%#d"; p++; continue; }
            if (c == 'k') { f += "%#H"; p++; continue; }
            if (c == 's') { f += std::to_string((long long)_mkgmtime(&tm)); p++; continue; }
            if (c == 'z') { long o = (long)t->tm_gmtoff / 60; char b[8]; snprintf(b, 8, "%c%02ld%02ld", o < 0 ? '-' : '+', labs(o) / 60, labs(o) % 60); f += b; p++; continue; }
            if (c == 'Z') { f += t->tm_zone ? gptr<const char>(t->tm_zone) : "UTC"; p++; continue; }
            if (c == 'E' || c == 'O') { f += '%'; f += p[2]; p += 2; continue; }
            f += '%'; f += c; p++;
            continue;
        }
        f += *p;
    }
    if (!max) return 0;
    _invalid_parameter_handler old = _set_thread_local_invalid_parameter_handler(
        [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t) {});
    size_t r = ::strftime(s, max, f.c_str(), &tm);
    _set_thread_local_invalid_parameter_handler(old);
    return r;
}
HLE_FUNC(strftime, size_t, (char* s, size_t max, const char* fmt, const d_tm* t)) { return do_strftime(s, max, fmt, t); }
HLE_FUNC(strftime_l, size_t, (char* s, size_t max, const char* fmt, const d_tm* t, void*)) { return do_strftime(s, max, fmt, t); }
HLE_FUNC(strptime, const char*, (const char* s, const char* fmt, d_tm* tm)) {
    // minimal: supports %Y %m %d %H %M %S %y %b %a %T %D and literals
    static const char* mons[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
    auto num = [&](int maxd, int& out) {
        int v = 0, n = 0;
        while (n < maxd && isdigit((unsigned char)*s)) { v = v * 10 + (*s++ - '0'); n++; }
        out = v;
        return n > 0;
    };
    for (const char* f = fmt; *f; f++) {
        if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*s)) s++; continue; }
        if (*f != '%') { if (*s != *f) return nullptr; s++; continue; }
        f++;
        int v;
        switch (*f) {
        case 'Y': if (!num(4, v)) return nullptr; tm->tm_year = v - 1900; break;
        case 'y': if (!num(2, v)) return nullptr; tm->tm_year = v < 69 ? v + 100 : v; break;
        case 'm': if (!num(2, v)) return nullptr; tm->tm_mon = v - 1; break;
        case 'd': case 'e': while (*s == ' ') s++; if (!num(2, v)) return nullptr; tm->tm_mday = v; break;
        case 'H': if (!num(2, v)) return nullptr; tm->tm_hour = v; break;
        case 'M': if (!num(2, v)) return nullptr; tm->tm_min = v; break;
        case 'S': if (!num(2, v)) return nullptr; tm->tm_sec = v; break;
        case 'j': if (!num(3, v)) return nullptr; tm->tm_yday = v - 1; break;
        case 'T': s = hle_impl_strptime(s, "%H:%M:%S", tm); if (!s) return nullptr; break;
        case 'D': s = hle_impl_strptime(s, "%m/%d/%y", tm); if (!s) return nullptr; break;
        case 'b': case 'B': case 'h': {
            int i = 0;
            for (; i < 12; i++) if (!_strnicmp(s, mons[i], 3)) break;
            if (i == 12) return nullptr;
            tm->tm_mon = i;
            while (isalpha((unsigned char)*s)) s++;
        } break;
        case 'a': case 'A': while (isalpha((unsigned char)*s)) s++; break;
        case 'Z': while (isalpha((unsigned char)*s)) s++; break;
        case 'z': if (*s == '+' || *s == '-') s++; num(4, v); break;
        case '%': if (*s != '%') return nullptr; s++; break;
        default: return nullptr;
        }
    }
    return s;
}

HLE_FUNC(nanosleep, int, (const d_timespec* req, d_timespec* rem)) {
    if (req) std::this_thread::sleep_for(std::chrono::nanoseconds(req->tv_sec * 1000000000ll + req->tv_nsec));
    if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
    return 0;
}
HLE_FUNC(usleep, int, (u32 us)) {
    if (us == 0) SwitchToThread();
    else std::this_thread::sleep_for(std::chrono::microseconds(us));
    return 0;
}
HLE_FUNC(sleep, u32, (u32 s)) { std::this_thread::sleep_for(std::chrono::seconds(s)); return 0; }
