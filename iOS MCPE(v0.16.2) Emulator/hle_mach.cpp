// libSystem: mach, dyld, dl*, sysctl, process, signals, mmap, TLV.
#include "fdtab.h"
#include "hle_common.h"
#include "macho.h"
#include "vfs.h"
#include "vmem.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <map>
#include <mutex>
#include <thread>

using namespace hlec;

namespace {
constexpr u32 kTaskSelf = 0x103, kHostSelf = 0x203;
std::atomic<u32> g_next_port{0x10000};
// The game sizes its worker pools (chunk generation / meshing) from this. EMU_NCPU overrides.
int reported_ncpu() {
    static int n = [] {
        if (const char* e = getenv("EMU_NCPU")) return std::clamp(atoi(e), 1, 64);
        return (int)std::clamp<unsigned>(std::thread::hardware_concurrency() / 2, 2, 8);
    }();
    return n;
}
constexpr u64 kMemSize = 2ull << 30;
const char* kMachine = "iPhone8,1";
const char* kModel = "N71AP";
const char* kOsVersion = "13E230";
const char* kOsRelease = "15.4.0";
}  // namespace

// ===================================================================== mach basics
HLE_DATA(mach_task_self_, 4) { *static_cast<u32*>(p) = kTaskSelf; }
HLE_DATA(vm_page_size, 8) { *static_cast<u64*>(p) = 0x4000; }
HLE_DATA(vm_page_mask, 8) { *static_cast<u64*>(p) = 0x3fff; }
HLE_DATA(vm_page_shift, 4) { *static_cast<u32*>(p) = 14; }
HLE_DATA(vm_kernel_page_size, 8) { *static_cast<u64*>(p) = 0x4000; }
HLE_DATA(vm_kernel_page_mask, 8) { *static_cast<u64*>(p) = 0x3fff; }
HLE_DATA(NDR_record, 8) { static_cast<u8*>(p)[4] = 1; }
HLE_FUNC(mach_host_self, u32, ()) { return kHostSelf; }
HLE_FUNC(mach_task_self, u32, ()) { return kTaskSelf; }
HLE_FUNC(mach_thread_self, u32, ()) { auto* t = threads::current(); return t ? 0x1000 + t->id : 0x1000; }
HLE_FUNC(mach_port_deallocate, int, (u32, u32)) { return 0; }
HLE_FUNC(mach_port_allocate, int, (u32, u32, u32* name)) { *name = g_next_port++; return 0; }
HLE_FUNC(mach_port_insert_right, int, (u32, u32, u32, u32)) { return 0; }
HLE_FUNC(mach_port_mod_refs, int, (u32, u32, u32, s32)) { return 0; }
HLE_FUNC(mach_port_move_member, int, (u32, u32, u32)) { return 0; }
HLE_FUNC(mach_port_destroy, int, (u32, u32)) { return 0; }
HLE_FUNC(mach_port_request_notification, int, (u32, u32, s32, u32, u32, u32, u32* prev)) { if (prev) *prev = 0; return 0; }
HLE_FUNC(mach_msg, int, (void*, s32 option, u32, u32, u32, u32 timeout, u32)) {
    // Only crash-reporter exception servers use mach_msg: make receivers wait forever.
    if ((option & 2) && !(option & 0x100)) {  // MACH_RCV_MSG without MACH_RCV_TIMEOUT
        for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
    }
    if (option & 2) std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
    return 0x10004003;  // MACH_RCV_TIMED_OUT
}
HLE_FUNC(host_page_size, int, (u32, u64* out)) { *out = 0x4000; return 0; }
HLE_FUNC(host_statistics, int, (u32, int flavor, u32* info, u32* count)) {
    if (flavor == 2) {  // HOST_VM_INFO: vm_statistics (natural_t fields)
        u32 n = std::min<u32>(*count, 15);
        memset(info, 0, n * 4);
        if (n > 0) info[0] = 60000;   // free_count (pages of 16K)
        if (n > 1) info[1] = 40000;   // active_count
        if (n > 2) info[2] = 20000;   // inactive_count
        if (n > 3) info[3] = 10000;   // wire_count
        *count = n;
        return 0;
    }
    memset(info, 0, *count * 4);
    return 0;
}
HLE_FUNC(host_statistics64, int, (u32, int, u32* info, u32* count)) {
    memset(info, 0, *count * 4);
    if (*count > 0) info[0] = 60000;
    return 0;
}
HLE_FUNC(host_info, int, (u32, int, u32* info, u32* count)) { memset(info, 0, *count * 4); return 0; }
HLE_FUNC(task_info, int, (u32, u32 flavor, u32* info, u32* count)) {
    memset(info, 0, *count * 4);
    // MACH_TASK_BASIC_INFO(20): {virtual_size u64, resident_size u64, resident_size_max u64, ...}
    if (flavor == 20 && *count >= 6) {
        u64* q = reinterpret_cast<u64*>(info);
        q[0] = 1ull << 32;
        q[1] = 300ull << 20;
        q[2] = 400ull << 20;
    }
    return 0;
}
HLE_FUNC(task_threads, int, (u32, u64*, u32* n)) { if (n) *n = 0; return 5; }
HLE_FUNC(task_get_exception_ports, int, (u32, u32, void*, u32* n, void*, void*, void*)) { if (n) *n = 0; return 0; }
HLE_FUNC(task_set_exception_ports, int, (u32, u32, u32, int, int)) { return 0; }
HLE_FUNC(task_swap_exception_ports, int, (u32, u32, u32, int, int, void*, u32* n, void*, void*, void*)) { if (n) *n = 0; return 0; }
HLE_FUNC(thread_get_exception_ports, int, (u32, u32, void*, u32* n, void*, void*, void*)) { if (n) *n = 0; return 0; }
HLE_FUNC(thread_swap_exception_ports, int, (u32, u32, u32, int, int, void*, u32* n, void*, void*, void*)) { if (n) *n = 0; return 0; }
HLE_FUNC(thread_get_state, int, (u32, int, void*, u32*)) { return 5; }
HLE_FUNC(thread_suspend, int, (u32)) { return 5; }
HLE_FUNC(thread_resume, int, (u32)) { return 5; }
HLE_FUNC(thread_policy_set, int, (u32, u32, void*, u32)) { return 0; }
HLE_FUNC(thread_info, int, (u32, u32, u32* info, u32* count)) { memset(info, 0, *count * 4); return 0; }
HLE_FUNC(exception_raise, int, ()) { return 5; }
HLE_FUNC(exception_raise_state, int, ()) { return 5; }
HLE_FUNC(exception_raise_state_identity, int, ()) { return 5; }
HLE_FUNC(mach_error_string, const char*, (int)) { return "mach error"; }

