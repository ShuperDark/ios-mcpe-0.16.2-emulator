// Root class NSObject and the block classes (compiled without ARC).
#include "objc_private.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long NSUInteger;
typedef struct _NSZone NSZone;

extern id class_createInstance(Class, size_t);
extern id object_dispose(id);
extern id _objc_rootRetain(id);
extern void _objc_rootRelease(id);
extern id _objc_rootAutorelease(id);
extern uintptr_t _objc_rootRetainCount(id);
extern bool _objc_rootTryRetain(id);
extern bool _objc_rootIsDeallocating(id);
extern Class object_getClass(id);
extern Class class_getSuperclass(Class);
extern BOOL class_respondsToSelector(Class, SEL);
extern IMP class_getMethodImplementation(Class, SEL);
extern BOOL class_conformsToProtocol(Class, void*);
extern BOOL class_isMetaClass(Class);
extern const char* class_getName(Class);
#include <emu/msg.h>
extern void* _Block_copy(const void*);
extern void _Block_release(const void*);
extern id objc_autorelease(id);

__attribute__((objc_root_class))
@interface NSObject {
    Class isa;
}
@end

@implementation NSObject

+ (void)initialize {}
+ (id)alloc { return objc_msgSend((id)self, @selector(allocWithZone:), (void*)0); }
+ (id)allocWithZone:(NSZone*)z { (void)z; return class_createInstance((Class)self, 0); }
+ (id)new { return objc_msgSend(objc_msgSend((id)self, @selector(alloc)), @selector(init)); }
- (id)init { return self; }
- (void)dealloc { object_dispose(self); }
- (void)finalize {}

- (id)retain { return _objc_rootRetain(self); }
- (oneway void)release { _objc_rootRelease(self); }
- (id)autorelease { return _objc_rootAutorelease(self); }
- (NSUInteger)retainCount { return _objc_rootRetainCount(self); }
- (BOOL)_tryRetain { return _objc_rootTryRetain(self); }
- (BOOL)_isDeallocating { return _objc_rootIsDeallocating(self); }
- (BOOL)allowsWeakReference { return !_objc_rootIsDeallocating(self); }
- (BOOL)retainWeakReference { return _objc_rootTryRetain(self); }
+ (id)retain { return self; }
+ (oneway void)release {}
+ (id)autorelease { return self; }
+ (NSUInteger)retainCount { return (NSUInteger)-1 >> 1; }
+ (BOOL)_tryRetain { return YES; }
+ (BOOL)_isDeallocating { return NO; }
+ (BOOL)allowsWeakReference { return YES; }
+ (BOOL)retainWeakReference { return YES; }

+ (Class)class { return (Class)self; }
- (Class)class { return object_getClass(self); }
+ (Class)superclass { return class_getSuperclass((Class)self); }
- (Class)superclass { return class_getSuperclass(object_getClass(self)); }
- (id)self { return self; }
- (NSZone*)zone { return NULL; }
+ (NSZone*)zone { return NULL; }
- (BOOL)isProxy { return NO; }
- (NSUInteger)hash { return (NSUInteger)self; }
- (BOOL)isEqual:(id)o { return self == o; }
+ (NSUInteger)hash { return (NSUInteger)self; }
+ (BOOL)isEqual:(id)o { return self == o; }

