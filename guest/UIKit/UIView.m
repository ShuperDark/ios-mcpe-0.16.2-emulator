// UIResponder, UIView, UIWindow, UIViewController, UIScreen, UIDevice, UITouch, UIEvent.
#import <emu/UIKit.h>
#include <emu/host.h>

#include <stdio.h>
#include <string.h>

#include <emu/msg.h>
extern Class object_getClass(id);
extern const char* class_getName(Class);

static CGFloat g_scale = 2.0;
static int g_px_w = 1280, g_px_h = 720;
EMU_EXPORT CGFloat _uikit_scale(void) { return g_scale; }
EMU_EXPORT void _uikit_pixel_size(int* w, int* h) { *w = g_px_w; *h = g_px_h; }
EMU_EXPORT CGSize _uikit_screen_points(void) { return CGSizeMake(g_px_w / g_scale, g_px_h / g_scale); }
EMU_EXPORT void _uikit_set_pixel_size(int w, int h) { g_px_w = w; g_px_h = h; }

static id g_first_responder;
EMU_EXPORT void _uikit_set_first_responder(id r) { g_first_responder = r; }
EMU_EXPORT id _uikit_first_responder(void) { return g_first_responder; }

static NSMutableArray* g_layout_queue;

// ===================================================================== UIResponder
@implementation UIResponder
- (UIResponder*)nextResponder { return nil; }
- (BOOL)canBecomeFirstResponder { return NO; }
- (BOOL)becomeFirstResponder {
    if (g_first_responder == self) return YES;
    if (g_first_responder && ![g_first_responder resignFirstResponder]) return NO;
    g_first_responder = self;
    return YES;
}
- (BOOL)resignFirstResponder {
    if (g_first_responder == self) g_first_responder = nil;
    return YES;
}
- (BOOL)canResignFirstResponder { return YES; }
- (BOOL)isFirstResponder { return g_first_responder == self; }
- (void)touchesBegan:(NSSet*)t withEvent:(UIEvent*)e { [[self nextResponder] touchesBegan:t withEvent:e]; }
- (void)touchesMoved:(NSSet*)t withEvent:(UIEvent*)e { [[self nextResponder] touchesMoved:t withEvent:e]; }
- (void)touchesEnded:(NSSet*)t withEvent:(UIEvent*)e { [[self nextResponder] touchesEnded:t withEvent:e]; }
- (void)touchesCancelled:(NSSet*)t withEvent:(UIEvent*)e { [[self nextResponder] touchesCancelled:t withEvent:e]; }
- (void)motionBegan:(NSInteger)m withEvent:(UIEvent*)e { (void)m; (void)e; }
- (void)motionEnded:(NSInteger)m withEvent:(UIEvent*)e { (void)m; (void)e; }
- (void)remoteControlReceivedWithEvent:(UIEvent*)e { (void)e; }
- (id)inputView { return nil; }
- (id)inputAccessoryView { return nil; }
- (void)reloadInputViews {}
- (NSArray*)keyCommands { return nil; }
- (id)undoManager { return nil; }
@end

// ===================================================================== UIView
@implementation UIView
+ (Class)layerClass { return [CALayer class]; }
+ (void)animateWithDuration:(NSTimeInterval)d animations:(void (^)(void))a { (void)d; if (a) a(); }
+ (void)animateWithDuration:(NSTimeInterval)d animations:(void (^)(void))a completion:(void (^)(BOOL))c { (void)d; if (a) a(); if (c) c(YES); }
+ (void)animateWithDuration:(NSTimeInterval)d delay:(NSTimeInterval)dl options:(NSUInteger)o animations:(void (^)(void))a completion:(void (^)(BOOL))c {
    (void)d; (void)dl; (void)o;
    if (a) a();
    if (c) c(YES);
}
+ (void)transitionWithView:(UIView*)v duration:(NSTimeInterval)d options:(NSUInteger)o animations:(void (^)(void))a completion:(void (^)(BOOL))c {
    (void)v; (void)d; (void)o;
    if (a) a();
    if (c) c(YES);
}
+ (void)performWithoutAnimation:(void (^)(void))a { if (a) a(); }
+ (void)beginAnimations:(NSString*)n context:(void*)c { (void)n; (void)c; }
+ (void)commitAnimations {}
+ (void)setAnimationDuration:(NSTimeInterval)d { (void)d; }
+ (void)setAnimationDelegate:(id)d { (void)d; }
+ (void)setAnimationDidStopSelector:(SEL)s { (void)s; }
+ (void)setAnimationBeginsFromCurrentState:(BOOL)b { (void)b; }
+ (void)setAnimationCurve:(NSInteger)c { (void)c; }
+ (void)setAnimationsEnabled:(BOOL)b { (void)b; }
+ (BOOL)areAnimationsEnabled { return NO; }
+ (void)setAnimationTimingFunction:(id)f { (void)f; }