HLE_FUNC(vm_allocate, int, (u32, u64* addr, u64 size, int flags)) {
    gaddr a = (flags & 1) || !*addr ? vmem::map_anywhere(size) : (vmem::map_fixed(*addr, size) ? *addr : 0);
    if (!a) return 3;  // KERN_NO_SPACE
    *addr = a;
    return 0;
}
HLE_FUNC(vm_deallocate, int, (u32, u64 addr, u64 size)) { vmem::unmap(addr, size); return 0; }
HLE_FUNC(mach_vm_allocate, int, (u32 t, u64* addr, u64 size, int flags)) { return hle_impl_vm_allocate(t, addr, size, flags); }
HLE_FUNC(mach_vm_deallocate, int, (u32, u64 addr, u64 size)) { vmem::unmap(addr, size); return 0; }
HLE_FUNC(vm_map, int, (u32, u64* addr, u64 size, u64, int flags)) {
    gaddr a = (flags & 1) || !*addr ? vmem::map_anywhere(size) : (vmem::map_fixed(*addr, size) ? *addr : 0);
    if (!a) return 3;
    *addr = a;
    return 0;
}
HLE_FUNC(vm_protect, int, (u32, u64, u64, int, int)) { return 0; }
HLE_FUNC(mach_make_memory_entry_64, int, (u32, u64*, u64, int, u32*, u32)) { return 5; }
HLE_FUNC(vm_read_overwrite, int, (u32, u64 addr, u64 size, u64 data, u64* out)) {
    if (!vmem::is_mapped(addr, size)) return 1;  // KERN_INVALID_ADDRESS
    memcpy(gptr(data), gptr(addr), size);
    if (out) *out = size;
    return 0;
}

