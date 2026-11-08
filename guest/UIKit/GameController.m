// GameController / StoreKit placeholders: no controllers, no store.
#import <emu/UIKit.h>
#include <emu/msg.h>

EMU_EXPORT NSString* const GCControllerDidConnectNotification = @"GCControllerDidConnectNotification";
EMU_EXPORT NSString* const GCControllerDidDisconnectNotification = @"GCControllerDidDisconnectNotification";

@interface GCController : NSObject
@end
@implementation GCController
+ (NSArray*)controllers { return [NSArray array]; }
+ (void)startWirelessControllerDiscoveryWithCompletionHandler:(void (^)(void))h { if (h) h(); }
+ (void)stopWirelessControllerDiscovery {}
- (id)extendedGamepad { return nil; }
- (id)gamepad { return nil; }
- (id)microGamepad { return nil; }
- (NSString*)vendorName { return @""; }
- (BOOL)isAttachedToDevice { return NO; }
- (NSInteger)playerIndex { return -1; }
- (void)setPlayerIndex:(NSInteger)i { (void)i; }
- (void)setControllerPausedHandler:(id)h { (void)h; }
@end

@interface SKPaymentQueue : NSObject
@end
@implementation SKPaymentQueue
+ (instancetype)defaultQueue { static SKPaymentQueue* q; if (!q) q = [[SKPaymentQueue alloc] init]; return q; }
+ (BOOL)canMakePayments { return NO; }
- (void)addTransactionObserver:(id)o { (void)o; }
- (void)removeTransactionObserver:(id)o { (void)o; }
- (void)addPayment:(id)p { (void)p; }
- (void)restoreCompletedTransactions {}
- (void)finishTransaction:(id)t { (void)t; }
- (NSArray*)transactions { return [NSArray array]; }
@end
@interface SKRequest : NSObject {
@public
    id _delegate;
}
@end
@implementation SKRequest
- (void)setDelegate:(id)d { _delegate = d; }
- (id)delegate { return _delegate; }
- (void)start {
    if ([_delegate respondsToSelector:@selector(request:didFailWithError:)])
        ((void (*)(id, SEL, id, id))objc_msgSend)(_delegate, @selector(request:didFailWithError:), self,
                                                  [NSError errorWithDomain:@"SKErrorDomain" code:0 userInfo:nil]);
}
- (void)cancel {}
@end
@interface SKProductsRequest : SKRequest
@end
@implementation SKProductsRequest
- (instancetype)initWithProductIdentifiers:(NSSet*)s { (void)s; return self; }
@end
@interface SKReceiptRefreshRequest : SKRequest
@end
@implementation SKReceiptRefreshRequest
- (instancetype)initWithReceiptProperties:(NSDictionary*)p { (void)p; return self; }
@end
