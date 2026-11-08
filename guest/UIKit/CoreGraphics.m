// CoreGraphics subset: geometry helpers, colors/color spaces, bitmap contexts
// and images (raster only - path drawing is accepted and ignored).
#import <emu/UIKit.h>
#include <emu/host.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

extern id objc_retain(id);
extern void objc_release(id);

// ---- geometry ----
EMU_EXPORT CGFloat CGRectGetMinX(CGRect r) { return r.size.width < 0 ? r.origin.x + r.size.width : r.origin.x; }
EMU_EXPORT CGFloat CGRectGetMinY(CGRect r) { return r.size.height < 0 ? r.origin.y + r.size.height : r.origin.y; }
EMU_EXPORT CGFloat CGRectGetWidth(CGRect r) { return fabs(r.size.width); }
EMU_EXPORT CGFloat CGRectGetHeight(CGRect r) { return fabs(r.size.height); }
EMU_EXPORT CGFloat CGRectGetMaxX(CGRect r) { return CGRectGetMinX(r) + CGRectGetWidth(r); }
EMU_EXPORT CGFloat CGRectGetMaxY(CGRect r) { return CGRectGetMinY(r) + CGRectGetHeight(r); }
EMU_EXPORT CGFloat CGRectGetMidX(CGRect r) { return CGRectGetMinX(r) + CGRectGetWidth(r) / 2; }
EMU_EXPORT CGFloat CGRectGetMidY(CGRect r) { return CGRectGetMinY(r) + CGRectGetHeight(r) / 2; }
EMU_EXPORT BOOL CGRectEqualToRect(CGRect a, CGRect b) {
    return a.origin.x == b.origin.x && a.origin.y == b.origin.y && a.size.width == b.size.width && a.size.height == b.size.height;
}
EMU_EXPORT BOOL CGSizeEqualToSize(CGSize a, CGSize b) { return a.width == b.width && a.height == b.height; }
EMU_EXPORT BOOL CGPointEqualToPoint(CGPoint a, CGPoint b) { return a.x == b.x && a.y == b.y; }
EMU_EXPORT BOOL CGRectIsEmpty(CGRect r) { return r.size.width == 0 || r.size.height == 0; }
EMU_EXPORT BOOL CGRectIsNull(CGRect r) { return r.origin.x == 1e300; }
EMU_EXPORT BOOL CGRectContainsPoint(CGRect r, CGPoint p) {
    return p.x >= CGRectGetMinX(r) && p.x < CGRectGetMaxX(r) && p.y >= CGRectGetMinY(r) && p.y < CGRectGetMaxY(r);
}
EMU_EXPORT BOOL CGRectIntersectsRect(CGRect a, CGRect b) {
    return CGRectGetMinX(a) < CGRectGetMaxX(b) && CGRectGetMinX(b) < CGRectGetMaxX(a) && CGRectGetMinY(a) < CGRectGetMaxY(b) &&
           CGRectGetMinY(b) < CGRectGetMaxY(a);
}
EMU_EXPORT CGRect CGRectInset(CGRect r, CGFloat dx, CGFloat dy) { return CGRectMake(r.origin.x + dx, r.origin.y + dy, r.size.width - 2 * dx, r.size.height - 2 * dy); }
EMU_EXPORT CGRect CGRectOffset(CGRect r, CGFloat dx, CGFloat dy) { return CGRectMake(r.origin.x + dx, r.origin.y + dy, r.size.width, r.size.height); }
EMU_EXPORT CGRect CGRectIntegral(CGRect r) {
    CGFloat x = floor(r.origin.x), y = floor(r.origin.y);
    return CGRectMake(x, y, ceil(r.origin.x + r.size.width) - x, ceil(r.origin.y + r.size.height) - y);
}
EMU_EXPORT CGRect CGRectStandardize(CGRect r) { return CGRectMake(CGRectGetMinX(r), CGRectGetMinY(r), CGRectGetWidth(r), CGRectGetHeight(r)); }
EMU_EXPORT CGRect CGRectUnion(CGRect a, CGRect b) {
    CGFloat x0 = fmin(CGRectGetMinX(a), CGRectGetMinX(b)), y0 = fmin(CGRectGetMinY(a), CGRectGetMinY(b));
    return CGRectMake(x0, y0, fmax(CGRectGetMaxX(a), CGRectGetMaxX(b)) - x0, fmax(CGRectGetMaxY(a), CGRectGetMaxY(b)) - y0);
}
EMU_EXPORT CGRect CGRectIntersection(CGRect a, CGRect b) {
    CGFloat x0 = fmax(CGRectGetMinX(a), CGRectGetMinX(b)), y0 = fmax(CGRectGetMinY(a), CGRectGetMinY(b));
    CGFloat x1 = fmin(CGRectGetMaxX(a), CGRectGetMaxX(b)), y1 = fmin(CGRectGetMaxY(a), CGRectGetMaxY(b));
    if (x1 < x0 || y1 < y0) return CGRectMake(1e300, 1e300, 0, 0);
    return CGRectMake(x0, y0, x1 - x0, y1 - y0);
}
EMU_EXPORT CGAffineTransform CGAffineTransformMakeScale(CGFloat sx, CGFloat sy) { CGAffineTransform t = {sx, 0, 0, sy, 0, 0}; return t; }
EMU_EXPORT CGAffineTransform CGAffineTransformMakeTranslation(CGFloat x, CGFloat y) { CGAffineTransform t = {1, 0, 0, 1, x, y}; return t; }
EMU_EXPORT CGAffineTransform CGAffineTransformMakeRotation(CGFloat a) { CGAffineTransform t = {cos(a), sin(a), -sin(a), cos(a), 0, 0}; return t; }
EMU_EXPORT BOOL CGAffineTransformIsIdentity(CGAffineTransform t) { return t.a == 1 && t.b == 0 && t.c == 0 && t.d == 1 && t.tx == 0 && t.ty == 0; }