// ===================================================================== mmap
HLE_FUNC(mmap, s64, (u64 addr, u64 len, int prot, int flags, int fd, s64 off)) {
    (void)prot;
    gaddr a = 0;
    if ((flags & 0x10) && addr) {  // MAP_FIXED
        vmem::unmap(addr, len);
        a = vmem::map_fixed(addr, len) ? addr : 0;
    } else {
        a = vmem::map_anywhere(len, addr);
    }
    if (!a) { set_errno(D_ENOMEM); return -1; }
    if (!(flags & 0x1000) && fd >= 0) {  // file backed (copy)
        auto f = fdtab::get(fd);
        if (f && f->kind == fdtab::Kind::File) {
            OVERLAPPED ov{};
            ov.Offset = (DWORD)(u64)off;
            ov.OffsetHigh = (DWORD)((u64)off >> 32);
            DWORD got = 0;
            ReadFile(f->handle, gptr(a), (DWORD)std::min<u64>(len, 0xffffffff), &got, &ov);
        }
    }
    return (s64)a;
}
HLE_FUNC(munmap, int, (u64 addr, u64 len)) { vmem::unmap(addr, len); return 0; }
HLE_FUNC(mprotect, int, (u64, u64, int)) { return 0; }
HLE_FUNC(madvise, int, (u64, u64, int)) { return 0; }
HLE_FUNC(posix_madvise, int, (u64, u64, int)) { return 0; }
HLE_FUNC(msync, int, (u64, u64, int)) { return 0; }
HLE_FUNC(mlock, int, (u64, u64)) { return 0; }
HLE_FUNC(munlock, int, (u64, u64)) { return 0; }
HLE_FUNC(minherit, int, (u64, u64, int)) { return 0; }
HLE_FUNC(getpagesize, int, ()) { return 0x4000; }

