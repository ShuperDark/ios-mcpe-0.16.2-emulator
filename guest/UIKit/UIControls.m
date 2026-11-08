// UIKit leaf classes: colors, fonts, images, controls, text input, alerts and
// assorted view controllers (most of them are inert placeholders).
#import <emu/UIKit.h>
#include <emu/host.h>

#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>

#include <emu/msg.h>
extern id CGColorCreate(id space, const CGFloat* comps);
extern id _cg_image_create_rgba(const void* rgba, int w, int h, BOOL premultiplied);
extern const uint8_t* _cg_image_pixels(id img);
extern size_t CGImageGetWidth(id i);
extern size_t CGImageGetHeight(id i);
extern id CGBitmapContextCreate(void* data, size_t w, size_t h, size_t bpc, size_t bpr, id space, uint32_t info);
extern id CGBitmapContextCreateImage(id c);
extern NSString* const UITextViewTextDidChangeNotification;
extern NSString* const UITextFieldTextDidChangeNotification;
extern NSString* const UIKeyboardDidShowNotification;
extern NSString* const UIKeyboardWillShowNotification;
extern NSString* const UIKeyboardDidHideNotification;
extern NSString* const UIKeyboardWillHideNotification;
extern NSString* const UIKeyboardFrameBeginUserInfoKey;
extern NSString* const UIKeyboardFrameEndUserInfoKey;

// ===================================================================== UIColor
@interface UIColor : NSObject <NSCopying> {
@public
    CGFloat _r, _g, _b, _a;
    id _cg;
}
@end
@implementation UIColor
+ (UIColor*)colorWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a {
    UIColor* c = [[[self alloc] init] autorelease];
    c->_r = r; c->_g = g; c->_b = b; c->_a = a;
    return c;
}
+ (UIColor*)colorWithWhite:(CGFloat)w alpha:(CGFloat)a { return [self colorWithRed:w green:w blue:w alpha:a]; }
+ (UIColor*)colorWithHue:(CGFloat)h saturation:(CGFloat)s brightness:(CGFloat)v alpha:(CGFloat)a { (void)h; (void)s; return [self colorWithRed:v green:v blue:v alpha:a]; }
+ (UIColor*)colorWithCGColor:(id)c { (void)c; return [self blackColor]; }
+ (UIColor*)colorWithPatternImage:(id)img { (void)img; return [self grayColor]; }
- (instancetype)initWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a { _r = r; _g = g; _b = b; _a = a; return self; }
- (instancetype)initWithWhite:(CGFloat)w alpha:(CGFloat)a { return [self initWithRed:w green:w blue:w alpha:a]; }
#define COLOR(name, r, g, b, a) + (UIColor*)name { return [self colorWithRed:r green:g blue:b alpha:a]; }
COLOR(blackColor, 0, 0, 0, 1)
COLOR(whiteColor, 1, 1, 1, 1)
COLOR(clearColor, 0, 0, 0, 0)
COLOR(grayColor, 0.5, 0.5, 0.5, 1)
COLOR(lightGrayColor, 2.0 / 3, 2.0 / 3, 2.0 / 3, 1)
COLOR(darkGrayColor, 1.0 / 3, 1.0 / 3, 1.0 / 3, 1)
COLOR(redColor, 1, 0, 0, 1)
COLOR(greenColor, 0, 1, 0, 1)
COLOR(blueColor, 0, 0, 1, 1)
COLOR(yellowColor, 1, 1, 0, 1)
COLOR(orangeColor, 1, 0.5, 0, 1)
COLOR(groupTableViewBackgroundColor, 0.94, 0.94, 0.96, 1)
- (void)dealloc { [_cg release]; [super dealloc]; }
- (id)copyWithZone:(NSZone*)z { (void)z; return [self retain]; }
- (id)CGColor {
    if (!_cg) {
        CGFloat c[4] = {_r, _g, _b, _a};
        _cg = CGColorCreate(nil, c);
    }
    return _cg;
}
- (UIColor*)colorWithAlphaComponent:(CGFloat)a { return [UIColor colorWithRed:_r green:_g blue:_b alpha:a]; }
- (BOOL)getRed:(CGFloat*)r green:(CGFloat*)g blue:(CGFloat*)b alpha:(CGFloat*)a {
    if (r) *r = _r; if (g) *g = _g; if (b) *b = _b; if (a) *a = _a;
    return YES;
}
- (BOOL)getWhite:(CGFloat*)w alpha:(CGFloat*)a { if (w) *w = _r; if (a) *a = _a; return YES; }
- (void)set {}
- (void)setFill {}
- (void)setStroke {}
@end

// ===================================================================== UIFont
@interface UIFont : NSObject {
    CGFloat _size;
    NSString* _name;
}
@end
@implementation UIFont
+ (UIFont*)fontWithName:(NSString*)n size:(CGFloat)s {
    UIFont* f = [[[self alloc] init] autorelease];
    f->_size = s;
    f->_name = [n copy];
    return f;
}
+ (UIFont*)systemFontOfSize:(CGFloat)s { return [self fontWithName:@"Helvetica" size:s]; }
+ (UIFont*)boldSystemFontOfSize:(CGFloat)s { return [self fontWithName:@"Helvetica-Bold" size:s]; }
+ (UIFont*)italicSystemFontOfSize:(CGFloat)s { return [self fontWithName:@"Helvetica-Oblique" size:s]; }
+ (UIFont*)systemFontOfSize:(CGFloat)s weight:(CGFloat)w { (void)w; return [self systemFontOfSize:s]; }
+ (UIFont*)preferredFontForTextStyle:(NSString*)s { (void)s; return [self systemFontOfSize:17]; }
+ (CGFloat)systemFontSize { return 14; }
+ (CGFloat)labelFontSize { return 17; }
+ (CGFloat)buttonFontSize { return 18; }
+ (NSArray*)familyNames { return [NSArray arrayWithObject:@"Helvetica"]; }
+ (NSArray*)fontNamesForFamilyName:(NSString*)n { return [NSArray arrayWithObject:n]; }
- (void)dealloc { [_name release]; [super dealloc]; }
- (CGFloat)pointSize { return _size; }
- (NSString*)fontName { return _name; }
- (NSString*)familyName { return _name; }
- (CGFloat)lineHeight { return _size * 1.2; }
- (CGFloat)ascender { return _size * 0.95; }
- (CGFloat)descender { return -_size * 0.25; }
- (CGFloat)capHeight { return _size * 0.7; }
- (CGFloat)xHeight { return _size * 0.5; }
- (CGFloat)leading { return 0; }
- (UIFont*)fontWithSize:(CGFloat)s { return [UIFont fontWithName:_name size:s]; }
@end

