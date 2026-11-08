// Minimal Foundation / CoreFoundation interface used by the guest frameworks.
// Implementation: guest/Foundation (manual retain/release Objective-C).
#pragma once
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef long NSInteger;
typedef unsigned long NSUInteger;
typedef double CGFloat;
typedef double NSTimeInterval;
typedef unsigned short unichar;
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#ifndef nil
#define nil ((id)0)
#endif
#ifndef Nil
#define Nil ((Class)0)
#endif
typedef id (*IMP)(id, SEL, ...);
#define NSNotFound ((NSInteger)0x7fffffffffffffffL)
typedef struct _NSZone NSZone;
typedef struct _NSRange { NSUInteger location, length; } NSRange;
static inline NSRange NSMakeRange(NSUInteger l, NSUInteger n) { NSRange r = {l, n}; return r; }
static inline NSUInteger NSMaxRange(NSRange r) { return r.location + r.length; }
typedef struct CGPoint { CGFloat x, y; } CGPoint;
typedef struct CGSize { CGFloat width, height; } CGSize;
typedef struct CGRect { CGPoint origin; CGSize size; } CGRect;
typedef struct CGAffineTransform { CGFloat a, b, c, d, tx, ty; } CGAffineTransform;
typedef struct UIEdgeInsets { CGFloat top, left, bottom, right; } UIEdgeInsets;
static inline CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat w, CGFloat h) { CGRect r = {{x, y}, {w, h}}; return r; }
static inline CGPoint CGPointMake(CGFloat x, CGFloat y) { CGPoint p = {x, y}; return p; }
static inline CGSize CGSizeMake(CGFloat w, CGFloat h) { CGSize s = {w, h}; return s; }
typedef NSInteger NSComparisonResult;
#define NSOrderedAscending (-1L)
#define NSOrderedSame 0L
#define NSOrderedDescending 1L
typedef NSUInteger NSStringEncoding;
#define NSASCIIStringEncoding 1
#define NSUTF8StringEncoding 4
#define NSISOLatin1StringEncoding 5
#define NSNonLossyASCIIStringEncoding 7
#define NSUnicodeStringEncoding 10
#define NSWindowsCP1252StringEncoding 12
#define NSUTF16StringEncoding 10
#define NSUTF16BigEndianStringEncoding 0x90000100
#define NSUTF16LittleEndianStringEncoding 0x94000100
#define NSUTF32StringEncoding 0x8c000100
#define NSUTF32LittleEndianStringEncoding 0x9c000100
#define NSMacOSRomanStringEncoding 30
enum {
    NSCaseInsensitiveSearch = 1, NSLiteralSearch = 2, NSBackwardsSearch = 4, NSAnchoredSearch = 8, NSNumericSearch = 64,
    NSDiacriticInsensitiveSearch = 128, NSWidthInsensitiveSearch = 256, NSForcedOrderingSearch = 512,
    NSRegularExpressionSearch = 1024
};

typedef NSComparisonResult (^NSComparator)(id a, id b);

#define NS_RETURNS_RETAINED __attribute__((ns_returns_retained))
#define EMU_EXPORT __attribute__((visibility("default")))

@class Protocol;
@class NSString, NSArray, NSDictionary, NSData, NSError, NSURL, NSNumber, NSDate, NSMutableArray, NSMutableDictionary,
    NSSet, NSLocale, NSCharacterSet, NSEnumerator, NSMethodSignature, NSInvocation, NSCoder, NSMutableString,
    NSTimeZone, NSThread, NSRunLoop, NSTimer, NSNotification, NSOperationQueue;

@protocol NSObject
@end
@protocol NSCopying
- (id)copyWithZone:(NSZone*)zone;
@end
@protocol NSMutableCopying
- (id)mutableCopyWithZone:(NSZone*)zone;
@end
@protocol NSCoding
@end
@protocol NSSecureCoding
@end
@protocol NSFastEnumeration
@end