// ===================================================================== dyld
namespace {
std::string guest_image_name(const MachImage& i) {
    if (i.is_main) return std::string(vfs::kBundlePath) + "/minecraftpe";
    return i.install_name;
}
}  // namespace
HLE_FUNC(_dyld_image_count, u32, ()) { return (u32)macho::images().size(); }
HLE_FUNC(_dyld_get_image_header, gaddr, (u32 i)) { auto& v = macho::images(); return i < v.size() ? v[i]->base : 0; }
HLE_FUNC(_dyld_get_image_vmaddr_slide, s64, (u32 i)) { auto& v = macho::images(); return i < v.size() ? v[i]->slide : 0; }
HLE_FUNC(_dyld_get_image_name, const char*, (u32 i)) {
    auto& v = macho::images();
    return i < v.size() ? intern(guest_image_name(*v[i])) : nullptr;
}
HLE_FUNC_R(_dyld_register_func_for_add_image, void, (gaddr fn)) {
    auto imgs = macho::images();
    for (auto& i : imgs) g_cpu->call(fn, {i->base, (u64)i->slide});
}
HLE_FUNC(_dyld_register_func_for_remove_image, void, (gaddr)) {}
HLE_FUNC(_dyld_find_unwind_sections, bool, (gaddr addr, u64* info)) {
    MachImage* img = macho::image_for_address(addr);
    if (!img) return false;
    memset(info, 0, 40);
    info[0] = img->base;
    if (auto* s = img->find_section("__TEXT", "__eh_frame")) { info[1] = s->addr; info[2] = s->size; }
    if (auto* s = img->find_section("__TEXT", "__unwind_info")) { info[3] = s->addr; info[4] = s->size; }
    return true;
}
HLE_FUNC(_dyld_get_image_slide, s64, (gaddr mh)) {
    for (auto& i : macho::images()) if (i->base == mh) return i->slide;
    return 0;
}
HLE_FUNC(dyld_get_sdk_version, u32, (gaddr)) { return 0x000e0000; }
HLE_FUNC(dyld_get_program_sdk_version, u32, ()) { return 0x00090300; }
HLE_FUNC(_NSGetExecutablePath, int, (char* buf, u32* size)) {
    std::string p = std::string(vfs::kBundlePath) + "/minecraftpe";
    if (*size < p.size() + 1) { *size = (u32)p.size() + 1; return -1; }
    memcpy(buf, p.c_str(), p.size() + 1);
    return 0;
}
static gaddr find_sect(gaddr mh, const char* seg, const char* sect, u64* size, bool unslid) {
    for (auto& i : macho::images()) {
        if (i->base != mh) continue;
        if (auto* s = i->find_section(seg, sect)) {
            if (size) *size = s->size;
            return unslid ? s->addr - i->slide : s->addr;
        }
    }
    if (size) *size = 0;
    return 0;
}
HLE_FUNC(getsectdatafromheader_64, gaddr, (gaddr mh, const char* seg, const char* sect, u64* size)) { return find_sect(mh, seg, sect, size, true); }
HLE_FUNC(getsectdatafromheader, gaddr, (gaddr mh, const char* seg, const char* sect, u32* size)) {
    u64 s = 0;
    gaddr r = find_sect(mh, seg, sect, &s, true);
    if (size) *size = (u32)s;
    return r;
}
HLE_FUNC(getsectiondata, gaddr, (gaddr mh, const char* seg, const char* sect, u64* size)) { return find_sect(mh, seg, sect, size, false); }
HLE_FUNC(getsegmentdata, gaddr, (gaddr mh, const char* seg, u64* size)) {
    for (auto& i : macho::images()) {
        if (i->base != mh) continue;
        if (auto* s = i->find_segment(seg)) { if (size) *size = s->vmsize; return s->addr; }
    }
    return 0;
}

struct DlInfo { u64 fname, fbase, sname, saddr; };
HLE_FUNC(dladdr, int, (gaddr addr, DlInfo* info)) {
    MachImage* img = macho::image_for_address(addr);
    if (!img) return 0;
    info->fname = gaddr_of(intern(guest_image_name(*img)));
    info->fbase = img->base;
    info->sname = 0;
    info->saddr = 0;
    auto it = img->symbols.upper_bound(addr);
    if (it != img->symbols.begin()) {
        --it;
        const std::string& n = it->second;
        info->sname = gaddr_of(intern(n.size() && n[0] == '_' ? n.substr(1) : n));
        info->saddr = it->first;
    }
    return 1;
}
namespace {
char g_dlerror_buf[256];
bool g_dlerror_set = false;
}  // namespace
HLE_FUNC(dlopen, gaddr, (const char* path, int)) {
    LOGD("dlopen(%s)", path ? path : "NULL");
    return path ? 0x7ead0001ull : 0x7ead0000ull;  // fake handles; dlsym searches globally
}
HLE_FUNC(dlclose, int, (gaddr)) { return 0; }
HLE_FUNC(dlerror, const char*, ()) {
    if (!g_dlerror_set) return nullptr;
    g_dlerror_set = false;
    return g_dlerror_buf;
}
HLE_FUNC(dlsym, gaddr, (gaddr, const char* name)) {
    std::string s = std::string("_") + name;
    gaddr a = macho::find_export(s);
    if (!a) a = hle::lookup(s);
    if (!a) {
        snprintf(g_dlerror_buf, sizeof g_dlerror_buf, "dlsym: symbol not found: %s", name);
        g_dlerror_set = true;
        LOGD("dlsym(%s) not found", name);
    }
    return a;
}
HLE_FUNC(dlopen_preflight, bool, (const char*)) { return false; }