@implementation NSString (EmuUIKitDrawing)
- (CGSize)sizeWithFont:(UIFont*)f { CGFloat s = f ? [f pointSize] : 14; return CGSizeMake([self length] * s * 0.55, s * 1.2); }
- (CGSize)sizeWithFont:(UIFont*)f constrainedToSize:(CGSize)c lineBreakMode:(NSInteger)m {
    (void)m;
    CGSize s = [self sizeWithFont:f];
    if (s.width > c.width && c.width > 0) {
        CGFloat lines = s.width / c.width + 1;
        s.width = c.width;
        s.height *= (lines < 1 ? 1 : (NSInteger)lines);
    }
    return s;
}
- (CGSize)sizeWithAttributes:(NSDictionary*)a { return [self sizeWithFont:[a objectForKey:@"NSFont"]]; }
- (CGRect)boundingRectWithSize:(CGSize)c options:(NSUInteger)o attributes:(NSDictionary*)a context:(id)ctx {
    (void)o; (void)ctx;
    CGSize s = [self sizeWithFont:[a objectForKey:@"NSFont"] constrainedToSize:c lineBreakMode:0];
    return CGRectMake(0, 0, s.width, s.height);
}
- (CGSize)drawAtPoint:(CGPoint)p withFont:(UIFont*)f { (void)p; return [self sizeWithFont:f]; }
- (void)drawAtPoint:(CGPoint)p withAttributes:(NSDictionary*)a { (void)p; (void)a; }
- (void)drawInRect:(CGRect)r withAttributes:(NSDictionary*)a { (void)r; (void)a; }
- (CGSize)drawInRect:(CGRect)r withFont:(UIFont*)f { (void)f; return r.size; }
@end

// ===================================================================== UIImage
@interface UIImage : NSObject {
@public
    id _cg;
    CGFloat _scale;
    NSInteger _orient;
}
@end
@implementation UIImage
+ (UIImage*)imageWithCGImage:(id)cg { return [self imageWithCGImage:cg scale:1 orientation:0]; }
+ (UIImage*)imageWithCGImage:(id)cg scale:(CGFloat)s orientation:(NSInteger)o {
    if (!cg) return nil;
    UIImage* i = [[[self alloc] init] autorelease];
    i->_cg = [cg retain];
    i->_scale = s > 0 ? s : 1;
    i->_orient = o;
    return i;
}
+ (UIImage*)imageWithData:(NSData*)d { return [[[self alloc] initWithData:d] autorelease]; }
+ (UIImage*)imageWithData:(NSData*)d scale:(CGFloat)s {
    UIImage* i = [self imageWithData:d];
    if (i) i->_scale = s;
    return i;
}
+ (UIImage*)imageWithContentsOfFile:(NSString*)p { return [[[self alloc] initWithContentsOfFile:p] autorelease]; }
+ (UIImage*)imageNamed:(NSString*)n {
    NSBundle* b = [NSBundle mainBundle];
    NSString* base = [n stringByDeletingPathExtension];
    NSString* ext = [[n pathExtension] length] ? [n pathExtension] : @"png";
    NSString* cands[] = {[base stringByAppendingString:@"@2x"], base};
    for (int i = 0; i < 2; i++) {
        NSString* p = [b pathForResource:cands[i] ofType:ext];
        if (p) {
            UIImage* img = [self imageWithContentsOfFile:p];
            if (img && i == 0) img->_scale = 2;
            return img;
        }
    }
    return nil;
}
- (instancetype)initWithData:(NSData*)d {
    int w = 0, h = 0;
    void* px = d ? emu_image_decode([d bytes], [d length], &w, &h) : NULL;
    if (!px) { [self release]; return nil; }
    _cg = _cg_image_create_rgba(px, w, h, NO);
    free(px);
    _scale = 1;
    return self;
}
- (instancetype)initWithContentsOfFile:(NSString*)p { return [self initWithData:[NSData dataWithContentsOfFile:p]]; }
- (instancetype)initWithCGImage:(id)cg { _cg = [cg retain]; _scale = 1; return self; }
- (void)dealloc { [_cg release]; [super dealloc]; }
- (id)CGImage { return _cg; }
- (CGSize)size { return CGSizeMake(CGImageGetWidth(_cg) / _scale, CGImageGetHeight(_cg) / _scale); }
- (CGFloat)scale { return _scale; }
- (NSInteger)imageOrientation { return _orient; }
- (UIImage*)resizableImageWithCapInsets:(UIEdgeInsets)i { (void)i; return self; }
- (UIImage*)stretchableImageWithLeftCapWidth:(NSInteger)l topCapHeight:(NSInteger)t { (void)l; (void)t; return self; }
- (UIImage*)imageWithRenderingMode:(NSInteger)m { (void)m; return self; }
- (void)drawInRect:(CGRect)r { (void)r; }
- (void)drawInRect:(CGRect)r blendMode:(int)m alpha:(CGFloat)a { (void)r; (void)m; (void)a; }
- (void)drawAtPoint:(CGPoint)p { (void)p; }
@end

EMU_EXPORT NSData* UIImagePNGRepresentation(UIImage* img) {
    id cg = [img CGImage];
    if (!cg) return nil;
    size_t len = 0;
    void* png = emu_image_encode_png(_cg_image_pixels(cg), (int)CGImageGetWidth(cg), (int)CGImageGetHeight(cg), &len);
    if (!png) return nil;
    return [NSData dataWithBytesNoCopy:png length:len freeWhenDone:YES];
}
EMU_EXPORT NSData* UIImageJPEGRepresentation(UIImage* img, CGFloat q) { (void)q; return UIImagePNGRepresentation(img); }

// ---- UIGraphics image contexts ----
static NSMutableArray* g_ctx_stack;
EMU_EXPORT void UIGraphicsBeginImageContextWithOptions(CGSize s, BOOL opaque, CGFloat scale) {
    (void)opaque;
    if (scale <= 0) scale = _uikit_scale();
    if (!g_ctx_stack) g_ctx_stack = [[NSMutableArray alloc] init];
    size_t w = (size_t)(s.width * scale), h = (size_t)(s.height * scale);
    id c = CGBitmapContextCreate(NULL, w ? w : 1, h ? h : 1, 8, 0, nil, 1);
    [g_ctx_stack addObject:c];
    [c release];
}
EMU_EXPORT void UIGraphicsBeginImageContext(CGSize s) { UIGraphicsBeginImageContextWithOptions(s, NO, 1); }
EMU_EXPORT id UIGraphicsGetCurrentContext(void) { return [g_ctx_stack lastObject]; }
EMU_EXPORT UIImage* UIGraphicsGetImageFromCurrentImageContext(void) {
    id c = [g_ctx_stack lastObject];
    if (!c) return nil;
    id cg = CGBitmapContextCreateImage(c);
    UIImage* i = [UIImage imageWithCGImage:cg];
    [cg release];
    return i;
}
EMU_EXPORT void UIGraphicsEndImageContext(void) { [g_ctx_stack removeLastObject]; }
EMU_EXPORT void UIGraphicsPushContext(id c) { if (!g_ctx_stack) g_ctx_stack = [[NSMutableArray alloc] init]; [g_ctx_stack addObject:c]; }
EMU_EXPORT void UIGraphicsPopContext(void) { [g_ctx_stack removeLastObject]; }
EMU_EXPORT void UIRectFill(CGRect r) { (void)r; }

