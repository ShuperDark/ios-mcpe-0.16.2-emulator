// QuartzCore (CALayer, CAEAGLLayer, CADisplayLink, CATransaction...) and OpenGLES (EAGLContext).
#import <emu/UIKit.h>
#include <emu/host.h>

#include <pthread.h>
#include <stdio.h>

#include <emu/msg.h>

EMU_EXPORT NSString* const kEAGLDrawablePropertyRetainedBacking = @"EAGLDrawablePropertyRetainedBacking";
EMU_EXPORT NSString* const kEAGLDrawablePropertyColorFormat = @"EAGLDrawablePropertyColorFormat";
EMU_EXPORT NSString* const kEAGLColorFormatRGBA8 = @"EAGLColorFormatRGBA8";
EMU_EXPORT NSString* const kEAGLColorFormatRGB565 = @"EAGLColorFormat565";
EMU_EXPORT NSString* const kEAGLColorFormatSRGBA8 = @"EAGLColorFormatSRGBA8";
EMU_EXPORT NSString* const kCAMediaTimingFunctionEaseInEaseOut = @"easeInEaseOut";
EMU_EXPORT NSString* const kCAMediaTimingFunctionLinear = @"linear";
EMU_EXPORT NSString* const kCAMediaTimingFunctionEaseIn = @"easeIn";
EMU_EXPORT NSString* const kCAMediaTimingFunctionEaseOut = @"easeOut";
EMU_EXPORT NSString* const kCAMediaTimingFunctionDefault = @"default";
EMU_EXPORT NSString* const kCATransactionDisableActions = @"disableActions";
EMU_EXPORT NSString* const kCATransactionAnimationDuration = @"animationDuration";
EMU_EXPORT NSString* const kCAFillModeForwards = @"forwards";
EMU_EXPORT NSString* const kCAGravityResizeAspect = @"resizeAspect";
EMU_EXPORT NSString* const kCATransitionFade = @"fade";

// ===================================================================== CALayer
@interface CALayer () {
@public
    CGRect _lbounds;
    CGPoint _position, _anchor;
    CGFloat _cscale;
    id _ldelegate;
    NSMutableArray* _sublayers;
    CALayer* _superlayer;
    BOOL _lhidden, _lopaque;
    id _contents;
}
@end
@implementation CALayer
+ (instancetype)layer { return [[[self alloc] init] autorelease]; }
- (instancetype)init {
    _cscale = 1;
    _anchor = CGPointMake(0.5, 0.5);
    return self;
}
- (void)dealloc { [_sublayers release]; [_contents release]; [super dealloc]; }
- (CGRect)bounds { return _lbounds; }
- (void)setBounds:(CGRect)b { _lbounds = b; }
- (CGRect)frame {
    return CGRectMake(_position.x - _lbounds.size.width * _anchor.x, _position.y - _lbounds.size.height * _anchor.y, _lbounds.size.width,
                      _lbounds.size.height);
}
- (void)setFrame:(CGRect)f {
    _lbounds.size = f.size;
    _position = CGPointMake(f.origin.x + f.size.width * _anchor.x, f.origin.y + f.size.height * _anchor.y);
}
- (CGPoint)position { return _position; }
- (void)setPosition:(CGPoint)p { _position = p; }
- (CGPoint)anchorPoint { return _anchor; }
- (void)setAnchorPoint:(CGPoint)a { _anchor = a; }
- (CGFloat)zPosition { return 0; }
- (void)setZPosition:(CGFloat)z { (void)z; }
- (CGFloat)contentsScale { return _cscale; }
- (void)setContentsScale:(CGFloat)s { _cscale = s; }
- (CGFloat)rasterizationScale { return _cscale; }
- (void)setRasterizationScale:(CGFloat)s { (void)s; }
- (void)setShouldRasterize:(BOOL)b { (void)b; }
- (id)delegate { return _ldelegate; }
- (void)setDelegate:(id)d { _ldelegate = d; }
- (BOOL)isOpaque { return _lopaque; }
- (void)setOpaque:(BOOL)o { _lopaque = o; }
- (BOOL)isHidden { return _lhidden; }
- (void)setHidden:(BOOL)h { _lhidden = h; }
- (NSArray*)sublayers { return _sublayers; }
- (void)setSublayers:(NSArray*)s { [_sublayers release]; _sublayers = [s mutableCopy]; }
- (CALayer*)superlayer { return _superlayer; }
- (void)addSublayer:(CALayer*)l {
    if (!_sublayers) _sublayers = [[NSMutableArray alloc] init];
    [l removeFromSuperlayer];
    [_sublayers addObject:l];
    l->_superlayer = self;
}
- (void)insertSublayer:(CALayer*)l atIndex:(unsigned)i {
    [self addSublayer:l];
    [_sublayers removeObjectIdenticalTo:l];
    [_sublayers insertObject:l atIndex:i > [_sublayers count] ? [_sublayers count] : i];
}
- (void)insertSublayer:(CALayer*)l below:(CALayer*)s { (void)s; [self insertSublayer:l atIndex:0]; }
- (void)insertSublayer:(CALayer*)l above:(CALayer*)s { (void)s; [self addSublayer:l]; }
- (void)removeFromSuperlayer {
    if (!_superlayer) return;
    CALayer* s = _superlayer;
    _superlayer = nil;
    [s->_sublayers removeObjectIdenticalTo:self];
}
- (id)contents { return _contents; }
- (void)setContents:(id)c { [_contents release]; _contents = [c retain]; }
- (void)setContentsGravity:(NSString*)g { (void)g; }
- (void)setNeedsDisplay {}
- (void)setNeedsLayout {}
- (void)layoutIfNeeded {}
- (void)setNeedsDisplayOnBoundsChange:(BOOL)b { (void)b; }
- (void)displayIfNeeded {}
- (void)renderInContext:(void*)ctx { (void)ctx; }
- (void)setMasksToBounds:(BOOL)b { (void)b; }
- (BOOL)masksToBounds { return NO; }
- (void)setCornerRadius:(CGFloat)r { (void)r; }
- (CGFloat)cornerRadius { return 0; }
- (void)setBorderWidth:(CGFloat)w { (void)w; }
- (void)setBorderColor:(void*)c { (void)c; }
- (void)setBackgroundColor:(void*)c { (void)c; }
- (void)setShadowColor:(void*)c { (void)c; }
- (void)setShadowOpacity:(float)o { (void)o; }
- (void)setShadowRadius:(CGFloat)r { (void)r; }
- (void)setShadowOffset:(CGSize)s { (void)s; }
- (void)setShadowPath:(void*)p { (void)p; }
- (void)setOpacity:(float)o { (void)o; }
- (float)opacity { return 1.0f; }
- (void)setMask:(CALayer*)m { (void)m; }
- (void)addAnimation:(id)a forKey:(NSString*)k { (void)a; (void)k; }
- (void)removeAnimationForKey:(NSString*)k { (void)k; }
- (void)removeAllAnimations {}
- (id)animationForKey:(NSString*)k { (void)k; return nil; }
- (void)setAffineTransform:(CGAffineTransform)t { (void)t; }
- (CGAffineTransform)affineTransform { CGAffineTransform t = {1, 0, 0, 1, 0, 0}; return t; }
- (void)setActions:(NSDictionary*)a { (void)a; }
- (id)presentationLayer { return self; }
- (id)modelLayer { return self; }
@end