// ===================================================================== TLV (__thread variables)
namespace {
std::mutex g_tlv_lock;
std::vector<TlvInfo> g_tlv_images;
}  // namespace
namespace tlv {
// called by the loader for every image with __thread_vars
void register_image(MachImage& img) {
    auto* vars = img.find_section("__DATA", "__thread_vars");
    if (!vars) return;
    std::lock_guard lk(g_tlv_lock);
    img.tlv.key = (u32)g_tlv_images.size();
    g_tlv_images.push_back(img.tlv);
    u64* d = gptr<u64>(vars->addr);
    for (u64 i = 0; i < vars->size / 24; i++) d[i * 3 + 1] = img.tlv.key;
}
}  // namespace tlv
HLE_RAW(_tlv_bootstrap) {
    u64* desc = gptr<u64>(c.x(0));
    u32 key = (u32)desc[1];
    u64 off = desc[2];
    GuestThread* t = threads::current();
    gaddr block = 0;
    auto it = t->tlv_blocks.find(key);
    if (it != t->tlv_blocks.end()) block = it->second;
    else {
        TlvInfo ti;
        {
            std::lock_guard lk(g_tlv_lock);
            ti = g_tlv_images.at(key);
        }
        void* p = vmem::gcalloc(1, ti.template_total_size + 16);
        if (ti.template_data_size) memcpy(p, gptr(ti.template_start), ti.template_data_size);
        block = gaddr_of(p);
        t->tlv_blocks[key] = block;
    }
    c.set_x(0, block + off);
}
HLE_FUNC(_tlv_atexit, void, (gaddr, gaddr)) {}

