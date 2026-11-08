// Mach-O (arm64) image loader - a tiny dyld.
#pragma once
#include "common.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>

struct MachSection {
    std::string segname, sectname;
    gaddr addr = 0;
    u64 size = 0;
    u32 flags = 0;
};

struct MachSegment {
    std::string name;
    gaddr addr = 0;  // slid
    u64 vmsize = 0;
    u32 initprot = 0;
};

struct MachBind {
    gaddr where;         // slid address of the pointer to patch
    std::string symbol;  // with leading underscore
    s64 addend;
    int ordinal;
    bool weak_import;
    bool lazy;
    bool weak_def;  // from weak-bind stream (coalescing)
};

struct TlvInfo {
    gaddr template_start = 0;  // slid address of __thread_data
    u64 template_data_size = 0;
    u64 template_total_size = 0;  // data + bss
    u32 key = 0;
};

class MachImage {
public:
    std::string path;          // host path the image was loaded from
    std::string install_name;  // guest-visible path (e.g. /usr/lib/libc++.1.dylib)
    gaddr base = 0;            // address of the mach header (slid)
    s64 slide = 0;
    gaddr vm_start = 0, vm_end = 0;
    gaddr entry = 0;  // LC_MAIN entry (slid) or 0
    bool is_main = false;
    u32 filetype = 0;

    std::vector<MachSegment> segments;
    std::vector<MachSection> sections;
    std::vector<MachBind> binds;
    std::vector<std::string> dylibs;  // LC_LOAD_DYLIB names in ordinal order
    std::unordered_map<std::string, gaddr> exports;
    std::unordered_map<std::string, bool> export_weak;
    // local+external symbols from the symbol table (for diagnostics / symbolication)
    std::map<gaddr, std::string> symbols;
    // defined symbols by name, including non-exported ones (self references, see bind_all)
    std::unordered_map<std::string, gaddr> definitions;
    TlvInfo tlv;

    const MachSection* find_section(const char* seg, const char* sect) const;
    const MachSegment* find_segment(const char* seg) const;
    bool contains(gaddr a) const { return a >= vm_start && a < vm_end; }
};

namespace macho {

// Loads (maps, rebases, parses exports/binds) but does not bind.
// preferred_base: 0 = use the image's own vmaddr when possible.
std::shared_ptr<MachImage> load(const std::string& host_path, const std::string& guest_path, gaddr preferred_base);

using SymbolResolver = std::function<gaddr(const MachImage& img, const MachBind& b, bool& ok)>;
void bind_all(MachImage& img, const SymbolResolver& resolve);

const std::vector<std::shared_ptr<MachImage>>& images();
void register_image(std::shared_ptr<MachImage> img);
MachImage* image_for_address(gaddr a);
// Best-effort symbolication: "image!symbol+0x12"
std::string symbolicate(gaddr a);
// Global export lookup across all images (load order). Returns 0 if not found.
gaddr find_export(const std::string& name);

}  // namespace macho