- (instancetype)initWithFrame:(CGRect)f {
    _frame = f;
    _bounds = CGRectMake(0, 0, f.size.width, f.size.height);
    _subviews = [[NSMutableArray alloc] init];
    _alpha = 1;
    _userInteraction = YES;
    _scale = 1;
    _layer = [[[[self class] layerClass] alloc] init];
    [_layer setDelegate:self];
    [_layer setFrame:f];
    [self setNeedsLayout];
    return self;
}
- (instancetype)init { return [self initWithFrame:CGRectZeroInline]; }
- (instancetype)initWithCoder:(id)c {
    CGRect f = CGRectZeroInline;
    if ([c respondsToSelector:@selector(_emuFrame)]) {
        NSValue* v = [c performSelector:@selector(_emuFrame)];
        f = [v CGRectValue];
    } else {
        CGSize s = _uikit_screen_points();
        f = CGRectMake(0, 0, s.width, s.height);
    }
    return [self initWithFrame:f];
}
- (void)dealloc {
    for (UIView* v in _subviews) v->_superview = nil;
    [_subviews release];
    [_layer setDelegate:nil];
    [_layer release];
    [_bg release];
    [super dealloc];
}
- (CALayer*)layer { return _layer; }
- (CGRect)frame { return _frame; }
- (void)setFrame:(CGRect)f {
    BOOL resized = f.size.width != _frame.size.width || f.size.height != _frame.size.height;
    _frame = f;
    _bounds.size = f.size;
    [_layer setFrame:f];
    if (resized) {
        [self setNeedsLayout];
        for (UIView* v in _subviews)
            if (v->_autoresize & 0x12) {  // flexible width/height
                CGRect sf = v->_frame;
                if (v->_autoresize & 0x2) sf.size.width = f.size.width - sf.origin.x;
                if (v->_autoresize & 0x10) sf.size.height = f.size.height - sf.origin.y;
                [v setFrame:sf];
            }
    }
}
- (CGRect)bounds { return _bounds; }
- (void)setBounds:(CGRect)b {
    BOOL resized = b.size.width != _bounds.size.width || b.size.height != _bounds.size.height;
    _bounds = b;
    _frame.size = b.size;
    [_layer setBounds:b];
    if (resized) [self setNeedsLayout];
}
- (CGPoint)center { return CGPointMake(_frame.origin.x + _frame.size.width / 2, _frame.origin.y + _frame.size.height / 2); }
- (void)setCenter:(CGPoint)c {
    CGRect f = _frame;
    f.origin = CGPointMake(c.x - f.size.width / 2, c.y - f.size.height / 2);
    [self setFrame:f];
}
- (CGAffineTransform)transform { CGAffineTransform t = {1, 0, 0, 1, 0, 0}; return t; }
- (void)setTransform:(CGAffineTransform)t { (void)t; }
- (UIView*)superview { return _superview; }
- (NSArray*)subviews { return [[_subviews copy] autorelease]; }
- (UIWindow*)window {
    UIView* v = self;
    while (v && ![v isKindOfClass:[UIWindow class]]) v = v->_superview;
    return (UIWindow*)v;
}
- (void)willMoveToSuperview:(UIView*)s { (void)s; }
- (void)didMoveToSuperview {}
- (void)willMoveToWindow:(UIWindow*)w { (void)w; }
- (void)didMoveToWindow {}
- (void)didAddSubview:(UIView*)v { (void)v; }
- (void)willRemoveSubview:(UIView*)v { (void)v; }
- (void)addSubview:(UIView*)v {
    if (!v || v == self) return;
    [v retain];
    [v removeFromSuperview];
    [v willMoveToSuperview:self];
    [_subviews addObject:v];
    v->_superview = self;
    [v release];
    [v didMoveToSuperview];
    [v didMoveToWindow];
    [self didAddSubview:v];
    [v setNeedsLayout];
}
- (void)insertSubview:(UIView*)v atIndex:(NSInteger)i {
    [self addSubview:v];
    [_subviews removeObjectIdenticalTo:v];
    if ((NSUInteger)i > [_subviews count]) i = (NSInteger)[_subviews count];
    [_subviews insertObject:v atIndex:(NSUInteger)i];
}
- (void)insertSubview:(UIView*)v belowSubview:(UIView*)s {
    NSUInteger i = [_subviews indexOfObjectIdenticalTo:s];
    [self insertSubview:v atIndex:i == (NSUInteger)NSNotFound ? 0 : (NSInteger)i];
}
- (void)insertSubview:(UIView*)v aboveSubview:(UIView*)s {
    NSUInteger i = [_subviews indexOfObjectIdenticalTo:s];
    [self insertSubview:v atIndex:i == (NSUInteger)NSNotFound ? (NSInteger)[_subviews count] : (NSInteger)i + 1];
}
- (void)bringSubviewToFront:(UIView*)v {
    if (![_subviews containsObject:v]) return;
    [v retain];
    [_subviews removeObjectIdenticalTo:v];
    [_subviews addObject:v];
    [v release];
}
- (void)sendSubviewToBack:(UIView*)v {
    if (![_subviews containsObject:v]) return;
    [v retain];
    [_subviews removeObjectIdenticalTo:v];
    [_subviews insertObject:v atIndex:0];
    [v release];
}
- (void)removeFromSuperview {
    UIView* s = _superview;
    if (!s) return;
    [s willRemoveSubview:self];
    [self retain];
    _superview = nil;
    [s->_subviews removeObjectIdenticalTo:self];
    if ([self isFirstResponder]) [self resignFirstResponder];
    [self didMoveToSuperview];
    [self autorelease];
}
- (UIView*)viewWithTag:(NSInteger)t {
    if (_tag == t) return self;
    for (UIView* v in _subviews) {
        UIView* r = [v viewWithTag:t];
        if (r) return r;
    }
    return nil;
}
- (BOOL)isDescendantOfView:(UIView*)v {
    for (UIView* x = self; x; x = x->_superview) if (x == v) return YES;
    return NO;
}
- (UIResponder*)nextResponder { return _vc ? (UIResponder*)_vc : (UIResponder*)_superview; }
- (void)setNeedsLayout {
    if (_needsLayout) return;
    _needsLayout = YES;
    if (!g_layout_queue) g_layout_queue = [[NSMutableArray alloc] init];
    [g_layout_queue addObject:self];
}
- (void)layoutIfNeeded {
    if (_needsLayout) {
        _needsLayout = NO;
        [self layoutSubviews];
        if (_vc && [_vc respondsToSelector:@selector(viewDidLayoutSubviews)]) [_vc performSelector:@selector(viewDidLayoutSubviews)];
    }
    for (UIView* v in [[_subviews copy] autorelease]) [v layoutIfNeeded];
}
- (void)layoutSubviews {}
- (void)setNeedsDisplay {}
- (void)setNeedsDisplayInRect:(CGRect)r { (void)r; }
- (void)drawRect:(CGRect)r { (void)r; }
- (void)setNeedsUpdateConstraints {}
- (void)updateConstraintsIfNeeded {}
- (void)updateConstraints {}
- (void)addConstraint:(id)c { (void)c; }
- (void)addConstraints:(NSArray*)c { (void)c; }
- (void)removeConstraint:(id)c { (void)c; }
- (void)removeConstraints:(NSArray*)c { (void)c; }
- (NSArray*)constraints { return [NSArray array]; }
- (void)setTranslatesAutoresizingMaskIntoConstraints:(BOOL)b { (void)b; }
- (CGSize)sizeThatFits:(CGSize)s { return s; }
- (void)sizeToFit {}
- (CGSize)intrinsicContentSize { return CGSizeMake(-1, -1); }
- (void)setAutoresizingMask:(NSUInteger)m { _autoresize = m; }
- (NSUInteger)autoresizingMask { return _autoresize; }
- (void)setAutoresizesSubviews:(BOOL)b { (void)b; }
- (void)setContentMode:(NSInteger)m { (void)m; }
- (NSInteger)contentMode { return 0; }
- (void)setClipsToBounds:(BOOL)b { (void)b; }
- (BOOL)clipsToBounds { return NO; }
- (BOOL)isHidden { return _hidden; }
- (void)setHidden:(BOOL)h { _hidden = h; }
- (CGFloat)alpha { return _alpha; }
- (void)setAlpha:(CGFloat)a { _alpha = a; }
- (BOOL)isOpaque { return _opaque; }
- (void)setOpaque:(BOOL)o { _opaque = o; }
- (NSInteger)tag { return _tag; }
- (void)setTag:(NSInteger)t { _tag = t; }
- (UIColor*)backgroundColor { return _bg; }
- (void)setBackgroundColor:(UIColor*)c { [_bg release]; _bg = [c retain]; }
- (UIColor*)tintColor { return nil; }
- (void)setTintColor:(UIColor*)c { (void)c; }
- (BOOL)isUserInteractionEnabled { return _userInteraction; }
- (void)setUserInteractionEnabled:(BOOL)b { _userInteraction = b; }
- (BOOL)isMultipleTouchEnabled { return _multiTouch; }
- (void)setMultipleTouchEnabled:(BOOL)b { _multiTouch = b; }
- (BOOL)isExclusiveTouch { return NO; }
- (void)setExclusiveTouch:(BOOL)b { (void)b; }
- (CGFloat)contentScaleFactor { return _scale; }
- (void)setContentScaleFactor:(CGFloat)s { _scale = s; [_layer setContentsScale:s]; }
- (void)setAccessibilityLabel:(NSString*)l { (void)l; }
- (void)setIsAccessibilityElement:(BOOL)b { (void)b; }
- (NSArray*)gestureRecognizers { return nil; }
- (void)addGestureRecognizer:(id)g { (void)g; }
- (void)removeGestureRecognizer:(id)g { (void)g; }
- (void)endEditing:(BOOL)force {
    (void)force;
    id fr = _uikit_first_responder();
    if ([fr isKindOfClass:[UIView class]] && [fr isDescendantOfView:self]) [fr resignFirstResponder];
}
- (BOOL)drawViewHierarchyInRect:(CGRect)r afterScreenUpdates:(BOOL)a { (void)r; (void)a; return NO; }
- (id)snapshotViewAfterScreenUpdates:(BOOL)a { (void)a; return nil; }
- (BOOL)pointInside:(CGPoint)p withEvent:(UIEvent*)e {
    (void)e;
    return p.x >= 0 && p.y >= 0 && p.x < _bounds.size.width && p.y < _bounds.size.height;
}
- (UIView*)hitTest:(CGPoint)p withEvent:(UIEvent*)e {
    if (_hidden || !_userInteraction || _alpha < 0.01) return nil;
    if (![self pointInside:p withEvent:e]) return nil;
    for (NSUInteger i = [_subviews count]; i-- > 0;) {
        UIView* v = [_subviews objectAtIndex:i];
        CGPoint q = CGPointMake(p.x - v->_frame.origin.x + v->_bounds.origin.x, p.y - v->_frame.origin.y + v->_bounds.origin.y);
        UIView* r = [v hitTest:q withEvent:e];
        if (r) return r;
    }
    return self;
}
static CGPoint to_window(UIView* v, CGPoint p) {
    for (; v; v = v->_superview) {
        if ([v isKindOfClass:[UIWindow class]]) break;
        p.x += v->_frame.origin.x - v->_bounds.origin.x;
        p.y += v->_frame.origin.y - v->_bounds.origin.y;
    }
    return p;
}
static CGPoint from_window(UIView* v, CGPoint p) {
    CGPoint o = to_window(v, CGPointMake(0, 0));
    return CGPointMake(p.x - o.x, p.y - o.y);
}
- (CGPoint)convertPoint:(CGPoint)p toView:(UIView*)v { return v ? from_window(v, to_window(self, p)) : to_window(self, p); }
- (CGPoint)convertPoint:(CGPoint)p fromView:(UIView*)v { return from_window(self, v ? to_window(v, p) : p); }
- (CGRect)convertRect:(CGRect)r toView:(UIView*)v { r.origin = [self convertPoint:r.origin toView:v]; return r; }
- (CGRect)convertRect:(CGRect)r fromView:(UIView*)v { r.origin = [self convertPoint:r.origin fromView:v]; return r; }
- (NSString*)description {
    return [NSString stringWithFormat:@"<%s: %p; frame = (%g %g; %g %g)>", class_getName(object_getClass(self)), self, _frame.origin.x,
                                      _frame.origin.y, _frame.size.width, _frame.size.height];
}
@end