// ===================================================================== process / environment
namespace {
std::mutex g_env_lock;
std::map<std::string, std::string> g_env;
std::map<std::string, const char*> g_env_c;
void env_init() {
    static bool done = false;
    if (done) return;
    done = true;
    g_env["HOME"] = vfs::kHomePath;
    g_env["TMPDIR"] = std::string(vfs::kHomePath) + "/tmp/";
    g_env["USER"] = "mobile";
    g_env["LOGNAME"] = "mobile";
    g_env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin";
    g_env["SHELL"] = "/bin/sh";
    g_env["CFFIXED_USER_HOME"] = vfs::kHomePath;
    // pass emulator debug switches (EMU_*) and OBJC_* through to the guest
    if (wchar_t* blk = GetEnvironmentStringsW()) {
        for (wchar_t* p = blk; *p; p += wcslen(p) + 1) {
            std::string kv = narrow(p);
            if (kv.rfind("EMU_", 0) && kv.rfind("OBJC_", 0)) continue;
            size_t eq = kv.find('=');
            if (eq != std::string::npos) g_env[kv.substr(0, eq)] = kv.substr(eq + 1);
        }
        FreeEnvironmentStringsW(blk);
    }
}
}  // namespace
HLE_FUNC(getenv, const char*, (const char* n)) {
    std::lock_guard lk(g_env_lock);
    env_init();
    auto it = g_env.find(n);
    if (it == g_env.end()) return nullptr;
    return intern(it->second);
}
HLE_FUNC(setenv, int, (const char* n, const char* v, int over)) {
    std::lock_guard lk(g_env_lock);
    env_init();
    if (!over && g_env.count(n)) return 0;
    g_env[n] = v ? v : "";
    return 0;
}
HLE_FUNC(unsetenv, int, (const char* n)) { std::lock_guard lk(g_env_lock); env_init(); g_env.erase(n); return 0; }
HLE_FUNC(getpid, int, ()) { return 4242; }
HLE_FUNC(getppid, int, ()) { return 1; }
HLE_FUNC(getuid, u32, ()) { return 501; }
HLE_FUNC(geteuid, u32, ()) { return 501; }
HLE_FUNC(getgid, u32, ()) { return 501; }
HLE_FUNC(getegid, u32, ()) { return 501; }
HLE_FUNC(issetugid, int, ()) { return 0; }
HLE_FUNC(getlogin, const char*, ()) { return "mobile"; }
HLE_FUNC(getprogname, const char*, ()) { return "minecraftpe"; }
HLE_FUNC(setprogname, void, (const char*)) {}
HLE_FUNC(gethostname, int, (char* buf, size_t n)) { strncpy(buf, "iPhone", n); return 0; }
HLE_FUNC(gethostuuid, int, (u8* id, const void*)) { for (int i = 0; i < 16; i++) id[i] = (u8)(0x42 + i); return 0; }
HLE_FUNC(getrlimit, int, (int res, u64* rl)) {
    rl[0] = res == 8 ? 256 : res == 3 ? 8ull << 20 : ~0ull;  // RLIMIT_NOFILE / RLIMIT_STACK
    rl[1] = ~0ull;
    return 0;
}
HLE_FUNC(setrlimit, int, (int, const u64*)) { return 0; }
HLE_FUNC(getrusage, int, (int, void* ru)) { memset(ru, 0, 144); return 0; }
HLE_FUNC(getpriority, int, (int, u32)) { return 0; }
HLE_FUNC(setpriority, int, (int, u32, int)) { return 0; }
HLE_FUNC(sysconf, s64, (int name)) {
    switch (name) {
    case 29: return 0x4000;                    // _SC_PAGESIZE
    case 57: case 58: return reported_ncpu();  // _SC_NPROCESSORS_CONF / ONLN
    case 200: return (s64)(kMemSize / 0x4000);  // _SC_PHYS_PAGES
    case 5: return 256;                        // _SC_OPEN_MAX
    case 3: return 100;                        // _SC_CLK_TCK
    case 1: return 262144;                     // _SC_ARG_MAX
    case 71: return 1024;                      // _SC_GETPW_R_SIZE_MAX
    default: return -1;
    }
}
struct UtsName { char sysname[256], nodename[256], release[256], version[256], machine[256]; };
HLE_FUNC(uname, int, (UtsName* u)) {
    memset(u, 0, sizeof *u);
    strcpy(u->sysname, "Darwin");
    strcpy(u->nodename, "iPhone");
    strcpy(u->release, kOsRelease);
    strcpy(u->version, "Darwin Kernel Version 15.4.0: root:xnu-3248.41.4~28/RELEASE_ARM64_S8000");
    strcpy(u->machine, kMachine);
    return 0;
}