@interface CAEAGLLayer : CALayer {
    NSDictionary* _props;
}
@end
@implementation CAEAGLLayer
- (void)dealloc { [_props release]; [super dealloc]; }
- (NSDictionary*)drawableProperties { return _props; }
- (void)setDrawableProperties:(NSDictionary*)p { [_props release]; _props = [p copy]; }
- (void)setPresentsWithTransaction:(BOOL)b { (void)b; }
@end
@interface CAGradientLayer : CALayer
@end
@implementation CAGradientLayer
- (void)setColors:(NSArray*)c { (void)c; }
- (void)setLocations:(NSArray*)l { (void)l; }
- (void)setStartPoint:(CGPoint)p { (void)p; }
- (void)setEndPoint:(CGPoint)p { (void)p; }
@end
@interface CAShapeLayer : CALayer
@end
@implementation CAShapeLayer
- (void)setPath:(void*)p { (void)p; }
- (void)setFillColor:(void*)c { (void)c; }
- (void)setStrokeColor:(void*)c { (void)c; }
- (void)setLineWidth:(CGFloat)w { (void)w; }
@end

@interface CAMediaTimingFunction : NSObject
@end
@implementation CAMediaTimingFunction
+ (instancetype)functionWithName:(NSString*)n { (void)n; return [[[self alloc] init] autorelease]; }
+ (instancetype)functionWithControlPoints:(float)a :(float)b :(float)c :(float)d { (void)a; (void)b; (void)c; (void)d; return [[[self alloc] init] autorelease]; }
@end
@interface CATransaction : NSObject
@end
@implementation CATransaction
+ (void)begin {}
+ (void)commit {}
+ (void)flush {}
+ (void)setDisableActions:(BOOL)b { (void)b; }
+ (void)setAnimationDuration:(NSTimeInterval)d { (void)d; }
+ (void)setCompletionBlock:(void (^)(void))b { if (b) b(); }
+ (void)setValue:(id)v forKey:(NSString*)k { (void)v; (void)k; }
+ (void)setAnimationTimingFunction:(id)f { (void)f; }
@end
@interface CAAnimation : NSObject
@end
@implementation CAAnimation
+ (instancetype)animation { return [[[self alloc] init] autorelease]; }
+ (instancetype)animationWithKeyPath:(NSString*)k { (void)k; return [[[self alloc] init] autorelease]; }
- (void)setDuration:(NSTimeInterval)d { (void)d; }
- (void)setDelegate:(id)d { (void)d; }
- (void)setTimingFunction:(id)f { (void)f; }
- (void)setRemovedOnCompletion:(BOOL)b { (void)b; }
- (void)setFillMode:(NSString*)m { (void)m; }
- (void)setRepeatCount:(float)c { (void)c; }
- (void)setFromValue:(id)v { (void)v; }
- (void)setToValue:(id)v { (void)v; }
- (void)setType:(NSString*)t { (void)t; }
- (void)setSubtype:(NSString*)t { (void)t; }
@end
@interface CABasicAnimation : CAAnimation
@end
@implementation CABasicAnimation
@end
@interface CATransition : CAAnimation
@end
@implementation CATransition
@end

