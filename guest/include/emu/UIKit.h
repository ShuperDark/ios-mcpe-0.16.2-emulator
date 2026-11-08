// Minimal UIKit / QuartzCore / OpenGLES interfaces of the guest frameworks.
#pragma once
#import <emu/Foundation.h>

@class UIView, UIWindow, UIViewController, UIScreen, UITouch, UIEvent, UIColor, UIFont, UIImage, CALayer;

static const CGRect CGRectZeroInline = {{0, 0}, {0, 0}};

@interface UIResponder : NSObject
- (UIResponder*)nextResponder;
- (BOOL)canBecomeFirstResponder;
- (BOOL)becomeFirstResponder;
- (BOOL)resignFirstResponder;
- (BOOL)isFirstResponder;
- (void)touchesBegan:(NSSet*)t withEvent:(UIEvent*)e;
- (void)touchesMoved:(NSSet*)t withEvent:(UIEvent*)e;
- (void)touchesEnded:(NSSet*)t withEvent:(UIEvent*)e;
- (void)touchesCancelled:(NSSet*)t withEvent:(UIEvent*)e;
@end

@interface CALayer : NSObject
+ (instancetype)layer;
- (CGRect)bounds;
- (void)setBounds:(CGRect)b;
- (CGRect)frame;
- (void)setFrame:(CGRect)f;
- (CGFloat)contentsScale;
- (void)setContentsScale:(CGFloat)s;
- (id)delegate;
- (void)setDelegate:(id)d;
@end

@interface UIView : UIResponder {
@public
    CGRect _frame;
    CGRect _bounds;
    UIView* _superview;
    NSMutableArray* _subviews;
    CALayer* _layer;
    UIViewController* _vc;
    BOOL _hidden, _needsLayout, _userInteraction, _multiTouch, _opaque;
    CGFloat _alpha, _scale;
    NSInteger _tag;
    UIColor* _bg;
    NSUInteger _autoresize;
}
+ (Class)layerClass;
- (instancetype)initWithFrame:(CGRect)f;
- (instancetype)initWithCoder:(id)c;
- (CGRect)frame;
- (void)setFrame:(CGRect)f;
- (CGRect)bounds;
- (void)setBounds:(CGRect)b;
- (CALayer*)layer;
- (UIView*)superview;
- (NSArray*)subviews;
- (UIWindow*)window;
- (void)addSubview:(UIView*)v;
- (void)removeFromSuperview;
- (void)setNeedsLayout;
- (void)layoutIfNeeded;
- (void)layoutSubviews;
- (UIView*)hitTest:(CGPoint)p withEvent:(UIEvent*)e;
- (CGPoint)convertPoint:(CGPoint)p fromView:(UIView*)v;
- (CGPoint)convertPoint:(CGPoint)p toView:(UIView*)v;
- (CGFloat)contentScaleFactor;
- (void)setContentScaleFactor:(CGFloat)s;
@end

@interface UIWindow : UIView
- (void)makeKeyAndVisible;
- (UIViewController*)rootViewController;
- (void)setRootViewController:(UIViewController*)vc;
@end

@interface UIViewController : UIResponder {
@public
    UIView* _view;
    NSString* _nibName;
    UIViewController* _presented;
    UIViewController* _presenting;
    UIViewController* _parent;
}
- (instancetype)initWithNibName:(NSString*)n bundle:(id)b;
- (UIView*)view;
- (void)setView:(UIView*)v;
- (void)loadView;
- (void)viewDidLoad;
- (BOOL)isViewLoaded;
- (void)viewWillAppear:(BOOL)a;
- (void)viewDidAppear:(BOOL)a;
- (void)viewWillDisappear:(BOOL)a;
- (void)viewDidDisappear:(BOOL)a;
@end

@interface UIScreen : NSObject
+ (UIScreen*)mainScreen;
- (CGRect)bounds;
- (CGFloat)scale;
@end

@interface UITouch : NSObject {
@public
    CGPoint _loc, _prev;  // window coordinates (points)
    NSInteger _phase;
    NSTimeInterval _time;
    UIView* _view;
    UIWindow* _window;
    NSUInteger _taps;
}
- (CGPoint)locationInView:(UIView*)v;
- (CGPoint)previousLocationInView:(UIView*)v;
@end

@interface UIEvent : NSObject {
@public
    NSSet* _touches;
    NSTimeInterval _time;
}
- (NSSet*)allTouches;
@end

// internal helpers
#ifdef __cplusplus
extern "C" {
#endif
EMU_EXPORT CGFloat _uikit_scale(void);
EMU_EXPORT CGSize _uikit_screen_points(void);
EMU_EXPORT void _uikit_pixel_size(int* w, int* h);
EMU_EXPORT void _uikit_layout_pass(void);
EMU_EXPORT void _uikit_register_display_link(id link);
EMU_EXPORT void _uikit_unregister_display_link(id link);
EMU_EXPORT void _uikit_set_first_responder(id r);
EMU_EXPORT id _uikit_first_responder(void);
#ifdef __cplusplus
}
#endif