static int sysctl_out(void* oldp, size_t* oldlen, const void* data, size_t n) {
    if (!oldlen) return 0;
    if (!oldp) { *oldlen = n; return 0; }
    if (*oldlen < n) { memcpy(oldp, data, *oldlen); set_errno(D_ENOMEM); return -1; }
    memcpy(oldp, data, n);
    *oldlen = n;
    return 0;
}
static int sysctl_str(void* oldp, size_t* oldlen, const char* s) { return sysctl_out(oldp, oldlen, s, strlen(s) + 1); }
static int sysctl_int(void* oldp, size_t* oldlen, s32 v) { return sysctl_out(oldp, oldlen, &v, 4); }
static int sysctl_u64(void* oldp, size_t* oldlen, u64 v) {
    if (oldlen && *oldlen == 4 && oldp) { u32 x = (u32)v; return sysctl_out(oldp, oldlen, &x, 4); }
    return sysctl_out(oldp, oldlen, &v, 8);
}
static int sysctl_named(const std::string& n, void* oldp, size_t* oldlen) {
    if (n == "hw.machine") return sysctl_str(oldp, oldlen, kMachine);
    if (n == "hw.model") return sysctl_str(oldp, oldlen, kModel);
    if (n == "hw.ncpu" || n == "hw.activecpu" || n == "hw.physicalcpu" || n == "hw.logicalcpu" || n == "hw.physicalcpu_max" || n == "hw.logicalcpu_max" || n == "hw.availcpu")
        return sysctl_int(oldp, oldlen, reported_ncpu());
    if (n == "hw.memsize" || n == "hw.physmem") return sysctl_u64(oldp, oldlen, kMemSize);
    if (n == "hw.pagesize") return sysctl_u64(oldp, oldlen, 0x4000);
    if (n == "hw.cachelinesize") return sysctl_u64(oldp, oldlen, 64);
    if (n == "hw.l1dcachesize" || n == "hw.l1icachesize") return sysctl_u64(oldp, oldlen, 65536);
    if (n == "hw.l2cachesize") return sysctl_u64(oldp, oldlen, 3 << 20);
    if (n == "hw.cputype") return sysctl_int(oldp, oldlen, 0x0100000c);
    if (n == "hw.cpusubtype") return sysctl_int(oldp, oldlen, 1);
    if (n == "hw.cpufamily") return sysctl_int(oldp, oldlen, (s32)0x2c91a47e);
    if (n == "hw.cpufrequency" || n == "hw.cpufrequency_max") return sysctl_u64(oldp, oldlen, 1850000000ull);
    if (n == "hw.busfrequency") return sysctl_u64(oldp, oldlen, 100000000ull);
    if (n == "hw.tbfrequency") return sysctl_u64(oldp, oldlen, 24000000ull);
    if (n == "kern.osversion") return sysctl_str(oldp, oldlen, kOsVersion);
    if (n == "kern.osrelease") return sysctl_str(oldp, oldlen, kOsRelease);
    if (n == "kern.ostype") return sysctl_str(oldp, oldlen, "Darwin");
    if (n == "kern.hostname") return sysctl_str(oldp, oldlen, "iPhone");
    if (n == "kern.boottime") {
        d_timeval tv{(s64)time(nullptr) - 3600, 0, 0};
        return sysctl_out(oldp, oldlen, &tv, sizeof tv);
    }
    if (n == "kern.maxfiles" || n == "kern.maxfilesperproc") return sysctl_int(oldp, oldlen, 12288);
    if (n == "hw.optional.neon" || n == "hw.optional.floatingpoint" || n == "hw.optional.armv8_crc32") return sysctl_int(oldp, oldlen, 1);
    if (n.rfind("hw.optional.", 0) == 0) return sysctl_int(oldp, oldlen, 0);
    LOGD("sysctlbyname(%s) unknown", n.c_str());
    set_errno(D_ENOENT);
    return -1;
}
HLE_FUNC(sysctlbyname, int, (const char* name, void* oldp, size_t* oldlen, const void*, size_t)) {
    return sysctl_named(name, oldp, oldlen);
}
HLE_FUNC(sysctl, int, (const s32* mib, u32 n, void* oldp, size_t* oldlen, const void*, size_t)) {
    if (n >= 2 && mib[0] == 6) {  // CTL_HW
        switch (mib[1]) {
        case 1: return sysctl_named("hw.machine", oldp, oldlen);
        case 2: return sysctl_named("hw.model", oldp, oldlen);
        case 3: case 25: return sysctl_named("hw.ncpu", oldp, oldlen);
        case 5: return sysctl_int(oldp, oldlen, (s32)(kMemSize >> 1));
        case 7: return sysctl_int(oldp, oldlen, 0x4000);
        case 15: return sysctl_int(oldp, oldlen, 1850000000);
        case 24: return sysctl_u64(oldp, oldlen, kMemSize);
        default: break;
        }
    }
    if (n >= 2 && mib[0] == 1) {  // CTL_KERN
        switch (mib[1]) {
        case 1: return sysctl_named("kern.ostype", oldp, oldlen);
        case 2: return sysctl_named("kern.osrelease", oldp, oldlen);
        case 10: return sysctl_named("kern.hostname", oldp, oldlen);
        case 21: return sysctl_named("kern.boottime", oldp, oldlen);
        case 65: return sysctl_named("kern.osversion", oldp, oldlen);
        case 14: {  // KERN_PROC: zeroed kinfo_proc (=> not being traced)
            if (oldp && oldlen) memset(oldp, 0, *oldlen);
            else if (oldlen) *oldlen = 648;
            return 0;
        }
        default: break;
        }
    }
    LOGD("sysctl(%d,%d) unknown", n > 0 ? mib[0] : -1, n > 1 ? mib[1] : -1);
    set_errno(D_ENOENT);
    return -1;
}
HLE_FUNC(sysctlnametomib, int, (const char*, s32*, size_t*)) { set_errno(D_ENOENT); return -1; }