// ---- color spaces / colors ----
@interface __CGColorSpace : NSObject
@end
@implementation __CGColorSpace
@end
@interface __CGColor : NSObject {
@public
    CGFloat _c[4];
}
@end
@implementation __CGColor
@end
EMU_EXPORT id CGColorSpaceCreateDeviceRGB(void) { return [[__CGColorSpace alloc] init]; }
EMU_EXPORT id CGColorSpaceCreateDeviceGray(void) { return [[__CGColorSpace alloc] init]; }
EMU_EXPORT id CGColorSpaceCreateWithName(id n) { (void)n; return [[__CGColorSpace alloc] init]; }
EMU_EXPORT void CGColorSpaceRelease(id c) { if (c) objc_release(c); }
EMU_EXPORT id CGColorSpaceRetain(id c) { return c ? objc_retain(c) : nil; }
EMU_EXPORT size_t CGColorSpaceGetNumberOfComponents(id c) { (void)c; return 3; }
EMU_EXPORT int CGColorSpaceGetModel(id c) { (void)c; return 1; }
EMU_EXPORT id CGColorCreate(id space, const CGFloat* comps) {
    (void)space;
    __CGColor* c = [[__CGColor alloc] init];
    for (int i = 0; i < 4; i++) c->_c[i] = comps ? comps[i] : 0;
    return c;
}
EMU_EXPORT id CGColorGetColorSpace(id c) { (void)c; static id cs; if (!cs) cs = CGColorSpaceCreateDeviceRGB(); return cs; }
EMU_EXPORT const CGFloat* CGColorGetComponents(id c) { return c ? ((__CGColor*)c)->_c : NULL; }
EMU_EXPORT size_t CGColorGetNumberOfComponents(id c) { (void)c; return 4; }
EMU_EXPORT CGFloat CGColorGetAlpha(id c) { return c ? ((__CGColor*)c)->_c[3] : 0; }
EMU_EXPORT void CGColorRelease(id c) { if (c) objc_release(c); }
EMU_EXPORT id CGColorRetain(id c) { return c ? objc_retain(c) : nil; }
EMU_EXPORT id CGColorCreateCopyWithAlpha(id c, CGFloat a) {
    __CGColor* n = CGColorCreate(nil, ((__CGColor*)c)->_c);
    n->_c[3] = a;
    return n;
}

