// Internal definitions of the guest Objective-C runtime (Apple arm64 ABI).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef __OBJC__
typedef struct objc_object* id;
typedef struct objc_class* Class;
typedef const struct objc_selector* SEL;
#endif
typedef id (*IMP)(id, SEL, ...);
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#ifndef nil
#define nil ((id)0)
#endif
#ifndef Nil
#define Nil ((Class)0)
#endif

typedef struct method_t {
    SEL name;
    const char* types;
    IMP imp;
} method_t;
typedef method_t* Method;

typedef struct method_list_t {
    uint32_t entsize_flags;
    uint32_t count;
    // entries follow
} method_list_t;

typedef struct ivar_t {
    int32_t* offset;  // pointer to the global offset variable
    const char* name;
    const char* type;
    uint32_t alignment_raw;
    uint32_t size;
} ivar_t;
typedef ivar_t* Ivar;
typedef struct ivar_list_t {
    uint32_t entsize;
    uint32_t count;
    ivar_t first;
} ivar_list_t;

typedef struct property_t {
    const char* name;
    const char* attributes;
} property_t;
typedef struct property_list_t {
    uint32_t entsize;
    uint32_t count;
    property_t first;
} property_list_t;

typedef struct protocol_t protocol_t;
typedef struct protocol_list_t {
    uintptr_t count;
    protocol_t* list[];
} protocol_list_t;
struct protocol_t {
    id isa;
    const char* name;
    protocol_list_t* protocols;
    method_list_t* instanceMethods;
    method_list_t* classMethods;
    method_list_t* optionalInstanceMethods;
    method_list_t* optionalClassMethods;
    property_list_t* instanceProperties;
    uint32_t size;
    uint32_t flags;
    const char** extendedMethodTypes;
};

typedef struct class_ro_t {
    uint32_t flags;
    uint32_t instanceStart;
    uint32_t instanceSize;
    uint32_t reserved;
    const uint8_t* ivarLayout;
    const char* name;
    method_list_t* baseMethods;
    protocol_list_t* baseProtocols;
    ivar_list_t* ivars;
    const uint8_t* weakIvarLayout;
    property_list_t* baseProperties;
} class_ro_t;

#define RO_META (1u << 0)
#define RO_ROOT (1u << 1)
#define RO_HAS_CXX_STRUCTORS (1u << 2)
#define RO_HAS_CXX_DTOR_ONLY (1u << 8)

typedef struct category_t {
    const char* name;
    Class cls;
    method_list_t* instanceMethods;
    method_list_t* classMethods;
    protocol_list_t* protocols;
    property_list_t* instanceProperties;
    property_list_t* classProperties;
} category_t;

typedef struct cache_bucket {
    SEL sel;
    IMP imp;
} cache_bucket;
typedef struct cache_table {
    uint64_t mask;      // capacity - 1
    uint64_t occupied;  // (kept after buckets would break asm; we keep it here)
    cache_bucket buckets[];
} cache_table;

// runtime side data, pointed to by class->bits | 1
typedef struct class_rt {
    const class_ro_t* ro;
    const char* name;
    uint32_t flags;
    uint32_t instance_size;
    method_t* methods;  // flattened (categories first)
    uint32_t nmethods, capmethods;
    protocol_t** protocols;
    uint32_t nprotocols, capprotocols;
    property_t* properties;
    uint32_t nproperties, capproperties;
    Class nonmeta;  // for metaclasses: the class
    Class cls;
} class_rt;

#define RT_REALIZED (1u << 0)
#define RT_INITIALIZED (1u << 1)
#define RT_INITIALIZING (1u << 2)
#define RT_CUSTOM_RR (1u << 3)
#define RT_META (1u << 4)
#define RT_CXX_CTOR (1u << 5)
#define RT_CXX_DTOR (1u << 6)
#define RT_LOADED (1u << 7)
#define RT_ROOT (1u << 8)
#define RT_FUTURE_REG (1u << 9)

struct objc_class {
    Class isa;
    Class superclass;
    cache_table* cache;
    void* vtable;
    uintptr_t bits;
};

struct objc_object {
    Class isa;
};

struct objc_super {
    id receiver;
    Class super_class;
};

// object header placed before every heap object created by the runtime
typedef struct obj_header {
    int64_t refcount;  // extra retain count (0 == one owner)
    uint32_t magic;
    uint32_t flags;    // OBJ_DEALLOCATING, OBJ_WEAKLY_REFERENCED, OBJ_HAS_ASSOC
} obj_header;
#define OBJ_MAGIC 0x4f626a43u
#define OBJ_DEALLOCATING 1u
#define OBJ_WEAKLY_REFERENCED 2u
#define OBJ_HAS_ASSOC 4u

static inline obj_header* objc_hdr(id o) { return (obj_header*)((char*)o - sizeof(obj_header)); }

// internal API
class_rt* _cls_rt(Class c);
class_rt* _cls_realize(Class c);
IMP _objc_lookup_imp(Class cls, SEL sel, id receiver, bool forward);
void _objc_flush_caches(Class cls);
void _objc_lock(void);
void _objc_unlock(void);
void _objc_weak_clear(id obj);
void _objc_assoc_remove(id obj);
SEL _sel_register(const char* name);
id _objc_nil_imp(id, SEL, ...);
void _objc_log(const char* fmt, ...);

extern cache_table _objc_empty_cache;