__attribute__((objc_root_class))
@interface NSObject <NSObject> {
    Class isa;
}
+ (void)initialize;
+ (id)alloc;
+ (id)allocWithZone:(NSZone*)z;
+ (id)new;
- (id)init;
- (void)dealloc;
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (NSUInteger)retainCount;
+ (Class)class;
- (Class)class;
+ (Class)superclass;
- (Class)superclass;
- (id)self;
- (NSUInteger)hash;
- (BOOL)isEqual:(id)o;
- (BOOL)isKindOfClass:(Class)c;
- (BOOL)isMemberOfClass:(Class)c;
+ (BOOL)isSubclassOfClass:(Class)c;
- (BOOL)respondsToSelector:(SEL)s;
+ (BOOL)instancesRespondToSelector:(SEL)s;
- (BOOL)conformsToProtocol:(Protocol*)p;
- (id)performSelector:(SEL)s;
- (id)performSelector:(SEL)s withObject:(id)a;
- (id)performSelector:(SEL)s withObject:(id)a withObject:(id)b;
- (id)copy;
- (id)mutableCopy;
- (IMP)methodForSelector:(SEL)s;
- (void)doesNotRecognizeSelector:(SEL)s;
// Foundation additions
- (NSString*)description;
- (NSString*)debugDescription;
+ (NSString*)description;
- (id)valueForKey:(NSString*)key;
- (void)setValue:(id)v forKey:(NSString*)key;
- (id)valueForKeyPath:(NSString*)kp;
- (void)setValue:(id)v forKeyPath:(NSString*)kp;
- (void)performSelector:(SEL)s withObject:(id)a afterDelay:(NSTimeInterval)d;
- (void)performSelectorOnMainThread:(SEL)s withObject:(id)a waitUntilDone:(BOOL)w;
@end

// ---------------------------------------------------------------- strings
@interface NSString : NSObject <NSCopying, NSMutableCopying>
+ (instancetype)string;
+ (instancetype)stringWithString:(NSString*)s;
+ (instancetype)stringWithUTF8String:(const char*)s;
+ (instancetype)stringWithCString:(const char*)s encoding:(NSStringEncoding)e;
+ (instancetype)stringWithFormat:(NSString*)fmt, ...;
+ (instancetype)stringWithCharacters:(const unichar*)c length:(NSUInteger)n;
+ (instancetype)stringWithContentsOfFile:(NSString*)p encoding:(NSStringEncoding)e error:(NSError**)err;
- (instancetype)initWithUTF8String:(const char*)s;
- (instancetype)initWithCharacters:(const unichar*)c length:(NSUInteger)n;
- (instancetype)initWithBytes:(const void*)b length:(NSUInteger)n encoding:(NSStringEncoding)e;
- (instancetype)initWithFormat:(NSString*)fmt arguments:(va_list)ap;
- (instancetype)initWithFormat:(NSString*)fmt, ...;
- (instancetype)initWithString:(NSString*)s;
- (instancetype)initWithData:(NSData*)d encoding:(NSStringEncoding)e;
- (NSUInteger)length;
- (unichar)characterAtIndex:(NSUInteger)i;
- (void)getCharacters:(unichar*)buf range:(NSRange)r;
- (const char*)UTF8String;
- (BOOL)isEqualToString:(NSString*)s;
- (NSComparisonResult)compare:(NSString*)s;
- (NSComparisonResult)compare:(NSString*)s options:(NSUInteger)o;
- (NSComparisonResult)caseInsensitiveCompare:(NSString*)s;
- (BOOL)hasPrefix:(NSString*)s;
- (BOOL)hasSuffix:(NSString*)s;
- (NSRange)rangeOfString:(NSString*)s;
- (NSRange)rangeOfString:(NSString*)s options:(NSUInteger)o;
- (BOOL)containsString:(NSString*)s;
- (NSRange)rangeOfComposedCharacterSequenceAtIndex:(NSUInteger)i;
- (NSString*)stringByAddingPercentEncodingWithAllowedCharacters:(id)set;
- (NSString*)stringByRemovingPercentEncoding;
- (NSString*)stringByAppendingPathExtension:(NSString*)e;
- (NSArray*)pathComponents;
- (NSString*)stringByReplacingOccurrencesOfString:(NSString*)a withString:(NSString*)b;
- (NSString*)stringByPaddingToLength:(NSUInteger)n withString:(NSString*)pad startingAtIndex:(NSUInteger)start;
- (NSString*)stringByTrimmingCharactersInSet:(id)set;
+ (NSString*)pathWithComponents:(NSArray*)c;
- (NSRange)rangeOfString:(NSString*)s options:(NSUInteger)o range:(NSRange)r;
- (NSString*)substringFromIndex:(NSUInteger)i;
- (NSString*)substringToIndex:(NSUInteger)i;
- (NSString*)substringWithRange:(NSRange)r;
- (NSString*)stringByAppendingString:(NSString*)s;
- (NSString*)stringByAppendingFormat:(NSString*)fmt, ...;
- (NSString*)stringByAppendingPathComponent:(NSString*)c;
- (NSString*)lastPathComponent;
- (NSString*)pathExtension;
- (NSString*)stringByDeletingLastPathComponent;
- (NSString*)stringByDeletingPathExtension;
- (NSArray*)componentsSeparatedByString:(NSString*)sep;
- (NSData*)dataUsingEncoding:(NSStringEncoding)e;
- (int)intValue;
- (NSInteger)integerValue;
- (long long)longLongValue;
- (double)doubleValue;
- (float)floatValue;
- (BOOL)boolValue;
- (NSString*)lowercaseString;
- (NSString*)uppercaseString;
- (const char*)fileSystemRepresentation;
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a encoding:(NSStringEncoding)e error:(NSError**)err;
@end
@interface NSMutableString : NSString
+ (instancetype)stringWithCapacity:(NSUInteger)n;
- (void)appendString:(NSString*)s;
- (void)appendFormat:(NSString*)fmt, ...;
- (void)replaceCharactersInRange:(NSRange)r withString:(NSString*)s;
- (void)insertString:(NSString*)s atIndex:(NSUInteger)i;
- (void)deleteCharactersInRange:(NSRange)r;
- (void)setString:(NSString*)s;
- (NSUInteger)replaceOccurrencesOfString:(NSString*)a withString:(NSString*)b options:(NSUInteger)opt range:(NSRange)r;
@end

