// Objective-C exceptions (minimal): throw reports and aborts; ObjC frames
// are unwound with the C++ personality so C++ exceptions pass through them.
#include "objc_private.h"

#include <stdio.h>
#include <stdlib.h>

#include <emu/msg.h>
extern const char* class_getName(Class);
extern int __gxx_personality_v0(int, int, uint64_t, void*, void*);

static void (*g_uncaught_handler)(id);

void objc_setUncaughtExceptionHandler_internal(void (*h)(id)) { g_uncaught_handler = h; }
void* objc_setUncaughtExceptionHandler(void* h) {
    void* old = (void*)g_uncaught_handler;
    g_uncaught_handler = (void (*)(id))h;
    return old;
}

__attribute__((noreturn)) void objc_exception_throw(id exc) {
    const char* cls = exc ? class_getName(exc->isa) : "nil";
    const char* reason = "";
    if (exc) {
        id r = objc_msgSend(exc, _sel_register("reason"));
        if (r) reason = (const char*)objc_msgSend(r, _sel_register("UTF8String"));
    }
    fprintf(stderr, "[objc] *** Terminating app due to uncaught exception '%s', reason: '%s'\n", cls, reason ? reason : "");
    if (g_uncaught_handler) g_uncaught_handler(exc);
    abort();
}
void objc_exception_rethrow(void) {
    fprintf(stderr, "[objc] objc_exception_rethrow\n");
    abort();
}
void* objc_begin_catch(void* exc) { return exc; }
void objc_end_catch(void) {}
void objc_terminate(void) { abort(); }

int __objc_personality_v0(int version, int actions, uint64_t cls, void* ex, void* ctx) {
    return __gxx_personality_v0(version, actions, cls, ex, ctx);
}

static int ehtype_false(void) { return 0; }
// C++ type_info vtable shape: [offset][typeinfo] then virtual functions
const void* objc_ehtype_vtable[] = {
    0, 0,
    (void*)ehtype_false, (void*)ehtype_false, (void*)ehtype_false, (void*)ehtype_false,
    (void*)ehtype_false, (void*)ehtype_false, (void*)ehtype_false, (void*)ehtype_false,
};
struct objc_typeinfo {
    const void* const* vtable;
    const char* name;
    Class cls_unremapped;
};
const struct objc_typeinfo OBJC_EHTYPE_id = {objc_ehtype_vtable + 2, "id", 0};
