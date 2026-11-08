// Synthesizes placeholder Objective-C classes for frameworks we do not
// implement (StoreKit, WebKit, Xbox Live UI, ...). They are empty subclasses of
// NSObject; the guest runtime registers them and messages to them return nil.
#include "hle.h"
#include "macho.h"
#include "vmem.h"

#include <map>
#include <mutex>

namespace objc_stubs {
namespace {
struct ClassRO {
    u32 flags, instanceStart, instanceSize, reserved;
    u64 ivarLayout, name, baseMethods, baseProtocols, ivars, weakIvarLayout, baseProperties;
};
struct ClassT {
    u64 isa, superclass, cache, vtable, data;
};
std::mutex g_lock;
std::map<std::string, std::pair<ClassT*, ClassT*>> g_classes;
std::vector<u64> g_list;
}  // namespace

gaddr synthesize_class(const std::string& name, bool meta) {
    std::lock_guard lk(g_lock);
    auto it = g_classes.find(name);
    if (it == g_classes.end()) {
        gaddr root = macho::find_export("_OBJC_CLASS_$_NSObject");
        gaddr rootmeta = macho::find_export("_OBJC_METACLASS_$_NSObject");
        gaddr cache = macho::find_export("__objc_empty_cache");
        auto* cls = static_cast<ClassT*>(vmem::gcalloc(1, sizeof(ClassT)));
        auto* mcls = static_cast<ClassT*>(vmem::gcalloc(1, sizeof(ClassT)));
        auto* ro = static_cast<ClassRO*>(vmem::gcalloc(1, sizeof(ClassRO)));
        auto* mro = static_cast<ClassRO*>(vmem::gcalloc(1, sizeof(ClassRO)));
        char* n = static_cast<char*>(vmem::gmalloc(name.size() + 1));
        memcpy(n, name.c_str(), name.size() + 1);
        ro->instanceStart = ro->instanceSize = 8;
        ro->name = gaddr_of(n);
        mro->flags = 1;  // RO_META
        mro->instanceStart = mro->instanceSize = 40;
        mro->name = gaddr_of(n);
        cls->isa = gaddr_of(mcls);
        cls->superclass = root;
        cls->cache = cache;
        cls->data = gaddr_of(ro);
        mcls->isa = rootmeta;
        mcls->superclass = rootmeta;
        mcls->cache = cache;
        mcls->data = gaddr_of(mro);
        it = g_classes.emplace(name, std::make_pair(cls, mcls)).first;
        g_list.push_back(gaddr_of(cls));
        LOGD("synthesized ObjC class %s", name.c_str());
    }
    return gaddr_of(meta ? it->second.second : it->second.first);
}
}  // namespace objc_stubs

// Called by the guest runtime (libobjc) while mapping images.
HLE_FUNC(emu_objc_synthesized_classes, const u64*, (u64* count)) {
    *count = objc_stubs::g_list.size();
    return objc_stubs::g_list.data();
}