// ---- images ----
@interface __CGImage : NSObject {
@public
    size_t _w, _h, _bpr;
    uint8_t* _px;  // RGBA8, alpha as given by _alphaInfo
    uint32_t _alphaInfo;
}
@end
@implementation __CGImage
- (void)dealloc { free(_px); [super dealloc]; }
@end
EMU_EXPORT id _cg_image_create_rgba(const void* rgba, int w, int h, BOOL premultiplied) {
    __CGImage* i = [[__CGImage alloc] init];
    i->_w = (size_t)w;
    i->_h = (size_t)h;
    i->_bpr = (size_t)w * 4;
    i->_px = malloc(i->_bpr * (size_t)h + 1);
    if (rgba) memcpy(i->_px, rgba, i->_bpr * (size_t)h);
    else memset(i->_px, 0, i->_bpr * (size_t)h);
    i->_alphaInfo = premultiplied ? 1 : 3;  // kCGImageAlphaPremultipliedLast / kCGImageAlphaLast
    return i;
}
EMU_EXPORT const uint8_t* _cg_image_pixels(id img) { return img ? ((__CGImage*)img)->_px : NULL; }
EMU_EXPORT size_t CGImageGetWidth(id i) { return i ? ((__CGImage*)i)->_w : 0; }
EMU_EXPORT size_t CGImageGetHeight(id i) { return i ? ((__CGImage*)i)->_h : 0; }
EMU_EXPORT size_t CGImageGetBitsPerComponent(id i) { (void)i; return 8; }
EMU_EXPORT size_t CGImageGetBitsPerPixel(id i) { (void)i; return 32; }
EMU_EXPORT size_t CGImageGetBytesPerRow(id i) { return i ? ((__CGImage*)i)->_bpr : 0; }
EMU_EXPORT uint32_t CGImageGetAlphaInfo(id i) { return i ? ((__CGImage*)i)->_alphaInfo : 0; }
EMU_EXPORT uint32_t CGImageGetBitmapInfo(id i) { return i ? ((__CGImage*)i)->_alphaInfo : 0; }
EMU_EXPORT id CGImageGetColorSpace(id i) { (void)i; return CGColorGetColorSpace(nil); }
EMU_EXPORT void CGImageRelease(id i) { if (i) objc_release(i); }
EMU_EXPORT id CGImageRetain(id i) { return i ? objc_retain(i) : nil; }
EMU_EXPORT id CGImageCreateWithImageInRect(id img, CGRect r) {
    __CGImage* s = (__CGImage*)img;
    if (!s) return nil;
    int x0 = (int)fmax(0, r.origin.x), y0 = (int)fmax(0, r.origin.y);
    int w = (int)fmin((double)s->_w - x0, r.size.width), h = (int)fmin((double)s->_h - y0, r.size.height);
    if (w <= 0 || h <= 0) return nil;
    __CGImage* d = _cg_image_create_rgba(NULL, w, h, s->_alphaInfo == 1);
    for (int y = 0; y < h; y++) memcpy(d->_px + (size_t)y * d->_bpr, s->_px + (size_t)(y0 + y) * s->_bpr + (size_t)x0 * 4, (size_t)w * 4);
    return d;
}
EMU_EXPORT id CGImageCreateCopy(id img) { return img ? CGImageCreateWithImageInRect(img, CGRectMake(0, 0, CGImageGetWidth(img), CGImageGetHeight(img))) : nil; }