- (BOOL)isKindOfClass:(Class)c {
    for (Class k = object_getClass(self); k; k = class_getSuperclass(k))
        if (k == c) return YES;
    return NO;
}
+ (BOOL)isKindOfClass:(Class)c {
    for (Class k = object_getClass(self); k; k = class_getSuperclass(k))
        if (k == c) return YES;
    return NO;
}
- (BOOL)isMemberOfClass:(Class)c { return object_getClass(self) == c; }
+ (BOOL)isMemberOfClass:(Class)c { return object_getClass(self) == c; }
+ (BOOL)isSubclassOfClass:(Class)c {
    for (Class k = (Class)self; k; k = class_getSuperclass(k))
        if (k == c) return YES;
    return NO;
}
- (BOOL)respondsToSelector:(SEL)s { return s && class_respondsToSelector(object_getClass(self), s); }
+ (BOOL)respondsToSelector:(SEL)s { return s && class_respondsToSelector(object_getClass(self), s); }
+ (BOOL)instancesRespondToSelector:(SEL)s { return s && class_respondsToSelector((Class)self, s); }
- (BOOL)conformsToProtocol:(id)p { return class_conformsToProtocol(object_getClass(self), (void*)p); }
+ (BOOL)conformsToProtocol:(id)p {
    for (Class k = (Class)self; k; k = class_getSuperclass(k))
        if (class_conformsToProtocol(k, (void*)p)) return YES;
    return NO;
}
- (IMP)methodForSelector:(SEL)s { return class_getMethodImplementation(object_getClass(self), s); }
+ (IMP)methodForSelector:(SEL)s { return class_getMethodImplementation(object_getClass(self), s); }
+ (IMP)instanceMethodForSelector:(SEL)s { return class_getMethodImplementation((Class)self, s); }

- (id)performSelector:(SEL)s { return ((id(*)(id, SEL))objc_msgSend)(self, s); }
- (id)performSelector:(SEL)s withObject:(id)a { return ((id(*)(id, SEL, id))objc_msgSend)(self, s, a); }
- (id)performSelector:(SEL)s withObject:(id)a withObject:(id)b { return ((id(*)(id, SEL, id, id))objc_msgSend)(self, s, a, b); }
+ (id)performSelector:(SEL)s { return ((id(*)(id, SEL))objc_msgSend)(self, s); }
+ (id)performSelector:(SEL)s withObject:(id)a { return ((id(*)(id, SEL, id))objc_msgSend)(self, s, a); }

- (id)copy { return objc_msgSend(self, @selector(copyWithZone:), (void*)0); }
- (id)mutableCopy { return objc_msgSend(self, @selector(mutableCopyWithZone:), (void*)0); }
+ (id)copy { return self; }
+ (id)copyWithZone:(NSZone*)z { (void)z; return self; }
+ (id)mutableCopyWithZone:(NSZone*)z { (void)z; return self; }

- (id)forwardingTargetForSelector:(SEL)s { (void)s; return nil; }
+ (id)forwardingTargetForSelector:(SEL)s { (void)s; return nil; }
+ (BOOL)resolveClassMethod:(SEL)s { (void)s; return NO; }
+ (BOOL)resolveInstanceMethod:(SEL)s { (void)s; return NO; }
- (void)doesNotRecognizeSelector:(SEL)s {
    fprintf(stderr, "[objc] -[%s %s]: unrecognized selector\n", class_getName(object_getClass(self)), (const char*)s);
}
+ (void)doesNotRecognizeSelector:(SEL)s {
    fprintf(stderr, "[objc] +[%s %s]: unrecognized selector\n", class_getName((Class)self), (const char*)s);
}
@end

// ---- block classes ----
@interface __NSBlockBase : NSObject
@end
@implementation __NSBlockBase
- (id)retain { return (id)_Block_copy(self); }
- (oneway void)release { _Block_release(self); }
- (id)copy { return (id)_Block_copy(self); }
- (id)copyWithZone:(NSZone*)z { (void)z; return (id)_Block_copy(self); }
- (id)autorelease { return objc_autorelease(self); }
- (NSUInteger)retainCount { return 1; }
- (BOOL)_tryRetain { return YES; }
- (BOOL)allowsWeakReference { return YES; }
- (BOOL)retainWeakReference { return YES; }
- (void)dealloc {}
@end
@interface __NSStackBlock__ : __NSBlockBase
@end
@implementation __NSStackBlock__
- (id)autorelease { return self; }
@end
@interface __NSMallocBlock__ : __NSBlockBase
@end
@implementation __NSMallocBlock__
@end
@interface __NSGlobalBlock__ : __NSBlockBase
@end
@implementation __NSGlobalBlock__
- (id)retain { return self; }
- (oneway void)release {}
- (id)copy { return self; }
- (id)autorelease { return self; }
@end

void _objc_root_init(void) {}