// ===================================================================== controls
@interface UIControl : UIView {
@public
    NSMutableArray* _actions;
    BOOL _enabled, _selected, _highlighted;
}
@end
@implementation UIControl
- (instancetype)initWithFrame:(CGRect)f { self = [super initWithFrame:f]; _enabled = YES; return self; }
- (void)dealloc { [_actions release]; [super dealloc]; }
- (void)addTarget:(id)t action:(SEL)a forControlEvents:(NSUInteger)e {
    if (!_actions) _actions = [[NSMutableArray alloc] init];
    [_actions addObject:[NSArray arrayWithObjects:t ? t : (id)[NSNull null], NSStringFromSelector(a), [NSNumber numberWithUnsignedInteger:e], nil]];
}
- (void)removeTarget:(id)t action:(SEL)a forControlEvents:(NSUInteger)e { (void)t; (void)a; (void)e; [_actions removeAllObjects]; }
- (void)sendActionsForControlEvents:(NSUInteger)e {
    for (NSArray* a in [[_actions copy] autorelease]) {
        if (!([[a objectAtIndex:2] unsignedIntegerValue] & e)) continue;
        id t = [a objectAtIndex:0];
        if (t == (id)[NSNull null]) continue;
        objc_msgSend(t, NSSelectorFromString([a objectAtIndex:1]), self);
    }
}
- (BOOL)isEnabled { return _enabled; }
- (void)setEnabled:(BOOL)e { _enabled = e; }
- (BOOL)isSelected { return _selected; }
- (void)setSelected:(BOOL)s { _selected = s; }
- (BOOL)isHighlighted { return _highlighted; }
- (void)setHighlighted:(BOOL)h { _highlighted = h; }
- (void)setContentHorizontalAlignment:(NSInteger)a { (void)a; }
- (void)setContentVerticalAlignment:(NSInteger)a { (void)a; }
- (void)touchesEnded:(NSSet*)t withEvent:(UIEvent*)e { (void)t; (void)e; [self sendActionsForControlEvents:1 << 6]; }  // TouchUpInside
- (void)touchesBegan:(NSSet*)t withEvent:(UIEvent*)e { (void)t; (void)e; [self sendActionsForControlEvents:1]; }
- (void)touchesMoved:(NSSet*)t withEvent:(UIEvent*)e { (void)t; (void)e; }
@end

@interface UILabel : UIView {
    NSString* _text;
    UIFont* _font;
    UIColor* _color;
    NSInteger _lines;
}
@end
@implementation UILabel
- (void)dealloc { [_text release]; [_font release]; [_color release]; [super dealloc]; }
- (NSString*)text { return _text; }
- (void)setText:(NSString*)t { [_text release]; _text = [t copy]; }
- (void)setAttributedText:(id)t { [self setText:[t description]]; }
- (UIFont*)font { return _font ? _font : [UIFont systemFontOfSize:17]; }
- (void)setFont:(UIFont*)f { [_font release]; _font = [f retain]; }
- (UIColor*)textColor { return _color; }
- (void)setTextColor:(UIColor*)c { [_color release]; _color = [c retain]; }
- (void)setTextAlignment:(NSInteger)a { (void)a; }
- (void)setNumberOfLines:(NSInteger)n { _lines = n; }
- (NSInteger)numberOfLines { return _lines; }
- (void)setLineBreakMode:(NSInteger)m { (void)m; }
- (void)setAdjustsFontSizeToFitWidth:(BOOL)b { (void)b; }
- (void)setMinimumScaleFactor:(CGFloat)f { (void)f; }
- (void)setMinimumFontSize:(CGFloat)f { (void)f; }
- (void)setShadowColor:(UIColor*)c { (void)c; }
- (void)setShadowOffset:(CGSize)s { (void)s; }
- (void)setHighlightedTextColor:(UIColor*)c { (void)c; }
- (void)setPreferredMaxLayoutWidth:(CGFloat)w { (void)w; }
- (void)sizeToFit { CGSize s = [_text sizeWithFont:[self font]]; CGRect f = _frame; f.size = s; [self setFrame:f]; }
@end

@interface UIImageView : UIView {
    UIImage* _image;
}
@end
@implementation UIImageView
- (instancetype)initWithImage:(UIImage*)i {
    CGSize s = i ? [i size] : CGSizeMake(0, 0);
    self = [super initWithFrame:CGRectMake(0, 0, s.width, s.height)];
    _image = [i retain];
    return self;
}
- (void)dealloc { [_image release]; [super dealloc]; }
- (UIImage*)image { return _image; }
- (void)setImage:(UIImage*)i { [_image release]; _image = [i retain]; }
- (void)setAnimationImages:(NSArray*)a { (void)a; }
- (void)setAnimationDuration:(NSTimeInterval)d { (void)d; }
- (void)startAnimating {}
- (void)stopAnimating {}
- (BOOL)isAnimating { return NO; }
@end

@interface UIButton : UIControl {
    UILabel* _title;
    UIImageView* _imageView;
}
@end
@implementation UIButton
+ (instancetype)buttonWithType:(NSInteger)t { (void)t; return [[[self alloc] initWithFrame:CGRectZeroInline] autorelease]; }
- (void)dealloc { [_title release]; [_imageView release]; [super dealloc]; }
- (UILabel*)titleLabel {
    if (!_title) _title = [[UILabel alloc] initWithFrame:[self bounds]];
    return _title;
}
- (UIImageView*)imageView {
    if (!_imageView) _imageView = [[UIImageView alloc] initWithFrame:[self bounds]];
    return _imageView;
}
- (void)setTitle:(NSString*)t forState:(NSUInteger)s { (void)s; [[self titleLabel] setText:t]; }
- (NSString*)titleForState:(NSUInteger)s { (void)s; return [[self titleLabel] text]; }
- (NSString*)currentTitle { return [[self titleLabel] text]; }
- (void)setTitleColor:(UIColor*)c forState:(NSUInteger)s { (void)s; [[self titleLabel] setTextColor:c]; }
- (void)setTitleShadowColor:(UIColor*)c forState:(NSUInteger)s { (void)c; (void)s; }
- (void)setImage:(UIImage*)i forState:(NSUInteger)s { (void)s; [[self imageView] setImage:i]; }
- (void)setBackgroundImage:(UIImage*)i forState:(NSUInteger)s { (void)i; (void)s; }
- (void)setAttributedTitle:(id)t forState:(NSUInteger)s { (void)s; [[self titleLabel] setText:[t description]]; }
- (UIEdgeInsets)titleEdgeInsets { UIEdgeInsets e = {0, 0, 0, 0}; return e; }
- (void)setTitleEdgeInsets:(UIEdgeInsets)e { (void)e; }
- (void)setImageEdgeInsets:(UIEdgeInsets)e { (void)e; }
- (void)setContentEdgeInsets:(UIEdgeInsets)e { (void)e; }
- (void)setAdjustsImageWhenHighlighted:(BOOL)b { (void)b; }
- (void)setShowsTouchWhenHighlighted:(BOOL)b { (void)b; }
@end

@interface UIActivityIndicatorView : UIView
@end
@implementation UIActivityIndicatorView
- (instancetype)initWithActivityIndicatorStyle:(NSInteger)s { (void)s; return [self initWithFrame:CGRectMake(0, 0, 20, 20)]; }
- (void)startAnimating {}
- (void)stopAnimating {}
- (BOOL)isAnimating { return NO; }
- (void)setHidesWhenStopped:(BOOL)b { (void)b; }
- (void)setActivityIndicatorViewStyle:(NSInteger)s { (void)s; }
- (void)setColor:(UIColor*)c { (void)c; }
@end
@interface UIProgressView : UIView
@end
@implementation UIProgressView
- (instancetype)initWithProgressViewStyle:(NSInteger)s { (void)s; return [self initWithFrame:CGRectMake(0, 0, 100, 4)]; }
- (void)setProgress:(float)p { (void)p; }
- (void)setProgress:(float)p animated:(BOOL)a { (void)p; (void)a; }
@end