// ---------------------------------------------------------------- values
@interface NSValue : NSObject <NSCopying>
+ (NSValue*)valueWithBytes:(const void*)v objCType:(const char*)t;
+ (NSValue*)valueWithPointer:(const void*)p;
+ (NSValue*)valueWithNonretainedObject:(id)o;
+ (NSValue*)valueWithRange:(NSRange)r;
+ (NSValue*)valueWithCGRect:(CGRect)r;
+ (NSValue*)valueWithCGPoint:(CGPoint)p;
+ (NSValue*)valueWithCGSize:(CGSize)s;
- (void)getValue:(void*)buf;
- (const char*)objCType;
- (void*)pointerValue;
- (id)nonretainedObjectValue;
- (NSRange)rangeValue;
- (CGRect)CGRectValue;
- (CGPoint)CGPointValue;
- (CGSize)CGSizeValue;
@end
@interface NSNumber : NSValue
+ (NSNumber*)numberWithBool:(BOOL)v;
+ (NSNumber*)numberWithChar:(char)v;
+ (NSNumber*)numberWithInt:(int)v;
+ (NSNumber*)numberWithUnsignedInt:(unsigned)v;
+ (NSNumber*)numberWithInteger:(NSInteger)v;
+ (NSNumber*)numberWithUnsignedInteger:(NSUInteger)v;
+ (NSNumber*)numberWithLong:(long)v;
+ (NSNumber*)numberWithUnsignedLong:(unsigned long)v;
+ (NSNumber*)numberWithLongLong:(long long)v;
+ (NSNumber*)numberWithUnsignedLongLong:(unsigned long long)v;
+ (NSNumber*)numberWithFloat:(float)v;
+ (NSNumber*)numberWithDouble:(double)v;
- (BOOL)boolValue;
- (char)charValue;
- (int)intValue;
- (unsigned)unsignedIntValue;
- (NSInteger)integerValue;
- (NSUInteger)unsignedIntegerValue;
- (long long)longLongValue;
- (unsigned long long)unsignedLongLongValue;
- (float)floatValue;
- (double)doubleValue;
- (NSString*)stringValue;
- (NSComparisonResult)compare:(NSNumber*)o;
- (BOOL)isEqualToNumber:(NSNumber*)o;
@end
@interface NSNull : NSObject <NSCopying>
+ (NSNull*)null;
@end
@interface NSDate : NSObject <NSCopying>
+ (instancetype)date;
+ (instancetype)dateWithTimeIntervalSinceNow:(NSTimeInterval)t;
+ (instancetype)dateWithTimeIntervalSince1970:(NSTimeInterval)t;
+ (instancetype)dateWithTimeIntervalSinceReferenceDate:(NSTimeInterval)t;
+ (instancetype)distantFuture;
+ (instancetype)distantPast;
+ (NSTimeInterval)timeIntervalSinceReferenceDate;
- (instancetype)initWithTimeIntervalSinceReferenceDate:(NSTimeInterval)t;
- (NSTimeInterval)timeIntervalSinceReferenceDate;
- (NSTimeInterval)timeIntervalSince1970;
- (NSTimeInterval)timeIntervalSinceNow;
- (NSTimeInterval)timeIntervalSinceDate:(NSDate*)d;
- (NSComparisonResult)compare:(NSDate*)d;
@end
@interface NSData : NSObject <NSCopying, NSMutableCopying>
+ (instancetype)data;
+ (instancetype)dataWithBytes:(const void*)b length:(NSUInteger)n;
+ (instancetype)dataWithBytesNoCopy:(void*)b length:(NSUInteger)n;
+ (instancetype)dataWithBytesNoCopy:(void*)b length:(NSUInteger)n freeWhenDone:(BOOL)f;
+ (instancetype)dataWithContentsOfFile:(NSString*)p;
+ (instancetype)dataWithData:(NSData*)d;
- (instancetype)initWithBytes:(const void*)b length:(NSUInteger)n;
- (instancetype)initWithBytesNoCopy:(void*)b length:(NSUInteger)n freeWhenDone:(BOOL)f;
- (instancetype)initWithContentsOfFile:(NSString*)p;
- (const void*)bytes;
- (NSUInteger)length;
- (void)getBytes:(void*)buf length:(NSUInteger)n;
- (void)getBytes:(void*)buf range:(NSRange)r;
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a;
- (BOOL)isEqualToData:(NSData*)d;
- (NSData*)subdataWithRange:(NSRange)r;
@end
@interface NSMutableData : NSData
+ (instancetype)dataWithCapacity:(NSUInteger)n;
+ (instancetype)dataWithLength:(NSUInteger)n;
- (void*)mutableBytes;
- (void)setLength:(NSUInteger)n;
- (void)appendBytes:(const void*)b length:(NSUInteger)n;
- (void)appendData:(NSData*)d;
- (void)increaseLengthBy:(NSUInteger)n;
- (void)replaceBytesInRange:(NSRange)r withBytes:(const void*)b;
@end