// ---- bitmap contexts ----
@interface __CGContext : NSObject {
@public
    uint8_t* _data;
    BOOL _owns;
    size_t _w, _h, _bpr, _bpc;
    uint32_t _info;
    CGFloat _tx, _ty, _sx, _sy;
    CGFloat _stack[16][4];
    int _sp;
}
@end
@implementation __CGContext
- (void)dealloc { if (_owns) free(_data); [super dealloc]; }
@end
EMU_EXPORT id CGBitmapContextCreate(void* data, size_t w, size_t h, size_t bpc, size_t bpr, id space, uint32_t info) {
    (void)space;
    if (bpc != 8) return nil;
    __CGContext* c = [[__CGContext alloc] init];
    c->_w = w;
    c->_h = h;
    c->_bpc = bpc;
    c->_bpr = bpr ? bpr : w * 4;
    c->_info = info;
    c->_sx = c->_sy = 1;
    if (data) c->_data = data;
    else {
        c->_data = calloc(1, c->_bpr * h + 1);
        c->_owns = YES;
    }
    return c;
}
EMU_EXPORT void* CGBitmapContextGetData(id c) { return c ? ((__CGContext*)c)->_data : NULL; }
EMU_EXPORT size_t CGBitmapContextGetWidth(id c) { return c ? ((__CGContext*)c)->_w : 0; }
EMU_EXPORT size_t CGBitmapContextGetHeight(id c) { return c ? ((__CGContext*)c)->_h : 0; }
EMU_EXPORT size_t CGBitmapContextGetBytesPerRow(id c) { return c ? ((__CGContext*)c)->_bpr : 0; }
EMU_EXPORT void CGContextRelease(id c) { if (c) objc_release(c); }
EMU_EXPORT id CGContextRetain(id c) { return c ? objc_retain(c) : nil; }
EMU_EXPORT id CGBitmapContextCreateImage(id cv) {
    __CGContext* c = (__CGContext*)cv;
    if (!c) return nil;
    __CGImage* i = _cg_image_create_rgba(NULL, (int)c->_w, (int)c->_h, (c->_info & 0x1f) == 1);
    for (size_t y = 0; y < c->_h; y++) memcpy(i->_px + y * i->_bpr, c->_data + y * c->_bpr, c->_w * 4);
    return i;
}
// Draws img scaled into rect (CG coordinates: origin bottom-left of the context).
EMU_EXPORT void CGContextDrawImage(id cv, CGRect r, id img) {
    __CGContext* c = (__CGContext*)cv;
    __CGImage* s = (__CGImage*)img;
    if (!c || !s || !s->_w || !s->_h) return;
    r.origin.x = r.origin.x * c->_sx + c->_tx;
    r.origin.y = r.origin.y * c->_sy + c->_ty;
    r.size.width *= c->_sx;
    r.size.height *= c->_sy;
    BOOL flipped = r.size.height < 0;
    if (flipped) { r.origin.y += r.size.height; r.size.height = -r.size.height; }
    int dw = (int)lround(r.size.width), dh = (int)lround(r.size.height);
    int dx0 = (int)lround(r.origin.x);
    // memory row 0 is the top of the context; CG y grows upwards
    int dy_top = (int)c->_h - (int)lround(r.origin.y) - dh;
    BOOL dstPremul = (c->_info & 0x1f) == 1 || (c->_info & 0x1f) == 2;
    BOOL srcPremul = s->_alphaInfo == 1 || s->_alphaInfo == 2;
    BOOL alphaFirst = (c->_info & 0x1f) == 2 || (c->_info & 0x1f) == 4 || (c->_info & 0x1f) == 6;
    BOOL bgr = (c->_info & 0x7000) == 0x2000;  // kCGBitmapByteOrder32Little
    for (int y = 0; y < dh; y++) {
        int ty = dy_top + y;
        if (ty < 0 || ty >= (int)c->_h) continue;
        int sy = (int)((long long)y * (long long)s->_h / dh);
        if (flipped) sy = (int)s->_h - 1 - sy;
        for (int x = 0; x < dw; x++) {
            int tx = dx0 + x;
            if (tx < 0 || tx >= (int)c->_w) continue;
            int sx = (int)((long long)x * (long long)s->_w / dw);
            const uint8_t* sp = s->_px + (size_t)sy * s->_bpr + (size_t)sx * 4;
            uint8_t rr = sp[0], gg = sp[1], bb = sp[2], aa = sp[3];
            if (dstPremul && !srcPremul) { rr = (uint8_t)(rr * aa / 255); gg = (uint8_t)(gg * aa / 255); bb = (uint8_t)(bb * aa / 255); }
            else if (!dstPremul && srcPremul && aa) { rr = (uint8_t)fmin(255, rr * 255 / aa); gg = (uint8_t)fmin(255, gg * 255 / aa); bb = (uint8_t)fmin(255, bb * 255 / aa); }
            uint8_t* dp = c->_data + (size_t)ty * c->_bpr + (size_t)tx * 4;
            uint8_t px[4];
            if (alphaFirst) { px[0] = aa; px[1] = rr; px[2] = gg; px[3] = bb; }
            else { px[0] = rr; px[1] = gg; px[2] = bb; px[3] = aa; }
            if (bgr) { dp[0] = px[3]; dp[1] = px[2]; dp[2] = px[1]; dp[3] = px[0]; }
            else memcpy(dp, px, 4);
        }
    }
}
EMU_EXPORT void CGContextClearRect(id cv, CGRect r) {
    __CGContext* c = (__CGContext*)cv;
    if (!c) return;
    int x0 = (int)fmax(0, r.origin.x), w = (int)fmin((double)c->_w - x0, r.size.width);
    int ytop = (int)c->_h - (int)(r.origin.y + r.size.height);
    for (int y = (int)fmax(0, ytop); y < (int)fmin((double)c->_h, ytop + r.size.height); y++)
        if (w > 0) memset(c->_data + (size_t)y * c->_bpr + (size_t)x0 * 4, 0, (size_t)w * 4);
}
EMU_EXPORT void CGContextFillRect(id cv, CGRect r) { (void)cv; (void)r; }
EMU_EXPORT void CGContextTranslateCTM(id cv, CGFloat x, CGFloat y) { __CGContext* c = (__CGContext*)cv; if (c) { c->_tx += x * c->_sx; c->_ty += y * c->_sy; } }
EMU_EXPORT void CGContextScaleCTM(id cv, CGFloat x, CGFloat y) { __CGContext* c = (__CGContext*)cv; if (c) { c->_sx *= x; c->_sy *= y; } }
EMU_EXPORT void CGContextRotateCTM(id cv, CGFloat a) { (void)cv; (void)a; }
EMU_EXPORT void CGContextConcatCTM(id cv, CGAffineTransform t) { (void)cv; (void)t; }
EMU_EXPORT void CGContextSaveGState(id cv) {
    __CGContext* c = (__CGContext*)cv;
    if (!c || c->_sp >= 16) return;
    c->_stack[c->_sp][0] = c->_tx; c->_stack[c->_sp][1] = c->_ty; c->_stack[c->_sp][2] = c->_sx; c->_stack[c->_sp][3] = c->_sy;
    c->_sp++;
}
EMU_EXPORT void CGContextRestoreGState(id cv) {
    __CGContext* c = (__CGContext*)cv;
    if (!c || c->_sp <= 0) return;
    c->_sp--;
    c->_tx = c->_stack[c->_sp][0]; c->_ty = c->_stack[c->_sp][1]; c->_sx = c->_stack[c->_sp][2]; c->_sy = c->_stack[c->_sp][3];
}
#define CGNOOP(name, ...) EMU_EXPORT void name(id c, ##__VA_ARGS__) { (void)c; }
EMU_EXPORT void CGContextAddArcToPoint(id c, CGFloat a, CGFloat b, CGFloat d, CGFloat e, CGFloat r) { (void)c; (void)a; (void)b; (void)d; (void)e; (void)r; }
EMU_EXPORT void CGContextAddLineToPoint(id c, CGFloat x, CGFloat y) { (void)c; (void)x; (void)y; }
EMU_EXPORT void CGContextMoveToPoint(id c, CGFloat x, CGFloat y) { (void)c; (void)x; (void)y; }
EMU_EXPORT void CGContextAddRect(id c, CGRect r) { (void)c; (void)r; }
EMU_EXPORT void CGContextBeginPath(id c) { (void)c; }
EMU_EXPORT void CGContextClosePath(id c) { (void)c; }
EMU_EXPORT void CGContextClip(id c) { (void)c; }
EMU_EXPORT void CGContextStrokePath(id c) { (void)c; }
EMU_EXPORT void CGContextFillPath(id c) { (void)c; }
EMU_EXPORT void CGContextSetLineWidth(id c, CGFloat w) { (void)c; (void)w; }
EMU_EXPORT void CGContextSetStrokeColorWithColor(id c, id col) { (void)c; (void)col; }
EMU_EXPORT void CGContextSetFillColorWithColor(id c, id col) { (void)c; (void)col; }
EMU_EXPORT void CGContextSetRGBFillColor(id c, CGFloat r, CGFloat g, CGFloat b, CGFloat a) { (void)c; (void)r; (void)g; (void)b; (void)a; }
EMU_EXPORT void CGContextSetBlendMode(id c, int m) { (void)c; (void)m; }
EMU_EXPORT void CGContextSetInterpolationQuality(id c, int q) { (void)c; (void)q; }
EMU_EXPORT void CGContextSetShouldAntialias(id c, BOOL b) { (void)c; (void)b; }
EMU_EXPORT void CGContextDrawLinearGradient(id c, id g, CGPoint a, CGPoint b, uint32_t o) { (void)c; (void)g; (void)a; (void)b; (void)o; }
EMU_EXPORT id CGGradientCreateWithColors(id space, id colors, const CGFloat* loc) { (void)space; (void)colors; (void)loc; return [[NSObject alloc] init]; }
EMU_EXPORT id CGGradientCreateWithColorComponents(id space, const CGFloat* c, const CGFloat* l, size_t n) { (void)space; (void)c; (void)l; (void)n; return [[NSObject alloc] init]; }
EMU_EXPORT void CGGradientRelease(id g) { if (g) objc_release(g); }
EMU_EXPORT id CGPathCreateMutable(void) { return [[NSObject alloc] init]; }
EMU_EXPORT void CGPathRelease(id p) { if (p) objc_release(p); }
EMU_EXPORT void CGPathAddRect(id p, const void* t, CGRect r) { (void)p; (void)t; (void)r; }
EMU_EXPORT void CGPathMoveToPoint(id p, const void* t, CGFloat x, CGFloat y) { (void)p; (void)t; (void)x; (void)y; }
EMU_EXPORT void CGPathAddLineToPoint(id p, const void* t, CGFloat x, CGFloat y) { (void)p; (void)t; (void)x; (void)y; }
EMU_EXPORT void CGPathCloseSubpath(id p) { (void)p; }
EMU_EXPORT void CGContextAddPath(id c, id p) { (void)c; (void)p; }