EMU_EXPORT void _uikit_layout_pass(void) {
    if (![g_layout_queue count]) return;
    NSArray* q = [g_layout_queue copy];
    [g_layout_queue removeAllObjects];
    for (UIView* v in q) [v layoutIfNeeded];
    [q release];
}

// ===================================================================== UIWindow
static UIWindow* g_key_window;
static NSMutableArray* g_windows;
@interface UIWindow () {
    UIViewController* _root;
    BOOL _visible;
}
@end
@implementation UIWindow
+ (UIWindow*)_keyWindow { return g_key_window; }
+ (NSArray*)_windows { return g_windows ? g_windows : [NSArray array]; }
- (instancetype)initWithFrame:(CGRect)f {
    self = [super initWithFrame:f];
    if (!g_windows) g_windows = [[NSMutableArray alloc] init];
    [g_windows addObject:self];
    return self;
}
- (UIScreen*)screen { return [UIScreen mainScreen]; }
- (void)setScreen:(UIScreen*)s { (void)s; }
- (UIViewController*)rootViewController { return _root; }
- (void)_showRoot {
    UIView* v = [_root view];
    [v setFrame:[self bounds]];
    [v setAutoresizingMask:0x12];
    [self addSubview:v];
    [_root viewWillAppear:NO];
    [self layoutIfNeeded];
    [_root viewDidAppear:NO];
}
- (void)setRootViewController:(UIViewController*)vc {
    if (vc == _root) return;
    if (_root) {
        [[_root view] removeFromSuperview];
        [_root release];
    }
    _root = [vc retain];
    if (_visible && _root) [self _showRoot];
}
- (void)makeKeyWindow { g_key_window = self; }
- (BOOL)isKeyWindow { return g_key_window == self; }
- (void)makeKeyAndVisible {
    g_key_window = self;
    if (_visible) return;
    _visible = YES;
    _hidden = NO;
    if (_root && ![[_root view] superview]) [self _showRoot];
}
- (void)setHidden:(BOOL)h {
    [super setHidden:h];
    if (!h && !_visible) [self makeKeyAndVisible];
}
- (void)becomeKeyWindow {}
- (void)resignKeyWindow {}
- (CGFloat)windowLevel { return 0; }
- (void)setWindowLevel:(CGFloat)l { (void)l; }
- (void)sendEvent:(UIEvent*)e { (void)e; }
- (UIResponder*)nextResponder { return (UIResponder*)objc_msgSend((id)NSClassFromString(@"UIApplication"), @selector(sharedApplication)); }
@end