// ---------------------------------------------------------------- collections
typedef struct {
    unsigned long state;
    id* itemsPtr;
    unsigned long* mutationsPtr;
    unsigned long extra[5];
} NSFastEnumerationState;

@interface NSEnumerator : NSObject <NSFastEnumeration>
- (id)nextObject;
- (NSArray*)allObjects;
@end
@interface NSArray : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
+ (instancetype)array;
+ (instancetype)arrayWithObject:(id)o;
+ (instancetype)arrayWithObjects:(id)first, ...;
+ (instancetype)arrayWithObjects:(const id*)objs count:(NSUInteger)n;
+ (instancetype)arrayWithArray:(NSArray*)a;
- (instancetype)initWithObjects:(const id*)objs count:(NSUInteger)n;
- (instancetype)initWithArray:(NSArray*)a;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)i;
- (id)objectAtIndexedSubscript:(NSUInteger)i;
- (id)firstObject;
- (id)lastObject;
- (BOOL)containsObject:(id)o;
- (NSUInteger)indexOfObject:(id)o;
- (NSArray*)arrayByAddingObject:(id)o;
- (NSArray*)arrayByAddingObjectsFromArray:(NSArray*)a;
- (NSString*)componentsJoinedByString:(NSString*)s;
- (NSEnumerator*)objectEnumerator;
- (NSEnumerator*)reverseObjectEnumerator;
- (NSArray*)sortedArrayUsingSelector:(SEL)s;
- (NSArray*)sortedArrayUsingComparator:(NSComparator)c;
- (NSArray*)subarrayWithRange:(NSRange)r;
- (void)makeObjectsPerformSelector:(SEL)s;
- (void)enumerateObjectsUsingBlock:(void (^)(id obj, NSUInteger idx, BOOL* stop))b;
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n;
@end
@interface NSMutableArray : NSArray
+ (instancetype)arrayWithCapacity:(NSUInteger)n;
- (instancetype)initWithCapacity:(NSUInteger)n;
- (void)addObject:(id)o;
- (void)addObjectsFromArray:(NSArray*)a;
- (void)insertObject:(id)o atIndex:(NSUInteger)i;
- (void)removeObjectAtIndex:(NSUInteger)i;
- (void)removeObject:(id)o;
- (void)removeObjectIdenticalTo:(id)o;
- (void)removeLastObject;
- (void)removeAllObjects;
- (void)replaceObjectAtIndex:(NSUInteger)i withObject:(id)o;
- (void)setObject:(id)o atIndexedSubscript:(NSUInteger)i;
- (void)sortUsingSelector:(SEL)s;
- (void)sortUsingComparator:(NSComparator)c;
@end
@interface NSDictionary : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
+ (instancetype)dictionary;
+ (instancetype)dictionaryWithObject:(id)o forKey:(id)k;
+ (instancetype)dictionaryWithObjects:(const id*)o forKeys:(const id*)k count:(NSUInteger)n;
+ (instancetype)dictionaryWithObjectsAndKeys:(id)first, ...;
+ (instancetype)dictionaryWithDictionary:(NSDictionary*)d;
+ (instancetype)dictionaryWithContentsOfFile:(NSString*)p;
- (instancetype)initWithObjects:(const id*)o forKeys:(const id*)k count:(NSUInteger)n;
- (instancetype)initWithDictionary:(NSDictionary*)d;
- (NSUInteger)count;
- (id)objectForKey:(id)k;
- (id)objectForKeyedSubscript:(id)k;
- (NSArray*)allKeys;
- (NSArray*)allValues;
- (NSEnumerator*)keyEnumerator;
- (NSEnumerator*)objectEnumerator;
- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(id key, id obj, BOOL* stop))b;
- (BOOL)writeToFile:(NSString*)p atomically:(BOOL)a;
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n;
@end
@interface NSMutableDictionary : NSDictionary
+ (instancetype)dictionaryWithCapacity:(NSUInteger)n;
- (instancetype)initWithCapacity:(NSUInteger)n;
- (void)setObject:(id)o forKey:(id)k;
- (void)setObject:(id)o forKeyedSubscript:(id)k;
- (void)removeObjectForKey:(id)k;
- (void)removeAllObjects;
- (void)addEntriesFromDictionary:(NSDictionary*)d;
@end
@interface NSSet : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
+ (instancetype)set;
+ (instancetype)setWithObject:(id)o;
+ (instancetype)setWithArray:(NSArray*)a;
+ (instancetype)setWithObjects:(id)first, ...;
- (instancetype)initWithObjects:(const id*)o count:(NSUInteger)n;
- (NSUInteger)count;
- (BOOL)containsObject:(id)o;
- (id)member:(id)o;
- (id)anyObject;
- (NSArray*)allObjects;
- (NSEnumerator*)objectEnumerator;
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState*)st objects:(id*)buf count:(NSUInteger)n;
@end
@interface NSMutableSet : NSSet
+ (instancetype)setWithCapacity:(NSUInteger)n;
- (void)addObject:(id)o;
- (void)removeObject:(id)o;
- (void)removeAllObjects;
- (void)addObjectsFromArray:(NSArray*)a;
@end