EMU_EXPORT double CACurrentMediaTime(void) { return _emu_now(); }

// ===================================================================== CADisplayLink
@interface CADisplayLink : NSObject {
@public
    id _target;
    SEL _sel;
    NSInteger _interval;
    BOOL _paused, _added;
    double _next, _stamp, _last;
}
@end
@implementation CADisplayLink
+ (CADisplayLink*)displayLinkWithTarget:(id)t selector:(SEL)s {
    CADisplayLink* l = [[[self alloc] init] autorelease];
    l->_target = [t retain];
    l->_sel = s;
    l->_interval = 1;
    return l;
}
- (void)dealloc { [_target release]; [super dealloc]; }
- (void)addToRunLoop:(id)rl forMode:(NSString*)m {
    (void)rl; (void)m;
    if (_added) return;
    _added = YES;
    _next = _emu_now();
    _uikit_register_display_link(self);
}
- (void)removeFromRunLoop:(id)rl forMode:(NSString*)m { (void)rl; (void)m; [self invalidate]; }
- (void)invalidate {
    if (!_added) return;
    _added = NO;
    _uikit_unregister_display_link(self);
}
- (BOOL)isPaused { return _paused; }
- (void)setPaused:(BOOL)p { _paused = p; }
- (NSInteger)frameInterval { return _interval; }
- (void)setFrameInterval:(NSInteger)i { _interval = i < 1 ? 1 : i; }
- (NSInteger)preferredFramesPerSecond { return 60 / _interval; }
- (void)setPreferredFramesPerSecond:(NSInteger)f { _interval = f <= 0 ? 1 : (60 / f < 1 ? 1 : 60 / f); }
- (double)timestamp { return _stamp; }
- (double)targetTimestamp { return _stamp + [self duration]; }
- (double)duration { return 1.0 / 60.0; }
- (id)target { return _target; }
- (double)_emuNext { return _next; }
- (void)_setEmuNext:(double)t { _next = t; }
- (void)_fire:(double)now {
    _last = _stamp;
    _stamp = now;
    objc_msgSend(_target, _sel, self);
}
@end

// ===================================================================== EAGLContext
@interface EAGLSharegroup : NSObject
@end
@implementation EAGLSharegroup
- (void)setDebugLabel:(NSString*)l { (void)l; }
@end
@interface EAGLContext : NSObject {
    NSUInteger _api;
    EAGLSharegroup* _group;
    BOOL _mt;
}
@end
static EAGLContext* g_current_ctx;
@implementation EAGLContext
+ (BOOL)setCurrentContext:(EAGLContext*)c {
    if (c == g_current_ctx) return YES;
    [c retain];
    [g_current_ctx release];
    g_current_ctx = c;
    return YES;
}
+ (EAGLContext*)currentContext { return g_current_ctx; }
- (instancetype)initWithAPI:(NSUInteger)api { return [self initWithAPI:api sharegroup:nil]; }
- (instancetype)initWithAPI:(NSUInteger)api sharegroup:(EAGLSharegroup*)g {
    if (api != 2) {  // only OpenGL ES 2.0 is offered
        [self release];
        return nil;
    }
    if (!pthread_main_np()) NSLog(@"[EAGL] context created off the main thread");
    int w, h;
    _uikit_pixel_size(&w, &h);
    if (!emu_window_create(w, h)) {
        [self release];
        return nil;
    }
    _api = api;
    _group = g ? [g retain] : [[EAGLSharegroup alloc] init];
    return self;
}
- (void)dealloc { [_group release]; [super dealloc]; }
- (NSUInteger)API { return _api; }
- (EAGLSharegroup*)sharegroup { return _group; }
- (BOOL)isMultiThreaded { return _mt; }
- (void)setMultiThreaded:(BOOL)m { _mt = m; }
- (void)setDebugLabel:(NSString*)l { (void)l; }
- (BOOL)renderbufferStorage:(NSUInteger)target fromDrawable:(id)drawable {
    (void)target;
    CGRect b = [(CALayer*)drawable bounds];
    CGFloat s = [(CALayer*)drawable contentsScale];
    int w = (int)(b.size.width * s + 0.5), h = (int)(b.size.height * s + 0.5);
    if (w <= 0 || h <= 0) _uikit_pixel_size(&w, &h);
    return emu_gl_storage_from_drawable(w, h) != 0;
}
- (BOOL)presentRenderbuffer:(NSUInteger)target { (void)target; return emu_gl_present() != 0; }
- (BOOL)presentRenderbuffer:(NSUInteger)target atTime:(double)t { (void)t; return [self presentRenderbuffer:target]; }
@end