// ===================================================================== UIViewController
@interface _EmuNibCoder : NSObject {
@public
    NSString* _nib;
    CGRect _frame;
}
@end
@implementation _EmuNibCoder
- (void)dealloc { [_nib release]; [super dealloc]; }
- (NSValue*)_emuFrame { return [NSValue valueWithCGRect:_frame]; }
- (void)setNib:(NSString*)n { [_nib release]; _nib = [n copy]; }
- (BOOL)allowsKeyedCoding { return YES; }
- (BOOL)containsValueForKey:(NSString*)k { (void)k; return NO; }
- (id)decodeObjectForKey:(NSString*)k { return [k isEqualToString:@"UINibName"] ? _nib : nil; }
- (BOOL)decodeBoolForKey:(NSString*)k { (void)k; return NO; }
- (NSInteger)decodeIntegerForKey:(NSString*)k { (void)k; return 0; }
- (int)decodeIntForKey:(NSString*)k { (void)k; return 0; }
- (double)decodeDoubleForKey:(NSString*)k { (void)k; return 0; }
- (float)decodeFloatForKey:(NSString*)k { (void)k; return 0; }
- (CGRect)decodeCGRectForKey:(NSString*)k { (void)k; return _frame; }
@end

// Root-view class of the known nibs of the game (no nib parser needed)
static NSString* nib_root_view_class(NSString* nib) {
    if ([nib isEqualToString:@"minecraftpeViewController"]) return @"EAGLView";
    return nil;
}