// ---------------------------------------------------------------- misc
@interface NSError : NSObject <NSCopying>
+ (instancetype)errorWithDomain:(NSString*)d code:(NSInteger)c userInfo:(NSDictionary*)u;
- (instancetype)initWithDomain:(NSString*)d code:(NSInteger)c userInfo:(NSDictionary*)u;
- (NSString*)domain;
- (NSInteger)code;
- (NSDictionary*)userInfo;
- (NSString*)localizedDescription;
@end
__attribute__((objc_exception))
@interface NSException : NSObject
+ (NSException*)exceptionWithName:(NSString*)n reason:(NSString*)r userInfo:(NSDictionary*)u;
+ (void)raise:(NSString*)n format:(NSString*)fmt, ...;
- (instancetype)initWithName:(NSString*)n reason:(NSString*)r userInfo:(NSDictionary*)u;
- (void)raise;
- (NSString*)name;
- (NSString*)reason;
- (NSDictionary*)userInfo;
@end
@interface NSNotification : NSObject
+ (instancetype)notificationWithName:(NSString*)n object:(id)o userInfo:(NSDictionary*)u;
- (NSString*)name;
- (id)object;
- (NSDictionary*)userInfo;
@end
@interface NSNotificationCenter : NSObject
+ (NSNotificationCenter*)defaultCenter;
- (void)addObserver:(id)o selector:(SEL)s name:(NSString*)n object:(id)obj;
- (void)removeObserver:(id)o;
- (void)removeObserver:(id)o name:(NSString*)n object:(id)obj;
- (void)postNotification:(NSNotification*)n;
- (void)postNotificationName:(NSString*)n object:(id)o;
- (void)postNotificationName:(NSString*)n object:(id)o userInfo:(NSDictionary*)u;
@end
@interface NSTimer : NSObject
+ (NSTimer*)scheduledTimerWithTimeInterval:(NSTimeInterval)ti target:(id)t selector:(SEL)s userInfo:(id)u repeats:(BOOL)r;
+ (NSTimer*)timerWithTimeInterval:(NSTimeInterval)ti target:(id)t selector:(SEL)s userInfo:(id)u repeats:(BOOL)r;
- (void)invalidate;
- (void)fire;
- (BOOL)isValid;
- (id)userInfo;
@end
@interface NSRunLoop : NSObject
+ (NSRunLoop*)currentRunLoop;
+ (NSRunLoop*)mainRunLoop;
- (void)addTimer:(NSTimer*)t forMode:(NSString*)m;
- (void)run;
- (BOOL)runMode:(NSString*)m beforeDate:(NSDate*)d;
- (void)runUntilDate:(NSDate*)d;
@end
@interface NSThread : NSObject
+ (NSThread*)currentThread;
+ (NSThread*)mainThread;
+ (BOOL)isMainThread;
- (BOOL)isMainThread;
+ (void)sleepForTimeInterval:(NSTimeInterval)t;
- (NSMutableDictionary*)threadDictionary;
@end