@interface UIScrollView : UIView
@end
@implementation UIScrollView
- (void)setContentSize:(CGSize)s { (void)s; }
- (CGSize)contentSize { return _bounds.size; }
- (void)setContentOffset:(CGPoint)o { (void)o; }
- (void)setContentOffset:(CGPoint)o animated:(BOOL)a { (void)o; (void)a; }
- (CGPoint)contentOffset { return CGPointMake(0, 0); }
- (void)setContentInset:(UIEdgeInsets)i { (void)i; }
- (UIEdgeInsets)contentInset { UIEdgeInsets e = {0, 0, 0, 0}; return e; }
- (void)setScrollIndicatorInsets:(UIEdgeInsets)i { (void)i; }
- (void)setDelegate:(id)d { (void)d; }
- (void)setBounces:(BOOL)b { (void)b; }
- (void)setScrollEnabled:(BOOL)b { (void)b; }
- (void)setPagingEnabled:(BOOL)b { (void)b; }
- (void)setShowsVerticalScrollIndicator:(BOOL)b { (void)b; }
- (void)setShowsHorizontalScrollIndicator:(BOOL)b { (void)b; }
- (void)setAlwaysBounceVertical:(BOOL)b { (void)b; }
- (void)setKeyboardDismissMode:(NSInteger)m { (void)m; }
- (void)scrollRectToVisible:(CGRect)r animated:(BOOL)a { (void)r; (void)a; }
- (void)flashScrollIndicators {}
@end

// ===================================================================== text input
@interface UITextView : UIScrollView {
@public
    NSMutableString* _text;
    NSRange _sel;
    id _tdelegate;
    UIFont* _font;
    BOOL _editable;
}
@end
@interface UITextField : UIControl {
@public
    NSMutableString* _text;
    NSRange _sel;
    id _tdelegate;
    NSString* _placeholder;
    UIFont* _font;
}
@end

static void keyboard_note(NSString* name, BOOL shown) {
    CGSize s = _uikit_screen_points();
    CGFloat kh = shown ? s.height * 0.45 : 0;
    CGRect r = CGRectMake(0, s.height - kh, s.width, kh);
    NSDictionary* info = [NSDictionary dictionaryWithObjectsAndKeys:[NSValue valueWithCGRect:r], UIKeyboardFrameBeginUserInfoKey,
                                                                    [NSValue valueWithCGRect:r], UIKeyboardFrameEndUserInfoKey, nil];
    [[NSNotificationCenter defaultCenter] postNotificationName:name object:nil userInfo:info];
}

@implementation UITextView
- (instancetype)initWithFrame:(CGRect)f {
    self = [super initWithFrame:f];
    _text = [[NSMutableString alloc] init];
    _editable = YES;
    return self;
}
- (instancetype)initWithFrame:(CGRect)f textContainer:(id)c { (void)c; return [self initWithFrame:f]; }
- (void)dealloc { [_text release]; [_font release]; [super dealloc]; }
- (NSString*)text { return [[_text copy] autorelease]; }
- (void)setText:(NSString*)t {
    [_text setString:t ? t : @""];
    _sel = NSMakeRange([_text length], 0);
}
- (void)setAttributedText:(id)t { [self setText:[t description]]; }
- (id)attributedText { return [self text]; }
- (id)delegate { return _tdelegate; }
- (void)setDelegate:(id)d { _tdelegate = d; }
- (NSRange)selectedRange { return _sel; }
- (void)setSelectedRange:(NSRange)r {
    if (NSMaxRange(r) > [_text length]) r = NSMakeRange([_text length], 0);
    _sel = r;
}
- (BOOL)hasText { return [_text length] > 0; }
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)becomeFirstResponder {
    if ([self isFirstResponder]) return YES;
    if ([_tdelegate respondsToSelector:@selector(textViewShouldBeginEditing:)] && !((BOOL(*)(id, SEL, id))objc_msgSend)(_tdelegate, @selector(textViewShouldBeginEditing:), self)) return NO;
    [super becomeFirstResponder];
    emu_set_text_input(1);
    keyboard_note(UIKeyboardWillShowNotification, YES);
    keyboard_note(UIKeyboardDidShowNotification, YES);
    if ([_tdelegate respondsToSelector:@selector(textViewDidBeginEditing:)]) objc_msgSend(_tdelegate, @selector(textViewDidBeginEditing:), self);
    emu_set_title("Minecraft PE 0.16.2 (iOS) - typing...");
    return YES;
}
- (BOOL)resignFirstResponder {
    if (![self isFirstResponder]) return YES;
    [super resignFirstResponder];
    emu_set_text_input(0);
    keyboard_note(UIKeyboardWillHideNotification, NO);
    keyboard_note(UIKeyboardDidHideNotification, NO);
    if ([_tdelegate respondsToSelector:@selector(textViewDidEndEditing:)]) objc_msgSend(_tdelegate, @selector(textViewDidEndEditing:), self);
    emu_set_title("Minecraft PE 0.16.2 (iOS)");
    return YES;
}
- (void)_replace:(NSRange)r with:(NSString*)s {
    if ([_tdelegate respondsToSelector:@selector(textView:shouldChangeTextInRange:replacementText:)]) {
        BOOL ok = ((BOOL(*)(id, SEL, id, NSRange, id))objc_msgSend)(_tdelegate, @selector(textView:shouldChangeTextInRange:replacementText:), self, r, s);
        if (!ok) return;
    }
    if (NSMaxRange(r) > [_text length]) return;
    [_text replaceCharactersInRange:r withString:s];
    _sel = NSMakeRange(r.location + [s length], 0);
    [[NSNotificationCenter defaultCenter] postNotificationName:UITextViewTextDidChangeNotification object:self];
    if ([_tdelegate respondsToSelector:@selector(textViewDidChange:)]) objc_msgSend(_tdelegate, @selector(textViewDidChange:), self);
    if ([_tdelegate respondsToSelector:@selector(textViewDidChangeSelection:)]) objc_msgSend(_tdelegate, @selector(textViewDidChangeSelection:), self);
}
- (void)insertText:(NSString*)s { [self _replace:_sel with:s]; }
- (void)deleteBackward {
    NSRange r = _sel;
    if (!r.length) {
        if (!r.location) return;
        r = [_text rangeOfComposedCharacterSequenceAtIndex:r.location - 1];
    }
    [self _replace:r with:@""];
}
- (void)_moveCaret:(NSInteger)d {
    NSInteger p = (NSInteger)_sel.location + d;
    if (p < 0) p = 0;
    if (p > (NSInteger)[_text length]) p = (NSInteger)[_text length];
    _sel = NSMakeRange((NSUInteger)p, 0);
    if ([_tdelegate respondsToSelector:@selector(textViewDidChangeSelection:)]) objc_msgSend(_tdelegate, @selector(textViewDidChangeSelection:), self);
}
- (UIFont*)font { return _font; }
- (void)setFont:(UIFont*)f { [_font release]; _font = [f retain]; }
- (void)setTextColor:(UIColor*)c { (void)c; }
- (void)setTextAlignment:(NSInteger)a { (void)a; }
- (void)setEditable:(BOOL)e { _editable = e; }
- (BOOL)isEditable { return _editable; }
- (void)setSelectable:(BOOL)e { (void)e; }
- (void)setKeyboardType:(NSInteger)t { (void)t; }
- (void)setReturnKeyType:(NSInteger)t { (void)t; }
- (void)setKeyboardAppearance:(NSInteger)t { (void)t; }
- (void)setAutocorrectionType:(NSInteger)t { (void)t; }
- (void)setAutocapitalizationType:(NSInteger)t { (void)t; }
- (void)setSpellCheckingType:(NSInteger)t { (void)t; }
- (void)setSecureTextEntry:(BOOL)b { (void)b; }
- (void)setEnablesReturnKeyAutomatically:(BOOL)b { (void)b; }
- (void)setDataDetectorTypes:(NSUInteger)t { (void)t; }
- (void)setTextContainerInset:(UIEdgeInsets)i { (void)i; }
- (void)setInputAccessoryView:(id)v { (void)v; }
- (void)setInputView:(id)v { (void)v; }
- (id)selectedTextRange { return nil; }
- (void)setSelectedTextRange:(id)r { (void)r; }
- (id)beginningOfDocument { return nil; }
- (id)endOfDocument { return nil; }
- (void)scrollRangeToVisible:(NSRange)r { (void)r; }
- (id)textStorage { return nil; }
- (id)layoutManager { return nil; }
- (id)textContainer { return nil; }
@end