@implementation UIViewController
- (instancetype)init { return [self initWithNibName:nil bundle:nil]; }
- (instancetype)initWithNibName:(NSString*)n bundle:(id)b {
    (void)b;
    _nibName = [n copy];
    return self;
}
- (instancetype)initWithCoder:(id)c {
    NSString* n = [c respondsToSelector:@selector(decodeObjectForKey:)] ? [c decodeObjectForKey:@"UINibName"] : nil;
    return [self initWithNibName:n bundle:nil];
}
- (void)dealloc {
    [_view release];
    [_nibName release];
    [super dealloc];
}
- (NSString*)nibName { return _nibName; }
- (id)nibBundle { return nil; }
- (void)loadView {
    CGSize s = _uikit_screen_points();
    NSString* cls = _nibName ? nib_root_view_class(_nibName) : nil;
    if (!cls && _nibName) NSLog(@"[UIKit] nib '%@' is not known; using a plain UIView", _nibName);
    Class c = cls ? NSClassFromString(cls) : [UIView class];
    if (!c) c = [UIView class];
    UIView* v;
    if (cls) {
        _EmuNibCoder* coder = [[[_EmuNibCoder alloc] init] autorelease];
        coder->_frame = CGRectMake(0, 0, s.width, s.height);
        v = [[c alloc] initWithCoder:coder];
        [v awakeFromNib];
    } else {
        v = [[c alloc] initWithFrame:CGRectMake(0, 0, s.width, s.height)];
    }
    [self setView:v];
    [v release];
}
- (UIView*)view {
    if (!_view) {
        [self loadView];
        [self viewDidLoad];
    }
    return _view;
}
- (UIView*)viewIfLoaded { return _view; }
- (void)setView:(UIView*)v {
    if (v == _view) return;
    if (_view) _view->_vc = nil;
    [_view release];
    _view = [v retain];
    if (v) v->_vc = self;
}
- (BOOL)isViewLoaded { return _view != nil; }
- (void)viewDidLoad {}
- (void)viewDidUnload {}
- (void)viewWillAppear:(BOOL)a { (void)a; }
- (void)viewDidAppear:(BOOL)a { (void)a; }
- (void)viewWillDisappear:(BOOL)a { (void)a; }
- (void)viewDidDisappear:(BOOL)a { (void)a; }
- (void)viewWillLayoutSubviews {}
- (void)viewDidLayoutSubviews {}
- (void)didReceiveMemoryWarning {}
- (void)updateViewConstraints {}
- (UIResponder*)nextResponder { return _view ? (UIResponder*)_view->_superview : nil; }
- (NSString*)title { return nil; }
- (void)setTitle:(NSString*)t { (void)t; }
- (UIViewController*)parentViewController { return _parent; }
- (UIViewController*)presentedViewController { return _presented; }
- (UIViewController*)presentingViewController { return _presenting; }
- (id)navigationController { return nil; }
- (id)navigationItem { return nil; }
- (id)tabBarController { return nil; }
- (id)splitViewController { return nil; }
- (BOOL)isBeingPresented { return NO; }
- (BOOL)isBeingDismissed { return NO; }
- (BOOL)isMovingToParentViewController { return NO; }
- (NSArray*)childViewControllers { return [NSArray array]; }
- (void)addChildViewController:(UIViewController*)c { c->_parent = self; }
- (void)removeFromParentViewController { _parent = nil; }
- (void)didMoveToParentViewController:(UIViewController*)p { (void)p; }
- (void)willMoveToParentViewController:(UIViewController*)p { (void)p; }
- (void)presentViewController:(UIViewController*)vc animated:(BOOL)a completion:(void (^)(void))c {
    (void)a;
    _presented = [vc retain];
    vc->_presenting = self;
    if ([vc respondsToSelector:@selector(_emuBeginPick)]) {  // UIImagePickerController: the host shows the picker
        [vc performSelector:@selector(_emuBeginPick)];
        if (c) c();
        return;
    }
    NSLog(@"[UIKit] presentViewController:%@ (shown as an overlay)", vc);
    UIView* v = [vc view];
    [v setFrame:[_view bounds]];
    [[_view window] addSubview:v];
    [vc viewWillAppear:NO];
    [vc viewDidAppear:NO];
    if (c) c();
}
- (void)presentModalViewController:(UIViewController*)vc animated:(BOOL)a { [self presentViewController:vc animated:a completion:nil]; }
- (void)dismissViewControllerAnimated:(BOOL)a completion:(void (^)(void))c {
    (void)a;
    UIViewController* p = _presented ? _presented : self;
    UIViewController* owner = _presented ? self : _presenting;
    [p viewWillDisappear:NO];
    [p->_view removeFromSuperview];
    [p viewDidDisappear:NO];
    if (owner) {
        owner->_presented = nil;
        p->_presenting = nil;
        [p release];
    }
    if (c) c();
}
- (void)dismissModalViewControllerAnimated:(BOOL)a { [self dismissViewControllerAnimated:a completion:nil]; }
- (NSUInteger)supportedInterfaceOrientations { return 0x18; }
- (BOOL)shouldAutorotate { return YES; }
- (BOOL)shouldAutorotateToInterfaceOrientation:(NSInteger)o { return o == 3 || o == 4; }
- (NSInteger)interfaceOrientation { return 4; }
- (NSInteger)preferredInterfaceOrientationForPresentation { return 4; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (void)setNeedsStatusBarAppearanceUpdate {}
- (void)setModalTransitionStyle:(NSInteger)s { (void)s; }
- (void)setModalPresentationStyle:(NSInteger)s { (void)s; }
- (NSInteger)modalPresentationStyle { return 0; }
- (void)setPreferredContentSize:(CGSize)s { (void)s; }
- (void)setEdgesForExtendedLayout:(NSUInteger)e { (void)e; }
- (void)setAutomaticallyAdjustsScrollViewInsets:(BOOL)b { (void)b; }
- (id)popoverPresentationController { return nil; }
- (id)storyboard { return nil; }
- (void)setWantsFullScreenLayout:(BOOL)b { (void)b; }
@end

// ===================================================================== UIScreen
@implementation UIScreen
+ (UIScreen*)mainScreen {
    static UIScreen* s;
    if (!s) s = [[UIScreen alloc] init];
    return s;
}
+ (NSArray*)screens { return [NSArray arrayWithObject:[self mainScreen]]; }
- (CGRect)bounds { CGSize s = _uikit_screen_points(); return CGRectMake(0, 0, s.width, s.height); }
- (CGRect)applicationFrame { return [self bounds]; }
- (CGRect)nativeBounds { return CGRectMake(0, 0, g_px_h, g_px_w); }  // portrait, pixels
- (CGFloat)scale { return g_scale; }
- (CGFloat)nativeScale { return g_scale; }
- (CGFloat)brightness { return 0.8; }
- (void)setBrightness:(CGFloat)b { (void)b; }
- (id)currentMode { return nil; }
- (NSArray*)availableModes { return [NSArray array]; }
- (id)coordinateSpace { return self; }
- (id)fixedCoordinateSpace { return self; }
- (NSInteger)maximumFramesPerSecond { return 60; }
- (id)displayLinkWithTarget:(id)t selector:(SEL)s {
    return objc_msgSend((id)NSClassFromString(@"CADisplayLink"), @selector(displayLinkWithTarget:selector:), t, s);
}
- (id)traitCollection { return nil; }
@end

// ===================================================================== UIDevice
@interface UIDevice : NSObject
@end
@implementation UIDevice
+ (UIDevice*)currentDevice {
    static UIDevice* d;
    if (!d) d = [[UIDevice alloc] init];
    return d;
}
- (NSString*)systemVersion { return @"9.3.2"; }
- (NSString*)systemName { return @"iPhone OS"; }
- (NSString*)model { return @"iPhone"; }
- (NSString*)localizedModel { return @"iPhone"; }
- (NSString*)name { return @"iPhone"; }
- (NSInteger)userInterfaceIdiom { return 0; }
- (NSInteger)orientation { return 3; }  // UIDeviceOrientationLandscapeLeft
- (BOOL)isGeneratingDeviceOrientationNotifications { return NO; }
- (void)beginGeneratingDeviceOrientationNotifications {}
- (void)endGeneratingDeviceOrientationNotifications {}
- (BOOL)isMultitaskingSupported { return YES; }
- (id)identifierForVendor {
    // A random UUID kept in the app's preferences (i.e. per data folder): two copies of the
    // emulator are two devices, otherwise a LAN host takes them for the same player.
    static id u;
    if (!u) {
        id defs = [NSClassFromString(@"NSUserDefaults") performSelector:@selector(standardUserDefaults)];
        NSString* s = [defs performSelector:@selector(stringForKey:) withObject:@"EmuIdentifierForVendor"];
        if (!s) {
            id nu = [NSClassFromString(@"NSUUID") performSelector:@selector(UUID)];
            s = [nu performSelector:@selector(UUIDString)];
            ((void (*)(id, SEL, id, id))objc_msgSend)(defs, @selector(setObject:forKey:), s, @"EmuIdentifierForVendor");
            [defs performSelector:@selector(synchronize)];
        }
        u = [[NSClassFromString(@"NSUUID") alloc] performSelector:@selector(initWithUUIDString:) withObject:s];
    }
    return u;
}
- (float)batteryLevel { return 1.0f; }
- (NSInteger)batteryState { return 3; }
- (BOOL)isBatteryMonitoringEnabled { return NO; }
- (void)setBatteryMonitoringEnabled:(BOOL)b { (void)b; }
- (BOOL)isProximityMonitoringEnabled { return NO; }
- (void)setProximityMonitoringEnabled:(BOOL)b { (void)b; }
- (void)playInputClick {}
@end

// ===================================================================== UITouch / UIEvent
@implementation UITouch
- (CGPoint)locationInView:(UIView*)v { return v ? [v convertPoint:_loc fromView:nil] : _loc; }
- (CGPoint)previousLocationInView:(UIView*)v { return v ? [v convertPoint:_prev fromView:nil] : _prev; }
- (CGPoint)preciseLocationInView:(UIView*)v { return [self locationInView:v]; }
- (NSInteger)phase { return _phase; }
- (NSTimeInterval)timestamp { return _time; }
- (UIView*)view { return _view; }
- (UIWindow*)window { return _window; }
- (NSUInteger)tapCount { return _taps; }
- (CGFloat)force { return 1.0; }
- (CGFloat)maximumPossibleForce { return 1.0; }
- (CGFloat)majorRadius { return 10.0; }
- (CGFloat)majorRadiusTolerance { return 2.0; }
- (NSInteger)type { return 0; }
- (NSArray*)gestureRecognizers { return nil; }
@end
@implementation UIEvent
- (void)dealloc { [_touches release]; [super dealloc]; }
- (NSSet*)allTouches { return _touches; }
- (NSSet*)touchesForView:(UIView*)v {
    NSMutableSet* r = [NSMutableSet set];
    for (UITouch* t in _touches) if (t->_view == v) [r addObject:t];
    return r;
}
- (NSSet*)touchesForWindow:(UIWindow*)w { (void)w; return _touches; }
- (NSTimeInterval)timestamp { return _time; }
- (NSInteger)type { return 0; }
- (NSInteger)subtype { return 0; }
@end