@interface NSBundle : NSObject
+ (NSBundle*)mainBundle;
- (NSDictionary*)infoDictionary;
- (id)objectForInfoDictionaryKey:(NSString*)k;
- (NSString*)bundlePath;
- (NSString*)resourcePath;
- (NSString*)pathForResource:(NSString*)name ofType:(NSString*)ext;
@end
@interface NSUserDefaults : NSObject
+ (NSUserDefaults*)standardUserDefaults;
- (id)objectForKey:(NSString*)k;
- (void)setObject:(id)v forKey:(NSString*)k;
- (BOOL)synchronize;
@end

// ---- internal helpers shared between guest frameworks ----
#ifdef __cplusplus
extern "C" {
#endif
EMU_EXPORT void NSLog(NSString* fmt, ...);
EMU_EXPORT Class NSClassFromString(NSString* s);
EMU_EXPORT NSString* NSStringFromClass(Class c);
EMU_EXPORT SEL NSSelectorFromString(NSString* s);
EMU_EXPORT NSString* NSStringFromSelector(SEL s);
EMU_EXPORT NSString* NSHomeDirectory(void);
EMU_EXPORT NSString* NSTemporaryDirectory(void);
EMU_EXPORT NSArray* NSSearchPathForDirectoriesInDomains(NSUInteger dir, NSUInteger mask, BOOL expand);
EMU_EXPORT void NSLogv(NSString* fmt, va_list ap);
// Runs one iteration of the main run loop (timers, performSelector, main dispatch queue).
EMU_EXPORT void _emu_runloop_iteration(NSTimeInterval max_wait);
EMU_EXPORT NSTimeInterval _emu_now(void);  // seconds, monotonic
EMU_EXPORT NSString* _emu_string_from_utf8(const char* s);
EMU_EXPORT id _emu_plist_parse(NSData* data);
EMU_EXPORT NSData* _emu_plist_xml(id obj);
extern void _dispatch_main_queue_drain(void);
#ifdef __cplusplus
}
#endif