@implementation UITextField
- (instancetype)initWithFrame:(CGRect)f {
    self = [super initWithFrame:f];
    _text = [[NSMutableString alloc] init];
    return self;
}
- (void)dealloc { [_text release]; [_placeholder release]; [_font release]; [super dealloc]; }
- (NSString*)text { return [[_text copy] autorelease]; }
- (void)setText:(NSString*)t { [_text setString:t ? t : @""]; _sel = NSMakeRange([_text length], 0); }
- (void)setAttributedText:(id)t { [self setText:[t description]]; }
- (id)delegate { return _tdelegate; }
- (void)setDelegate:(id)d { _tdelegate = d; }
- (NSString*)placeholder { return _placeholder; }
- (void)setPlaceholder:(NSString*)p { [_placeholder release]; _placeholder = [p copy]; }
- (void)setAttributedPlaceholder:(id)p { [self setPlaceholder:[p description]]; }
- (BOOL)hasText { return [_text length] > 0; }
- (BOOL)canBecomeFirstResponder { return YES; }
- (BOOL)becomeFirstResponder {
    if ([self isFirstResponder]) return YES;
    if ([_tdelegate respondsToSelector:@selector(textFieldShouldBeginEditing:)] && !((BOOL(*)(id, SEL, id))objc_msgSend)(_tdelegate, @selector(textFieldShouldBeginEditing:), self)) return NO;
    [super becomeFirstResponder];
    emu_set_text_input(1);
    keyboard_note(UIKeyboardWillShowNotification, YES);
    keyboard_note(UIKeyboardDidShowNotification, YES);
    if ([_tdelegate respondsToSelector:@selector(textFieldDidBeginEditing:)]) objc_msgSend(_tdelegate, @selector(textFieldDidBeginEditing:), self);
    return YES;
}
- (BOOL)resignFirstResponder {
    if (![self isFirstResponder]) return YES;
    [super resignFirstResponder];
    emu_set_text_input(0);
    keyboard_note(UIKeyboardWillHideNotification, NO);
    keyboard_note(UIKeyboardDidHideNotification, NO);
    if ([_tdelegate respondsToSelector:@selector(textFieldDidEndEditing:)]) objc_msgSend(_tdelegate, @selector(textFieldDidEndEditing:), self);
    return YES;
}
- (void)_replace:(NSRange)r with:(NSString*)s {
    if ([s isEqualToString:@"\n"]) {
        if ([_tdelegate respondsToSelector:@selector(textFieldShouldReturn:)]) objc_msgSend(_tdelegate, @selector(textFieldShouldReturn:), self);
        [self sendActionsForControlEvents:1 << 19];  // EditingDidEndOnExit
        return;
    }
    if ([_tdelegate respondsToSelector:@selector(textField:shouldChangeCharactersInRange:replacementString:)]) {
        BOOL ok = ((BOOL(*)(id, SEL, id, NSRange, id))objc_msgSend)(_tdelegate, @selector(textField:shouldChangeCharactersInRange:replacementString:), self, r, s);
        if (!ok) return;
    }
    if (NSMaxRange(r) > [_text length]) return;
    [_text replaceCharactersInRange:r withString:s];
    _sel = NSMakeRange(r.location + [s length], 0);
    [self sendActionsForControlEvents:1 << 17];  // EditingChanged
    [[NSNotificationCenter defaultCenter] postNotificationName:UITextFieldTextDidChangeNotification object:self];
}
- (void)insertText:(NSString*)s { [self _replace:_sel with:s]; }
- (void)deleteBackward {
    NSRange r = _sel;
    if (!r.length) {
        if (!r.location) return;
        r = NSMakeRange(r.location - 1, 1);
    }
    [self _replace:r with:@""];
}
- (void)_moveCaret:(NSInteger)d {
    NSInteger p = (NSInteger)_sel.location + d;
    if (p < 0) p = 0;
    if (p > (NSInteger)[_text length]) p = (NSInteger)[_text length];
    _sel = NSMakeRange((NSUInteger)p, 0);
}
- (UIFont*)font { return _font; }
- (void)setFont:(UIFont*)f { [_font release]; _font = [f retain]; }
- (void)setTextColor:(UIColor*)c { (void)c; }
- (void)setTextAlignment:(NSInteger)a { (void)a; }
- (void)setBorderStyle:(NSInteger)s { (void)s; }
- (void)setClearButtonMode:(NSInteger)m { (void)m; }
- (void)setKeyboardType:(NSInteger)t { (void)t; }
- (void)setReturnKeyType:(NSInteger)t { (void)t; }
- (void)setKeyboardAppearance:(NSInteger)t { (void)t; }
- (void)setAutocorrectionType:(NSInteger)t { (void)t; }
- (void)setAutocapitalizationType:(NSInteger)t { (void)t; }
- (void)setSpellCheckingType:(NSInteger)t { (void)t; }
- (void)setSecureTextEntry:(BOOL)b { (void)b; }
- (void)setLeftView:(UIView*)v { (void)v; }
- (void)setLeftViewMode:(NSInteger)m { (void)m; }
- (void)setRightView:(UIView*)v { (void)v; }
- (void)setRightViewMode:(NSInteger)m { (void)m; }
- (void)setAdjustsFontSizeToFitWidth:(BOOL)b { (void)b; }
- (void)setMinimumFontSize:(CGFloat)f { (void)f; }
- (void)setEnablesReturnKeyAutomatically:(BOOL)b { (void)b; }
- (void)setInputAccessoryView:(id)v { (void)v; }
- (void)setClearsOnBeginEditing:(BOOL)b { (void)b; }
- (id)selectedTextRange { return nil; }
@end

