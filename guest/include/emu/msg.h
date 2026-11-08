// objc_msgSend must never be called through a variadic prototype on arm64
// Apple platforms: variadic arguments are passed on the stack, while methods
// expect them in registers. This header turns `objc_msgSend(obj, sel, args...)`
// into a call through a non-variadic function type with pointer-sized
// arguments. Use explicit casts for float/struct arguments or BOOL returns.
#pragma once

#ifdef __OBJC__
typedef id emu_id_t;
#else
typedef struct objc_object* emu_id_t;
typedef const struct objc_selector* SEL;
#endif

extern emu_id_t objc_msgSend(emu_id_t, SEL, ...);

#define EMU_MSG0(o, s) ((emu_id_t(*)(emu_id_t, SEL))objc_msgSend)((emu_id_t)(o), (s))
#define EMU_MSG1(o, s, a) ((emu_id_t(*)(emu_id_t, SEL, void*))objc_msgSend)((emu_id_t)(o), (s), (void*)(a))
#define EMU_MSG2(o, s, a, b) ((emu_id_t(*)(emu_id_t, SEL, void*, void*))objc_msgSend)((emu_id_t)(o), (s), (void*)(a), (void*)(b))
#define EMU_MSG3(o, s, a, b, c) ((emu_id_t(*)(emu_id_t, SEL, void*, void*, void*))objc_msgSend)((emu_id_t)(o), (s), (void*)(a), (void*)(b), (void*)(c))
#define EMU_MSG4(o, s, a, b, c, d) \
    ((emu_id_t(*)(emu_id_t, SEL, void*, void*, void*, void*))objc_msgSend)((emu_id_t)(o), (s), (void*)(a), (void*)(b), (void*)(c), (void*)(d))
#define EMU_MSG_PICK(_1, _2, _3, _4, _5, _6, N, ...) N
#define objc_msgSend(...) EMU_MSG_PICK(__VA_ARGS__, EMU_MSG4, EMU_MSG3, EMU_MSG2, EMU_MSG1, EMU_MSG0, _unused)(__VA_ARGS__)