// ===================================================================== exit / abort / signals
namespace {
void flush_all() { fflush(stdout); fflush(stderr); }
}  // namespace
HLE_FUNC(exit, void, (int code)) {
    LOGI("guest exit(%d)", code);
    flush_all();
    ExitProcess((UINT)code);
}
HLE_FUNC(_exit, void, (int code)) { flush_all(); ExitProcess((UINT)code); }
HLE_FUNC(_Exit, void, (int code)) { flush_all(); ExitProcess((UINT)code); }
HLE_RAW(abort) {
    c.dump_state("abort()");
    flush_all();
    fatal("guest called abort()");
}
HLE_FUNC(__assert_rtn, void, (const char* fn, const char* file, int line, const char* expr)) {
    LOGE("ASSERTION FAILED: %s (%s:%d %s)", expr ? expr : "?", file ? file : "?", line, fn ? fn : "?");
    g_cpu->dump_state("__assert_rtn");
    fatal("guest assertion failure");
}
HLE_DATA(__stack_chk_guard, 8) { *static_cast<u64*>(p) = 0x2f8a7c3e91d4b605ull; }
HLE_RAW(__stack_chk_fail) {
    c.dump_state("stack smashing detected");
    fatal("guest __stack_chk_fail");
}
HLE_FUNC(__cxa_atexit, int, (gaddr, gaddr, gaddr)) { return 0; }
HLE_FUNC(atexit, int, (gaddr)) { return 0; }
HLE_FUNC(__cxa_finalize, void, (gaddr)) {}
HLE_FUNC(__cxa_finalize_ranges, void, (gaddr, u32)) {}
HLE_FUNC(signal, gaddr, (int, gaddr)) { return 0; }
HLE_FUNC(sigaction, int, (int, const void*, void* old)) { if (old) memset(old, 0, 16); return 0; }
HLE_FUNC(sigaltstack, int, (const void*, void* old)) { if (old) memset(old, 0, 24); return 0; }
HLE_FUNC(sigprocmask, int, (int, const void*, u32* old)) { if (old) *old = 0; return 0; }
HLE_FUNC(sigemptyset, int, (u32* s)) { *s = 0; return 0; }
HLE_FUNC(sigfillset, int, (u32* s)) { *s = ~0u; return 0; }
HLE_FUNC(sigaddset, int, (u32* s, int n)) { *s |= 1u << (n - 1); return 0; }
HLE_FUNC(sigdelset, int, (u32* s, int n)) { *s &= ~(1u << (n - 1)); return 0; }
HLE_FUNC(raise, int, (int sig)) {
    LOGW("guest raise(%d)", sig);
    if (sig == 6) { g_cpu->dump_state("raise(SIGABRT)"); fatal("SIGABRT"); }
    return 0;
}
HLE_FUNC(kill, int, (int, int)) { return 0; }
HLE_FUNC(backtrace, int, (void**, int)) { return 0; }
HLE_FUNC(backtrace_symbols, void*, (void**, int)) { return nullptr; }
HLE_FUNC(backtrace_symbols_fd, void, (void**, int, int)) {}
HLE_FUNC(openlog, void, (const char*, int, int)) {}
HLE_FUNC(closelog, void, ()) {}
HLE_RAW(syslog) {
    hle::VaReader va(c.sp());
    (void)va;
    LOGI("[syslog] %s", gptr<const char>(c.x(1)));
}
HLE_FUNC(hash_create, gaddr, (u32)) { return 0; }
HLE_FUNC(hash_search, gaddr, (gaddr, const char*, gaddr, gaddr)) { return 0; }