// keyboard input from the host goes to the first responder
EMU_EXPORT void _uikit_text_input_char(unsigned ch) {
    id fr = _uikit_first_responder();
    if (![fr isKindOfClass:[UITextView class]] && ![fr isKindOfClass:[UITextField class]]) return;
    if (ch == 8) { [fr deleteBackward]; return; }
    if (ch == 27) { [fr resignFirstResponder]; return; }
    if (ch == '\r') ch = '\n';
    if (ch < 32 && ch != '\n' && ch != '\t') return;
    unichar buf[2];
    NSUInteger n = 0;
    if (ch >= 0x10000) {
        ch -= 0x10000;
        buf[n++] = (unichar)(0xd800 + (ch >> 10));
        buf[n++] = (unichar)(0xdc00 + (ch & 0x3ff));
    } else buf[n++] = (unichar)ch;
    [fr insertText:[NSString stringWithCharacters:buf length:n]];
}
EMU_EXPORT void _uikit_text_input_key(int vk) {
    id fr = _uikit_first_responder();
    if (![fr respondsToSelector:@selector(_moveCaret:)]) return;
    if (vk == 0x25) ((void (*)(id, SEL, NSInteger))objc_msgSend)(fr, @selector(_moveCaret:), -1);       // VK_LEFT
    else if (vk == 0x27) ((void (*)(id, SEL, NSInteger))objc_msgSend)(fr, @selector(_moveCaret:), 1);   // VK_RIGHT
    else if (vk == 0x24) ((void (*)(id, SEL, NSInteger))objc_msgSend)(fr, @selector(_moveCaret:), -100000);  // HOME
    else if (vk == 0x23) ((void (*)(id, SEL, NSInteger))objc_msgSend)(fr, @selector(_moveCaret:), 100000);   // END
}

// ===================================================================== alerts
@interface UIAlertView : UIView {
    NSString* _title;
    NSString* _message;
    id _adelegate;
    NSMutableArray* _buttons;
    NSInteger _cancel;
}
@end
@implementation UIAlertView
- (instancetype)initWithTitle:(NSString*)t message:(NSString*)m delegate:(id)d cancelButtonTitle:(NSString*)c otherButtonTitles:(NSString*)o, ... {
    self = [super initWithFrame:CGRectZeroInline];
    _title = [t copy];
    _message = [m copy];
    _adelegate = d;
    _buttons = [[NSMutableArray alloc] init];
    _cancel = -1;
    if (c) { _cancel = 0; [_buttons addObject:c]; }
    va_list ap;
    va_start(ap, o);
    for (NSString* b = o; b; b = va_arg(ap, NSString*)) [_buttons addObject:b];
    va_end(ap);
    return self;
}
- (void)dealloc { [_title release]; [_message release]; [_buttons release]; [super dealloc]; }
- (NSInteger)addButtonWithTitle:(NSString*)t { [_buttons addObject:t]; return (NSInteger)[_buttons count] - 1; }
- (NSInteger)cancelButtonIndex { return _cancel; }
- (void)setCancelButtonIndex:(NSInteger)i { _cancel = i; }
- (NSInteger)firstOtherButtonIndex { return _cancel == 0 ? 1 : 0; }
- (NSInteger)numberOfButtons { return (NSInteger)[_buttons count]; }
- (NSString*)buttonTitleAtIndex:(NSInteger)i { return i >= 0 && i < (NSInteger)[_buttons count] ? [_buttons objectAtIndex:(NSUInteger)i] : nil; }
- (void)setAlertViewStyle:(NSInteger)s { (void)s; }
- (id)textFieldAtIndex:(NSInteger)i { (void)i; return nil; }
- (void)setDelegate:(id)d { _adelegate = d; }
- (void)show {
    NSLog(@"[UIKit] alert: %@ - %@", _title, _message);
    [self retain];
    dispatch_block_t b = ^{
        NSString* text = [NSString stringWithFormat:@"%@\n\n(%@)", self->_message ? self->_message : @"", [self->_buttons componentsJoinedByString:@" / "]];
        int r = emu_message_box([self->_title ? self->_title : @"" UTF8String], [text UTF8String], [self->_buttons count] > 1 ? 2 : 1);
        NSInteger idx = [self->_buttons count] > 1 ? (r == 0 ? [self firstOtherButtonIndex] : self->_cancel) : 0;
        if ([self->_adelegate respondsToSelector:@selector(alertView:clickedButtonAtIndex:)])
            ((void (*)(id, SEL, id, NSInteger))objc_msgSend)(self->_adelegate, @selector(alertView:clickedButtonAtIndex:), self, idx);
        if ([self->_adelegate respondsToSelector:@selector(alertView:didDismissWithButtonIndex:)])
            ((void (*)(id, SEL, id, NSInteger))objc_msgSend)(self->_adelegate, @selector(alertView:didDismissWithButtonIndex:), self, idx);
        [self release];
    };
    dispatch_async(dispatch_get_main_queue(), b);
}
- (void)dismissWithClickedButtonIndex:(NSInteger)i animated:(BOOL)a { (void)i; (void)a; }
- (BOOL)isVisible { return NO; }
@end

@interface UIAlertAction : NSObject {
@public
    NSString* _title;
    NSInteger _style;
    void (^_handler)(id);
}
@end
@implementation UIAlertAction
+ (instancetype)actionWithTitle:(NSString*)t style:(NSInteger)s handler:(void (^)(id))h {
    UIAlertAction* a = [[[self alloc] init] autorelease];
    a->_title = [t copy];
    a->_style = s;
    a->_handler = [h copy];
    return a;
}
- (void)dealloc { [_title release]; [_handler release]; [super dealloc]; }
- (NSString*)title { return _title; }
- (NSInteger)style { return _style; }
- (void)setEnabled:(BOOL)e { (void)e; }
@end
@interface UIAlertController : UIViewController {
    NSString* _atitle;
    NSString* _message;
    NSMutableArray* _acts;
}
@end
@implementation UIAlertController
+ (instancetype)alertControllerWithTitle:(NSString*)t message:(NSString*)m preferredStyle:(NSInteger)s {
    (void)s;
    UIAlertController* a = [[[self alloc] initWithNibName:nil bundle:nil] autorelease];
    a->_atitle = [t copy];
    a->_message = [m copy];
    a->_acts = [[NSMutableArray alloc] init];
    return a;
}
- (void)dealloc { [_atitle release]; [_message release]; [_acts release]; [super dealloc]; }
- (void)addAction:(UIAlertAction*)a { [_acts addObject:a]; }
- (NSArray*)actions { return _acts; }
- (void)addTextFieldWithConfigurationHandler:(void (^)(id))h { (void)h; }
- (NSArray*)textFields { return [NSArray array]; }
- (void)viewDidAppear:(BOOL)an {
    (void)an;
    int r = emu_message_box([_atitle ? _atitle : @"" UTF8String], [_message ? _message : @"" UTF8String], [_acts count] > 1 ? 2 : 1);
    UIAlertAction* chosen = nil;
    for (UIAlertAction* a in _acts) {
        BOOL cancel = a->_style == 1;
        if ((r == 0 && !cancel) || (r != 0 && cancel)) { chosen = a; break; }
    }
    if (!chosen && [_acts count]) chosen = [_acts objectAtIndex:0];
    [self retain];
    [self dismissViewControllerAnimated:NO completion:nil];
    if (chosen && chosen->_handler) chosen->_handler(chosen);
    [self release];
}
@end