// ---- data providers (images from raw data) ----
EMU_EXPORT id CGDataProviderCreateWithCFData(id data) { return data ? objc_retain(data) : nil; }
EMU_EXPORT id CGDataProviderCreateWithData(void* info, const void* data, size_t size, void* release) {
    (void)info; (void)release;
    return [[NSData alloc] initWithBytes:data length:size];
}
EMU_EXPORT void CGDataProviderRelease(id p) { if (p) objc_release(p); }
EMU_EXPORT id CGImageCreateWithPNGDataProvider(id provider, const CGFloat* decode, BOOL interp, int intent) {
    (void)decode; (void)interp; (void)intent;
    NSData* d = provider;
    int w = 0, h = 0;
    void* px = emu_image_decode([d bytes], [d length], &w, &h);
    if (!px) return nil;
    id img = _cg_image_create_rgba(px, w, h, NO);
    free(px);
    return img;
}
EMU_EXPORT id CGImageCreateWithJPEGDataProvider(id provider, const CGFloat* decode, BOOL interp, int intent) {
    return CGImageCreateWithPNGDataProvider(provider, decode, interp, intent);
}
EMU_EXPORT id CGImageCreate(size_t w, size_t h, size_t bpc, size_t bpp, size_t bpr, id cs, uint32_t info, id provider, const CGFloat* d, BOOL i, int intent) {
    (void)bpc; (void)cs; (void)d; (void)i; (void)intent;
    NSData* data = provider;
    if (bpp != 32) return nil;
    __CGImage* img = _cg_image_create_rgba(NULL, (int)w, (int)h, (info & 0x1f) == 1);
    for (size_t y = 0; y < h; y++) memcpy(img->_px + y * img->_bpr, (const uint8_t*)[data bytes] + y * bpr, w * 4);
    return img;
}
EMU_EXPORT id CGImageGetDataProvider(id img) {
    __CGImage* i = (__CGImage*)img;
    return i ? [NSData dataWithBytes:i->_px length:i->_bpr * i->_h] : nil;
}
EMU_EXPORT id CGDataProviderCopyData(id p) { return p ? objc_retain(p) : nil; }