// ===================================================================== tables, navigation, misc controllers (inert)
@interface UITableView : UIScrollView
@end
@implementation UITableView
- (instancetype)initWithFrame:(CGRect)f style:(NSInteger)s { (void)s; return [self initWithFrame:f]; }
- (void)reloadData {}
- (void)setDataSource:(id)d { (void)d; }
- (void)setRowHeight:(CGFloat)h { (void)h; }
- (void)setSeparatorStyle:(NSInteger)s { (void)s; }
- (void)setSeparatorColor:(UIColor*)c { (void)c; }
- (void)setTableHeaderView:(UIView*)v { (void)v; }
- (void)setTableFooterView:(UIView*)v { (void)v; }
- (UIView*)tableHeaderView { return nil; }
- (UIView*)tableFooterView { return nil; }
- (id)dequeueReusableCellWithIdentifier:(NSString*)i { (void)i; return nil; }
- (void)registerClass:(Class)c forCellReuseIdentifier:(NSString*)i { (void)c; (void)i; }
- (void)deselectRowAtIndexPath:(id)p animated:(BOOL)a { (void)p; (void)a; }
- (void)setBackgroundView:(UIView*)v { (void)v; }
- (void)setAllowsSelection:(BOOL)b { (void)b; }
@end
@interface UITableViewCell : UIView {
    UILabel* _label;
    UILabel* _detail;
}
@end
@implementation UITableViewCell
- (instancetype)initWithStyle:(NSInteger)s reuseIdentifier:(NSString*)i { (void)s; (void)i; return [self initWithFrame:CGRectMake(0, 0, 320, 44)]; }
- (void)dealloc { [_label release]; [_detail release]; [super dealloc]; }
- (UILabel*)textLabel { if (!_label) _label = [[UILabel alloc] initWithFrame:[self bounds]]; return _label; }
- (UILabel*)detailTextLabel { if (!_detail) _detail = [[UILabel alloc] initWithFrame:[self bounds]]; return _detail; }
- (UIImageView*)imageView { return nil; }
- (UIView*)contentView { return self; }
- (void)setAccessoryType:(NSInteger)t { (void)t; }
- (void)setSelectionStyle:(NSInteger)s { (void)s; }
- (void)setAccessoryView:(UIView*)v { (void)v; }
@end
@interface UITableViewController : UIViewController
@end
@implementation UITableViewController
- (instancetype)initWithStyle:(NSInteger)s { (void)s; return [self initWithNibName:nil bundle:nil]; }
- (UITableView*)tableView { return (UITableView*)[self view]; }
- (void)loadView {
    CGSize s = _uikit_screen_points();
    UITableView* t = [[UITableView alloc] initWithFrame:CGRectMake(0, 0, s.width, s.height)];
    [self setView:t];
    [t release];
}
@end
@interface UINavigationBar : UIView
@end
@implementation UINavigationBar
- (void)setBarStyle:(NSInteger)s { (void)s; }
- (void)setTintColor:(UIColor*)c { (void)c; }
- (void)setBarTintColor:(UIColor*)c { (void)c; }
- (void)setTranslucent:(BOOL)b { (void)b; }
- (void)setTitleTextAttributes:(NSDictionary*)d { (void)d; }
@end
@interface UINavigationItem : NSObject
@end
@implementation UINavigationItem
- (void)setTitle:(NSString*)t { (void)t; }
- (void)setLeftBarButtonItem:(id)i { (void)i; }
- (void)setRightBarButtonItem:(id)i { (void)i; }
- (void)setLeftBarButtonItems:(NSArray*)i { (void)i; }
- (void)setRightBarButtonItems:(NSArray*)i { (void)i; }
- (id)rightBarButtonItem { return nil; }
- (id)leftBarButtonItem { return nil; }
- (void)setHidesBackButton:(BOOL)b { (void)b; }
- (void)setTitleView:(UIView*)v { (void)v; }
@end
@interface UINavigationController : UIViewController {
    NSMutableArray* _stack;
}
@end
@implementation UINavigationController
- (instancetype)initWithRootViewController:(UIViewController*)vc {
    self = [self initWithNibName:nil bundle:nil];
    _stack = [[NSMutableArray alloc] initWithObjects:vc, nil];
    return self;
}
- (void)dealloc { [_stack release]; [super dealloc]; }
- (NSArray*)viewControllers { return _stack; }
- (UIViewController*)topViewController { return [_stack lastObject]; }
- (UIViewController*)visibleViewController { return [_stack lastObject]; }
- (void)pushViewController:(UIViewController*)vc animated:(BOOL)a { (void)a; [_stack addObject:vc]; }
- (UIViewController*)popViewControllerAnimated:(BOOL)a { (void)a; id v = [[[_stack lastObject] retain] autorelease]; if ([_stack count] > 1) [_stack removeLastObject]; return v; }
- (NSArray*)popToRootViewControllerAnimated:(BOOL)a { (void)a; return [NSArray array]; }
- (UINavigationBar*)navigationBar { return [[[UINavigationBar alloc] initWithFrame:CGRectZeroInline] autorelease]; }
- (void)setNavigationBarHidden:(BOOL)h { (void)h; }
- (void)setNavigationBarHidden:(BOOL)h animated:(BOOL)a { (void)h; (void)a; }
- (void)setDelegate:(id)d { (void)d; }
@end
@interface UIBarButtonItem : NSObject
@end
@implementation UIBarButtonItem
- (instancetype)initWithBarButtonSystemItem:(NSInteger)i target:(id)t action:(SEL)a { (void)i; (void)t; (void)a; return self; }
- (instancetype)initWithTitle:(NSString*)t style:(NSInteger)s target:(id)tg action:(SEL)a { (void)t; (void)s; (void)tg; (void)a; return self; }
- (instancetype)initWithImage:(UIImage*)i style:(NSInteger)s target:(id)tg action:(SEL)a { (void)i; (void)s; (void)tg; (void)a; return self; }
- (instancetype)initWithCustomView:(UIView*)v { (void)v; return self; }
- (void)setEnabled:(BOOL)e { (void)e; }
- (void)setTintColor:(UIColor*)c { (void)c; }
- (void)setTitle:(NSString*)t { (void)t; }
@end
@interface UIPopoverController : NSObject
@end
@implementation UIPopoverController
- (instancetype)initWithContentViewController:(UIViewController*)vc { (void)vc; return self; }
- (void)presentPopoverFromRect:(CGRect)r inView:(UIView*)v permittedArrowDirections:(NSUInteger)d animated:(BOOL)a { (void)r; (void)v; (void)d; (void)a; }
- (void)dismissPopoverAnimated:(BOOL)a { (void)a; }
- (void)setPopoverContentSize:(CGSize)s { (void)s; }
- (void)setDelegate:(id)d { (void)d; }
- (BOOL)isPopoverVisible { return NO; }
@end
// The photo library is the host: presenting the picker opens a Windows file dialog, or the
// photo / file picker of the phone connected to the remote screen (remote.cpp). The answer
// is polled from the main loop (_uikit_poll_image_picker) and handed to the delegate.
extern NSString* const UIImagePickerControllerOriginalImage;
extern NSString* const UIImagePickerControllerMediaType;
@interface UIImagePickerController : UINavigationController {
@public
    id _pdelegate;
}
@end
static UIImagePickerController* g_picking;
@implementation UIImagePickerController
+ (BOOL)isSourceTypeAvailable:(NSInteger)t { return t == 0 || t == 2; }  // photo library / saved photos
+ (NSArray*)availableMediaTypesForSourceType:(NSInteger)t { (void)t; return [NSArray arrayWithObject:@"public.image"]; }
- (void)setSourceType:(NSInteger)t { (void)t; }
- (NSInteger)sourceType { return 0; }
- (void)setMediaTypes:(NSArray*)t { (void)t; }
- (void)setAllowsEditing:(BOOL)b { (void)b; }
- (void)setDelegate:(id)d { _pdelegate = d; }
- (id)delegate { return _pdelegate; }
- (void)_emuBeginPick {
    if (g_picking) return;
    g_picking = [self retain];
    NSLog(@"[UIKit] image picker: asking the host for an image");
    emu_pick_image_begin();
}
@end
EMU_EXPORT void _uikit_poll_image_picker(void) {
    if (!g_picking) return;
    size_t len = 0;
    int r = emu_pick_image_poll(&len);
    if (r == 0) return;
    UIImagePickerController* p = g_picking;
    g_picking = nil;
    UIImage* img = nil;
    if (r > 0 && len) {
        void* buf = malloc(len);
        emu_pick_image_take(buf);
        img = [UIImage imageWithData:[NSData dataWithBytesNoCopy:buf length:len freeWhenDone:YES]];
        if (!img) NSLog(@"[UIKit] image picker: the file is not an image");
    }
    id d = p->_pdelegate;
    if (img && [d respondsToSelector:@selector(imagePickerController:didFinishPickingMediaWithInfo:)]) {
        NSDictionary* info = [NSDictionary dictionaryWithObjectsAndKeys:img, UIImagePickerControllerOriginalImage, @"public.image",
                                                                        UIImagePickerControllerMediaType, nil];
        ((void (*)(id, SEL, id, id))objc_msgSend)(d, @selector(imagePickerController:didFinishPickingMediaWithInfo:), p, info);
    } else if ([d respondsToSelector:@selector(imagePickerControllerDidCancel:)]) {
        ((void (*)(id, SEL, id))objc_msgSend)(d, @selector(imagePickerControllerDidCancel:), p);
    }
    [p release];
}
@interface UIUserNotificationSettings : NSObject
@end
@implementation UIUserNotificationSettings
+ (instancetype)settingsForTypes:(NSUInteger)t categories:(NSSet*)c { (void)t; (void)c; return [[[self alloc] init] autorelease]; }
- (NSUInteger)types { return 0; }
@end
@interface UIWebView : UIView
@end
@implementation UIWebView
- (void)loadRequest:(id)r { (void)r; }
- (void)loadHTMLString:(NSString*)s baseURL:(id)u { (void)s; (void)u; }
- (NSString*)stringByEvaluatingJavaScriptFromString:(NSString*)s { (void)s; return @""; }
- (void)setDelegate:(id)d { (void)d; }
- (void)setScalesPageToFit:(BOOL)b { (void)b; }
- (void)stopLoading {}
- (id)scrollView { return nil; }
@end
@interface UIPasteboard : NSObject {
    NSString* _s;
}
@end
@implementation UIPasteboard
+ (UIPasteboard*)generalPasteboard { static UIPasteboard* p; if (!p) p = [[UIPasteboard alloc] init]; return p; }
- (NSString*)string { return _s; }
- (void)setString:(NSString*)s { [_s release]; _s = [s copy]; }
@end
@interface UIGestureRecognizer : NSObject
@end
@implementation UIGestureRecognizer
- (instancetype)initWithTarget:(id)t action:(SEL)a { (void)t; (void)a; return self; }
- (void)setDelegate:(id)d { (void)d; }
- (void)setCancelsTouchesInView:(BOOL)b { (void)b; }
- (void)setEnabled:(BOOL)b { (void)b; }
- (NSInteger)state { return 0; }
- (UIView*)view { return nil; }
- (CGPoint)locationInView:(UIView*)v { (void)v; return CGPointMake(0, 0); }
@end
@interface UITapGestureRecognizer : UIGestureRecognizer
@end
@implementation UITapGestureRecognizer
- (void)setNumberOfTapsRequired:(NSUInteger)n { (void)n; }
@end
@interface UIPanGestureRecognizer : UIGestureRecognizer
@end
@implementation UIPanGestureRecognizer
@end
@interface UILongPressGestureRecognizer : UIGestureRecognizer
@end
@implementation UILongPressGestureRecognizer
@end
@interface UISwipeGestureRecognizer : UIGestureRecognizer
@end
@implementation UISwipeGestureRecognizer
@end
@interface NSLayoutConstraint : NSObject
@end
@implementation NSLayoutConstraint
+ (instancetype)constraintWithItem:(id)a attribute:(NSInteger)aa relatedBy:(NSInteger)r toItem:(id)b attribute:(NSInteger)ba multiplier:(CGFloat)m constant:(CGFloat)c {
    (void)a; (void)aa; (void)r; (void)b; (void)ba; (void)m; (void)c;
    return [[[self alloc] init] autorelease];
}
+ (NSArray*)constraintsWithVisualFormat:(NSString*)f options:(NSUInteger)o metrics:(NSDictionary*)m views:(NSDictionary*)v {
    (void)f; (void)o; (void)m; (void)v;
    return [NSArray array];
}
+ (void)activateConstraints:(NSArray*)c { (void)c; }
+ (void)deactivateConstraints:(NSArray*)c { (void)c; }
- (void)setConstant:(CGFloat)c { (void)c; }
- (CGFloat)constant { return 0; }
- (void)setActive:(BOOL)a { (void)a; }
- (void)setPriority:(float)p { (void)p; }
@end
@interface UIStoryboard : NSObject
@end
@implementation UIStoryboard
+ (instancetype)storyboardWithName:(NSString*)n bundle:(id)b { (void)n; (void)b; return nil; }
@end
@interface UINib : NSObject
@end
@implementation UINib
+ (instancetype)nibWithNibName:(NSString*)n bundle:(id)b { (void)n; (void)b; return [[[self alloc] init] autorelease]; }
- (NSArray*)instantiateWithOwner:(id)o options:(NSDictionary*)d { (void)o; (void)d; return [NSArray array]; }
@end
@implementation NSBundle (EmuUIKitNib)
- (NSArray*)loadNibNamed:(NSString*)n owner:(id)o options:(NSDictionary*)d { (void)o; (void)d; NSLog(@"[UIKit] loadNibNamed:%@ not supported", n); return [NSArray array]; }
@end
@interface UIApplicationShortcutItem : NSObject
@end
@implementation UIApplicationShortcutItem
@end
@interface UILocalNotification : NSObject
@end
@implementation UILocalNotification
- (void)setFireDate:(NSDate*)d { (void)d; }
- (void)setAlertBody:(NSString*)b { (void)b; }
- (void)setTimeZone:(id)z { (void)z; }
- (void)setSoundName:(NSString*)s { (void)s; }
- (void)setUserInfo:(NSDictionary*)u { (void)u; }
- (void)setApplicationIconBadgeNumber:(NSInteger)n { (void)n; }
@end
